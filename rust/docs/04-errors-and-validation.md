# 04 — Errors and validation

This chapter is about failure as a first-class value. You will return errors instead of
panicking, give them structure with your own enum, send messages to the right stream exactly
once, and keep validation logic pure so you can test it without a filesystem.

## Why this matters for Task Tracker

The specification calls out bad arguments, non-numeric ids, unknown ids, missing files, and
invalid statuses. Each is a normal event in a CLI, not a crash. If you handle them with
`unwrap()`, the user sees a panic message and your program exits with code 101; if you print an
error and then also return `Err`, the user sees the same message twice. Rust gives you the
tools to do this properly with only the standard library: `Result`, `?`, `map_err`, `ok_or`,
and a hand-written error enum.

Everything here is std-only. Traits such as `Display` and `std::error::Error` are in the
standard library; the `thiserror`-style behavior described below is behavior you implement
yourself. See `03-structs-enums-and-match.md` for enums and `impl` blocks.

## What you will learn

- `Result<T, E>`, `?`, error conversion through `From`, and returning `Result` from `main`.
- Why `Box<dyn Error>` is convenient but throws away structure.
- How to write a custom error enum, `Display`, `Error`, `From`, and context-carrying variants
  by hand.
- `map_err`, `ok_or`, `ok_or_else`, and matching `std::io::ErrorKind::NotFound`.
- A discipline for `panic!`, `unwrap`, and `expect`.
- `std::process::exit` versus `std::process::ExitCode`, and what happens to destructors.
- stdout versus stderr, the print-once principle, and exit codes 0/1/2.
- Pure validation functions and friendly messages for parse failures.

---

## 1. `Result<T, E>` and `?`

`Result<T, E>` is an enum: `Ok(T)` means success, `Err(E)` means failure. A function that can
fail declares that in its return type, so the caller cannot ignore it.

```rust
#[derive(Debug)]
enum FetchError {
    Empty,
}

fn first_line(text: &str) -> Result<&str, FetchError> {
    text.lines().next().ok_or(FetchError::Empty)
}

fn headline(text: &str) -> Result<usize, FetchError> {
    let line = first_line(text)?;   // on Err, return early with that error
    Ok(line.len())
}

fn main() {
    println!("{:?}", headline("first line\nsecond"));
    println!("{:?}", headline(""));
}
```

Expected output:

```text
Ok(10)
Err(Empty)
```

The `?` operator unwraps `Ok` and returns `Err` from the current function. It only works in a
function whose return type can absorb that error. Without the right `From` implementation,
the compiler says so:

```rust
fn bad(text: &str) -> Result<i32, FetchError> {
    let n: i32 = text.trim().parse()?;   // error is ParseIntError
    Ok(n)
}
```

```text
error[E0277]: `?` couldn't convert the error to `FetchError`
 --> src/main.rs:12:37
  |
12 |     let n: i32 = text.trim().parse()?;
  |                                      ^ the trait `From<ParseIntError>`
  |                                        is not implemented for `FetchError`
```

You then have three choices: add `impl From<ParseIntError> for FetchError`, convert explicitly
with `map_err`, or change the error type. §4 shows the first.

## 2. Returning `Result` from `main`

`main` may return any type that implements `Termination`; `Result<(), E>` does, provided `E:
Debug`. Returning `Ok(())` means success.

```rust
fn main() -> Result<(), FetchError> {
    let n = headline("first line\nsecond")?;
    println!("{n}");
    Ok(())
}
```

Expected output on stdout and exit status:

```text
10
exit status: 0
```

If `headline` fails instead, the runtime prints `Error: Empty` to **stderr** using `Debug`
formatting and exits with status 1. That is why deriving `Debug` on your error type matters.
Forgetting it produces a `Termination` trait error pointing at the `Err` type.

## 3. `Box<dyn Error>`: convenient but erased

`dyn Error` is a **trait object**: a value whose concrete type is hidden behind the trait.
This pattern is common in examples:

```rust
use std::error::Error;

fn run() -> Result<(), Box<dyn Error>> {
    let n: i32 = "7".parse()?;
    println!("{n}");
    Ok(())
}

fn main() -> Result<(), Box<dyn Error>> {
    run()
}
```

`?` works because the standard library provides `From<E> for Box<dyn Error>` for error types
`E`. The cost is **type erasure**: a caller can no longer `match` on your variants, attach
different exit codes to different failures, or recover a path field without downcasting.
Use `Box<dyn Error>` for quick experiments; for a CLI with a known set of failure kinds,
prefer your own enum.

## 4. A custom error enum, written by hand

An error type is usually an enum whose variants describe failure **kinds** and carry
**context**. This is the behavior a derive macro would generate for you in other projects;
here you write it with std traits. Three pieces:

1. `Display` for the user-facing message (`{}`).
2. `Debug` for developer diagnostics (derive it).
3. `std::error::Error` as the marker, plus `source()` to expose the underlying cause.

```rust
use std::{error::Error, fmt, io, num::ParseIntError};

#[derive(Debug)]
enum CatalogError {
    Io { path: String, source: io::Error },
    BadId { text: String, source: ParseIntError },
    Missing,
}

impl fmt::Display for CatalogError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Io { path, .. } => write!(f, "could not read '{path}'"),
            Self::BadId { text, .. } => write!(f, "'{text}' is not a valid id"),
            Self::Missing => write!(f, "catalog entry not found"),
        }
    }
}

impl Error for CatalogError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        match self {
            Self::Io { source, .. } => Some(source),
            Self::BadId { source, .. } => Some(source),
            Self::Missing => None,
        }
    }
}

impl From<io::Error> for CatalogError {
    fn from(source: io::Error) -> Self {
        Self::Io { path: String::from("<unknown>"), source }
    }
}

impl From<ParseIntError> for CatalogError {
    fn from(source: ParseIntError) -> Self {
        Self::BadId { text: String::from("<unknown>"), source }
    }
}
```

Notes:

- `Error` requires `Debug + Display`. It has default methods; overriding `source()` is
  optional but useful when you keep an underlying error.
- `source()` returns `Option<&(dyn Error + 'static)>`; `None` means "no deeper cause".
- A blanket `From` cannot know *which path* or *which text* caused the failure, so its message
  is generic. When you have that context at the call site, build the variant yourself with
  `map_err` instead.
- `String` fields own their context. Borrowing `&str` in an error type forces you to add a
  lifetime parameter everywhere; owning is simpler for CLI errors.

> **Professional habit.** Write the error enum before the happy path. Each variant should
> answer: "what went wrong, and what does the user need to know to fix it?" If two variants
> always get the same message, merge them.

## 5. `map_err`, `ok_or`, and `ok_or_else`

| Tool | Input | Effect |
|---|---|---|
| `Result::map_err(f)` | `Result<T, E>` | converts `E` to another error type using `f` |
| `Option::ok_or(err)` | `Option<T>` | `Some(v)` → `Ok(v)`; `None` → `Err(err)` |
| `Option::ok_or_else(f)` | `Option<T>` | same, but builds the error only on `None` |

```rust
use std::fs;

fn read_catalog(path: &str) -> Result<String, CatalogError> {
    fs::read_to_string(path).map_err(|source| CatalogError::Io {
        path: path.to_string(),
        source,
    })
}
```

Here `map_err` preserves the `Ok(String)` and replaces only the error, adding the path that
the plain `io::Error` does not carry. `ok_or` is eager, so `ok_or(String::from("..."))`
allocates even on success; use `ok_or_else(|| ...)` for anything with a cost.

## 6. Matching `io::ErrorKind::NotFound`

Do not compare error *messages* — they are human text and can change. `io::Error::kind()`
returns an `ErrorKind` enum you can match. A **match guard** is the `if ...` after a pattern.

```rust
use std::{fs, io::ErrorKind};

fn read_or_default(path: &str) -> Result<String, CatalogError> {
    match fs::read_to_string(path) {
        Ok(text) => Ok(text),
        Err(e) if e.kind() == ErrorKind::NotFound => Ok(String::new()),
        Err(e) => Err(CatalogError::Io {
            path: path.into(),
            source: e,
        }),
    }
}
```

"Missing means start with an empty store" is a product decision, not a Rust rule. Make it
explicit in your notes and tests. Other likely kinds include `PermissionDenied` and
`InvalidData`; do not collapse them into `NotFound`.

## 7. Panic, `unwrap`, and `expect`: a discipline

| Tool | Meaning | Acceptable use |
|---|---|---|
| `panic!("...")` | Unwinds the stack and prints to stderr | Unreachable states, violated invariants, tests |
| `unwrap()` | Panics with a generic message | Scratch code and tests |
| `expect("why")` | Panics with your explanation | Tests; truly impossible states, with the reason written down |

A default Rust panic exits with status 101 and, because it unwinds, still runs destructors on
the way out (unless the profile is configured with `panic = "abort"`). Users should never see
a panic because they typed a bad id.

```rust
#[test]
fn parses_a_valid_id() {
    let id = parse_id(" 42 ").expect("test fixture is valid");
    assert_eq!(id, 42);
}
```

In that test (using the `parse_id` helper from §10), `expect` is a statement of confidence about
the *fixture*, not a replacement for validation. In production paths, return `Result` and let the caller decide. A useful
question before every `unwrap`: "what exact user input would make the compiler prove me
wrong?"

## 8. `process::exit` versus `ExitCode`

`std::process::exit(2)` stops the process immediately. **Destructors do not run**: buffered
writers are not flushed and `Drop` implementations are skipped.

```rust
std::process::exit(2);   // nothing after this line executes; no drops
```

`std::process::ExitCode` is returned from `main`. The function returns normally first, so
cleanup still happens. `ExitCode::SUCCESS` is 0 and `ExitCode::FAILURE` is 1; `ExitCode::from(u8)`
lets you choose a specific code.

```rust
use std::process::ExitCode;

fn run() -> Result<(), CatalogError> {
    // ... the actual work ...
    Ok(())
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::from(2)
        }
    }
}
```

This shape is what you want when different failures need different exit codes: keep the
message and the code decision together at the top level. `main` returning `Result` is simpler
but always exits 1 for any `Err`.

## 9. Print once, and print to the right stream

**stdout** carries the program's normal output: a list, a success line. **stderr** carries
diagnostics. Redirecting `> file` should not capture error messages.

**Print-once principle:** choose one reporter. Either `main` returns `Result` and the runtime
prints the error, or your code prints with `eprintln!` and returns `ExitCode`. Never both.

```rust
fn main() -> Result<(), FetchError> {
    let outcome = headline("");
    if let Err(e) = &outcome {
        eprintln!("error: {e}");   // printed here
    }
    outcome                        // runtime prints it again
}
```

```text
error: Empty
Error: Empty
```

Pick one and delete the other. Success messages belong on stdout and only for successful
operations.

| Exit code | Meaning |
|---|---|
| `0` | Success (`ExitCode::SUCCESS`) |
| `1` | General failure; default when `main` returns `Err`, also `ExitCode::FAILURE` |
| `2` | Command-line usage or validation problem (a widespread convention you can adopt) |
| `101` | Default Rust panic status — not a user-facing error contract |

## 10. Validation as pure functions

A **pure function** gives the same output for the same input, performs no I/O, and mutates
nothing. Validation is a good fit: turn untrusted text into a checked value or a clear error,
without touching files or the clock.

```rust
use std::num::ParseIntError;

fn parse_id(text: &str) -> Result<i32, ParseIntError> {
    text.trim().parse::<i32>()
}

fn is_blank(text: &str) -> bool {
    text.trim().is_empty()
}

fn is_allowed_stage_word(word: &str) -> bool {
    matches!(word, "planned" | "running" | "finished")
}
```

`trim()` accepts harmless surrounding whitespace in typed input; an empty string still fails
to parse, and `is_blank` catches it before you store it. The stage words above are a generic
example — your CLI's accepted words are your decision, and `03-structs-enums-and-match.md`
asks where the string mapping should live.

Validate at the **edge** (arguments, file contents), then trust the interior: once a value is
in your own type, callers should not have to re-check it.

## 11. Translating parse errors into user messages

`ParseIntError` is precise but cryptic. Translate it at the boundary, including the offending
text:

```rust
fn id_or_message(text: &str) -> Result<i32, String> {
    parse_id(text).map_err(|_| {
        format!("'{text}' is not a valid id; expected an integer")
    })
}
```

```text
id_or_message(" 42 ") -> Ok(42)
id_or_message("oops") -> Err("'oops' is not a valid id; expected an integer")
```

Using `String` as the error here keeps the example small. In the real CLI, put that message in
a variant of your error enum so callers can match on the kind while still showing the text.

---

## Common mistakes

- **`?` in a function returning `()` or the wrong error type.** The compiler must be able to
  convert the error; use `map_err` or add `From`.
- **Dropping context.** `io::Error` alone does not say *which file*; add it to your variant.
- **Printing and returning `Err`.** Pick stdout/stderr and one reporter (§9).
- **Reaching for `unwrap` on user input.** That is a panic, not an error message.
- **Using `process::exit` after buffered writes.** Destructors are skipped; prefer `ExitCode`.
- **Matching error strings instead of `ErrorKind`, or using eager `ok_or(String::from(..))`.**
  Text is not stable, and the allocation happens even on success; use `ok_or_else`.
- **`Error` without `Display`/`Debug`, or panic where `ExitCode` would do.** Reserve panic 101
  for bugs, not bad arguments.

> **Professional habit.** Keep an "error contract" in your journal: one table listing each
> failure kind, its user-facing message, and its exit code. Update it whenever you add a
> variant. It prevents both duplicate messages and silent failures.

## Apply it to Task Tracker

Questions only — no solution code here.

1. For each command (`add`, `update`, `delete`, the `mark-*` commands, `list`), list every
   way it can fail before you design error variants.
2. Which of your failures need context (a path, an id, a raw status word)? What field will
   hold it — owned `String` or a borrow with a lifetime?
3. Which `From` implementations do you actually need for `?`, and where will `map_err` be
   clearer than a blanket conversion?
4. Will `main` return `Result` or `ExitCode`? Where exactly is each message printed, and how
   do you guarantee it is printed once?
5. Which failures deserve exit code 2 (usage/validation) and which deserve 1 (I/O or corrupt
   data)? Where is that decision made?
6. A missing file might mean "create a fresh store" or "fatal error". Which do you choose,
   and how will you test that choice without depending on a particular machine state?
7. Name three places you were tempted to use `unwrap`/`expect`. What is the user-facing
   alternative for each?
8. Which validation functions can be pure? How would you test them with no files and no clock?

## Checkpoint

- [ ] I can write a function returning `Result<T, E>`, call it with `?`, and explain the `From`
      requirement when that fails.
- [ ] I can write an error enum with `Display`, `Debug`, `Error::source`, and `From`, and say
      when `map_err` beats `From`.
- [ ] I can distinguish `NotFound` from other I/O failures using `ErrorKind`.
- [ ] I can explain why `Box<dyn Error>` loses structure.
- [ ] I can state the print-once rule, pick the right stream, and explain why `ExitCode` runs
      destructors while `process::exit` does not.
- [ ] I can write pure validation helpers and translate a `ParseIntError` into a helpful line.

Next: `05-collections-and-strings.md`
