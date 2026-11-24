# 10 — Testing and Debugging

This chapter makes correctness visible. You will write tests with the standard library alone,
create isolated temporary directories by hand, control time and storage in tests, and learn a
repeatable way to read compiler errors instead of guessing at them.

## Why this matters for Task Tracker

The definition of done in `LEARNING_PATH.md` includes manual error cases, clean gates
(`cargo fmt --check`, `cargo clippy`, `cargo test`), and an explanation of every line of code.
Tests are how you earn that last bullet. A test forces you to answer "what exactly should happen?"
before the compiler forces you to answer "what exactly is the type?".

Two project details make testing interesting:

- **No external crates in the CLI track.** There is no `tempfile`, so test isolation is your own
  small utility. The API track adds `tower` as a dev-dependency for `oneshot` tests.
- **Real time and real files are nondeterministic.** You will inject a clock and a store so the
  same test produces the same result every run. See `09-modules-and-architecture.md`.

## What you will learn

- How `cargo test` discovers `#[test]` functions and `#[cfg(test)] mod tests`
- How to use `assert!`, `assert_eq!`, `assert_ne!`, and `matches!` with useful failure messages
- How to write table-driven tests from arrays of structs and assert `Result` values, error variants,
  and error payloads
- How to build unique temporary directories with `std` only, and clean them up with an RAII guard
- How to inject clock and store traits for deterministic tests
- How `--nocapture` and `--test-threads=1` change test runs
- How to use `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings`, `dbg!`, and backtraces
- How to read the six most common compiler error codes
- A systematic debugging workflow and a manual CLI smoke-test routine

---

## 1. The test harness

Cargo builds a **test harness** for you: a generated binary that finds functions marked `#[test]`,
runs them, and prints a summary. A **unit test** lives inside the crate and can see private items;
an **integration test** lives in `tests/` and uses only the library's public API.

```rust
// src/notes.rs
pub struct Note {
    title: String,
}

impl Note {
    pub fn new(title: &str) -> Self {
        Self { title: title.trim().to_string() }
    }

    pub fn is_blank(&self) -> bool {
        self.title.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn blank_titles_are_detected() {
        assert!(Note::new("   ").is_blank());
        assert!(!Note::new("Milk").is_blank());
    }
}
```

`#[cfg(test)]` means "compile this only for tests", so the module adds nothing to the release
binary. `use super::*` brings the parent module's names into the test scope — including private
ones, because the test module is a descendant.

`cargo test` prints something like:

```text
running 1 test
test notes::tests::blank_titles_are_detected ... ok
test result: ok. 1 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out
```

A test fails by panicking. The harness catches the panic, reports the test name and line, and
continues with the other tests.

> **Professional habit.** Name tests after the behavior, not the function:
> `blank_titles_are_detected` beats `test_is_blank_1`.

---

## 2. Assertions

| Macro | Use it for |
|---|---|
| `assert!(condition)` | A boolean property |
| `assert_eq!(left, right)` | Equality; prints both values on failure |
| `assert_ne!(left, right)` | Inequality |
| `matches!(expr, pattern)` | Testing an enum variant or shape |

```rust
#[derive(Debug, PartialEq, Eq)]
enum NoteKind { Draft, Published }

#[derive(Debug, PartialEq, Eq)]
enum ParseNoteKindError { Empty, Unknown(String) }

fn parse_note_kind(raw: &str) -> Result<NoteKind, ParseNoteKindError> {
    match raw.trim() {
        "draft" => Ok(NoteKind::Draft),
        "published" => Ok(NoteKind::Published),
        "" => Err(ParseNoteKindError::Empty),
        other => Err(ParseNoteKindError::Unknown(other.to_string())),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_known_kinds() {
        assert_eq!(parse_note_kind("draft"), Ok(NoteKind::Draft));
        assert_eq!(parse_note_kind(" published "), Ok(NoteKind::Published));
    }

    #[test]
    fn rejects_unknown_kind() {
        assert!(matches!(
            parse_note_kind("archived"),
            Err(ParseNoteKindError::Unknown(_))
        ));
    }

    #[test]
    fn rejects_empty_kind() {
        assert_ne!(parse_note_kind(""), Ok(NoteKind::Draft));
        assert_eq!(parse_note_kind(""), Err(ParseNoteKindError::Empty));
    }
}
```

`assert_eq!` and `assert_ne!` require `Debug` on both values so they can print a diff; the
`Result` comparison also requires `PartialEq` on its inner types. `matches!` needs neither.

All assertion macros accept an optional format string as a built-in failure report, for example
`assert_eq!(parse_note_kind("draft"), Ok(NoteKind::Draft), "lowercase draft");`.

### Table-driven tests

When many inputs share one rule, put cases in an array of structs and loop. This keeps the test
short and makes missing cases obvious.

```rust
struct Case {
    input: &'static str,
    expected: Result<NoteKind, ParseNoteKindError>,
}

#[test]
fn all_kinds_in_one_table() {
    let cases = [
        Case { input: "draft", expected: Ok(NoteKind::Draft) },
        Case { input: "published", expected: Ok(NoteKind::Published) },
        Case { input: " DRAFT ", expected: Err(ParseNoteKindError::Unknown("DRAFT".to_string())) },
        Case { input: "", expected: Err(ParseNoteKindError::Empty) },
    ];

    for case in cases {
        assert_eq!(
            parse_note_kind(case.input),
            case.expected,
            "unexpected result for input {:?}", case.input
        );
    }
}
```

### Asserting on error payloads

`matches!` tells you the variant; a `match` also lets you check the value inside it:

```rust
match parse_note_kind("archive") {
    Err(ParseNoteKindError::Unknown(value)) => assert_eq!(value, "archive"),
    other => panic!("unexpected result: {other:?}"),
}
```

`unwrap_err()` is convenient (`let err = parse_note_kind("").unwrap_err();`) but requires the
success type to implement `Debug`. Prefer explicit matches when the variant matters.

> **Professional habit.** Add the case message argument to table tests. "left != right" in a loop
> is useless; "unexpected result for input \"DRAFT\"" tells you exactly which row failed.

---

## 3. Temporary directories without a crate

Tests must not write into the project directory or into a shared path. Build a unique directory
under `std::env::temp_dir()` from three sources of uniqueness: process ID, a monotonic atomic
counter, and nanoseconds since the Unix epoch.

```rust
use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

static NONCE: AtomicU64 = AtomicU64::new(0);

fn unique_temp_dir(label: &str) -> PathBuf {
    let nanos = SystemTime::now().duration_since(UNIX_EPOCH)
        .map(|d| d.as_nanos()).unwrap_or(0);
    let sequence = NONCE.fetch_add(1, Ordering::Relaxed);
    env::temp_dir().join(format!("rust-{label}-{}-{nanos}-{sequence}", process::id()))
}

struct TempDir { path: PathBuf }

impl TempDir {
    fn new(label: &str) -> std::io::Result<Self> {
        let path = unique_temp_dir(label);
        fs::create_dir_all(&path)?;
        Ok(Self { path })
    }

    fn path(&self) -> &Path { &self.path }
}

impl Drop for TempDir {
    fn drop(&mut self) { let _ = fs::remove_dir_all(&self.path); } // only our own path
}
```

**RAII** (Resource Acquisition Is Initialization) means tying cleanup to a value's lifetime: the
`Drop` implementation runs when the owner goes out of scope, including on early return or panic.
You get cleanup without remembering a call at every exit point. `process::exit` skips `Drop`, which
is another reason to prefer `ExitCode` from `main` (`07-filesystem-and-cli.md`).

Safety rules for this little guard:

- Never accept an arbitrary path from a test and delete it — the guard owns its own generated path.
- Use a unique name because tests run in parallel by default.
- `remove_dir_all` deletes recursively; keep the label specific to this project.
- Best-effort cleanup (`let _ = ...`) is acceptable in `Drop`, since `Drop` cannot return an error.

A test using it simply creates the guard, works inside `temp.path()`, and lets the guard drop.

> **Professional habit.** Keep the temp-dir helper in the test module where it is used until a
> second module needs it. Do not solve a problem you do not have yet.

---

## 4. Determinism: inject the clock and the store

A **test double** is a stand-in for a real dependency. If the service owns a `Clock` trait and a
`Store` trait (`09-modules-and-architecture.md`), tests can supply fixed versions.

```rust
pub trait Clock { fn now_epoch_secs(&self) -> i64; }

pub fn stamp(clock: &impl Clock) -> i64 { clock.now_epoch_secs() }

struct FixedClock { secs: i64 }
impl Clock for FixedClock {
    fn now_epoch_secs(&self) -> i64 { self.secs }
}

#[test]
fn stamp_is_controlled_by_the_injected_clock() {
    let clock = FixedClock { secs: 1_700_000_000 };
    assert_eq!(stamp(&clock), 1_700_000_000);
}
```

No sleeping, no reading the wall clock, no "roughly now" assertions. Combine a fixed clock with a
`TempDir` and a file-backed store and you can assert exact timestamps and exact file contents.
An in-memory store implementing the same `Store` trait is even faster for pure rule tests.

In production the clock wraps `SystemTime::now()` and the store reads/writes `tasks.json` under
cwd; in tests use a `FixedClock` and an in-memory `Vec` or a file store pointed at `TempDir`.
Keep the codec tests independent of files: feed it `&str`, assert the parsed value or error. That
is why `06-json-without-crates.md` separates the grammar from I/O.

> **Professional habit.** If a test would pass with the real clock or real cwd swapped in, it is
> probably not testing the rule you think it is.

---

## 5. Running and filtering tests

```bash
cargo test                            # everything: unit, integration, doc tests
cargo test parses_known               # run tests whose names contain this text
cargo test -- --nocapture             # show captured stdout/stderr
cargo test -- --test-threads=1        # run tests serially
cargo test -- --nocapture --test-threads=1
```

By default the harness captures output from passing tests; `--nocapture` releases it. Use
`--test-threads=1` when tests share a global resource (an environment variable, a fixed port, or a
well-known path). Unique temp dirs normally mean you do **not** need this flag.

---

## 6. Debugging tools

### `dbg!` and `eprintln!`

```rust
let parsed = dbg!(parse_note_kind("draft"));
```

`dbg!` prints the expression, its value, and the file/line to **stderr**, then *returns the value*,
so it can be dropped into an expression without changing types. `eprintln!` just prints a message.
Both are temporary instruments: remove them before finishing. Remember that the test harness
captures stderr too — use `--nocapture` to see it.

### Backtraces

```bash
RUST_BACKTRACE=1 cargo run -- add "Milk"
RUST_BACKTRACE=full cargo test
```

A **backtrace** lists the chain of function calls that led to a panic, innermost first. It is
invaluable when a panic happens inside a helper. Setting `RUST_BACKTRACE=1` is temporary and local
to that command.

### Quality gates

```bash
cargo fmt --check                          # does nothing but report formatting differences
cargo fmt                                  # apply rustfmt to the whole crate
cargo clippy --all-targets -- -D warnings  # lint source AND tests, fail on any warning
```

**rustfmt** is the standard formatter; **clippy** is the standard linter. `--all-targets` includes
unit tests, integration tests, and examples — not just `src/`. `-D warnings` promotes every
warning to an error, which keeps the gate strict. Run all three gates before you call a phase done,
as in `12-professional-habits.md`.

---

## 7. Reading compiler diagnostics

Read the **first** error fully: Rust lists later errors as consequences. The error code, message,
span, and "note" lines are a tutorial. Offline detail is available:

```bash
rustc --explain E0382
```

| Code | Meaning | Typical cause and fix |
|---|---|---|
| `E0382` | Use of a moved value | `let s = String::from("x"); takes(s); println!("{s}");` — pass `&s`, clone, or return ownership |
| `E0502` / `E0503` | Borrow conflict | Holding `&v` while calling `v.push(...)` ends the mutable borrow attempt; shorten the immutable borrow, e.g. compute the result first |
| `E0597` | Borrowed value does not live long enough | Returning a `&str` to a local `String`; return an owned `String` instead |
| `E0277` | Trait bound not satisfied | Calling `.clone()` on a type without `Clone`, or `?` when no `From` conversion exists; derive/implement the trait or change the signature |
| `E0432` | Unresolved import | Typo, missing `mod foo;` declaration, wrong `crate::` path, or a crate that is not in `Cargo.toml` |
| `E0609` | No field on this type | Wrong field name, or you have a `Result`/`Option` and need `?` or an unwrap first |

Neighbouring codes (`E0499`, `E0507`) often carry the same lesson; the message text matters more
than memorising numbers. Rust prints the type it actually saw — read it, do not assume it.

For `E0502`, `let first = &names[0]; names.push("extra".into()); println!("{first}");` fails because
the immutable borrow is still alive when `push` asks for a mutable one. Use or copy `first` first.

### A systematic workflow

1. Read the first error's message and highlighted span; write down what it says in your own words.
2. Identify the two types or owners involved. Rust usually prints them.
3. Reduce to the smallest example that reproduces the error, ideally in `learnrust/` or a scratch
   crate.
4. State the intended ownership or trait relationship before changing code.
5. Apply the smallest fix; re-run `cargo check`, then `cargo test`.
6. If the error changes into a different one, celebrate — you made progress. Read it again.
7. Record the error, cause, and fix in your build journal.

> **Professional habit.** Guess-and-check is slow; the compiler is deterministic. Reading one
> diagnostic carefully usually replaces five random edits.

---

## 8. Common mistakes

| Mistake | Symptom | Fix |
|---|---|---|
| Forgetting `#[cfg(test)]` | Test helpers ship in the binary | Put tests in a `#[cfg(test)] mod tests` |
| Forgetting `use super::*` | "not found in this scope" inside test module | Import the parent's names |
| Comparing custom types with `assert_eq!` | E0277 about `Debug`/`PartialEq` | Derive both traits |
| One global temp directory for all tests | Flaky failures under parallel runs | Unique names from pid + counter + nanos |
| Using production cwd in a test | Test writes into the repo | Inject a path or point the store at `TempDir` |
| Calling `SystemTime::now()` in tested logic | Time-dependent assertions | Inject a `Clock` |
| Expecting `--nocapture` to fix a failure | It only reveals output | Assert on values, not on printed text |

---

## 9. Apply it to Task Tracker

Questions and checklists only — write your answers, then implement in `11-feature-by-feature.md`.

- [ ] Which three behaviors will you test first, before any command handler exists?
- [ ] For each `Result`-returning function you plan, list the success value and each error variant
      you want to assert on.
- [ ] Which cases belong in a table-driven test (for example status words or bad IDs), and where
      will your temp-directory helper live?
- [ ] What exact timestamp will your fixed clock return, and what string do you expect from the
      time utility?
- [ ] Which modules need a fake store and which can be tested with the real codec alone?
- [ ] Write your three smoke-test scenarios (success, missing file, bad argument) with expected
      stdout, stderr, and exit code — no implementation code.

---

## 10. Manual CLI smoke test in a scratch directory

Automated tests cover logic; a smoke test proves the whole executable behaves from a real shell.
Use a directory outside the repository so you never confuse crate files with data files.

```bash
cargo build
mkdir -p /tmp/opencode/tt-smoke
cd /tmp/opencode/tt-smoke
cargo run --manifest-path /home/eliot/projects/code-labs/rust/starter-project/Cargo.toml -- list
echo "exit=$?"
cargo run --manifest-path /home/eliot/projects/code-labs/rust/starter-project/Cargo.toml -- frobnicate 2> err.txt
echo "exit=$?"
cat err.txt
ls -la
```

Check that `tasks.json` appears in the scratch directory, that stdout holds results only, that
stderr holds diagnostics, and that failure exit codes are non-zero. Delete the scratch directory
when the run is done. See `07-filesystem-and-cli.md` for the path and exit-code rules.

---

## Checkpoint

- [ ] I can explain `#[test]`, `#[cfg(test)]`, and the harness, and choose among `assert!`,
      `assert_eq!`, `assert_ne!`, and `matches!` for a claim.
- [ ] I can write a table-driven test with a struct array and assert an error variant *and* payload.
- [ ] I can build a unique temp dir with `std`, clean it with RAII, and explain why
      `process::exit` skips `Drop`.
- [ ] I can inject a clock and a store to make a test deterministic.
- [ ] I can reveal captured output, serialise tests, and run `cargo fmt --check` plus
      `cargo clippy --all-targets -- -D warnings` before finishing.
- [ ] I can decode E0382, E0502/E0503, E0597, E0277, E0432, and E0609, and use
      `rustc --explain` for the rest.

**Next:** `11-feature-by-feature.md` — implement the CLI one verified phase at a time.
