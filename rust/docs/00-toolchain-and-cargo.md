# 00 — Toolchain, Cargo, and the Offline Workspace

## Why this matters for Task Tracker

You will spend more time talking to Cargo and the compiler than typing application logic. If you can
read a diagnostic, find the answer in local documentation, and keep a clean, dependency-free crate,
the rest of the learning path is manageable. If you cannot, every chapter becomes a guessing game.

This chapter sets up the workspace you will use for the entire project. It does **not** implement any
Task Tracker behavior.

## What you will learn

- What `rustup`, `rustc`, `cargo`, `rustfmt`, and `clippy` each do
- What a **toolchain** (installed compiler plus tools) and a **component** (one installable piece of
  a toolchain) are
- What a **crate** (a unit of compilation) and a **package** (a directory with `Cargo.toml`) are
- How to read `Cargo.toml`, `Cargo.lock`, and `target/`
- Why `[dependencies]` must stay empty and how to verify that it did
- Which offline documentation commands replace web searches
- How to read an error code, span, help message, and note
- How to keep disposable experiments in `/tmp/opencode/rust-scratch`
- The git habits that keep this shared workspace tidy
- A concrete first-20-minutes routine

---

## 1. The toolchain

**Toolchain** means the coordinated set of programs that builds Rust code: the compiler `rustc`, the
standard library, the build tool `cargo`, and supporting tools. **rustup** is the toolchain manager:
it installs and selects toolchains and their components. A **component** is one installable part of a
toolchain, such as the compiler, the standard library source, or the formatter.

Check what you have. All of these work offline:

```bash
rustup show                    # active toolchain and its components
rustup toolchain list          # installed toolchains
rustup component list --installed
rustc --version                # rustc 1.99.1 (...)
cargo --version                # cargo 1.99.1 (...)
rustfmt --version
cargo clippy --version
rustc --print sysroot          # filesystem root of the active toolchain
```

`rustup --version` reports the version of rustup itself, not the compiler; the informational lines it
prints tell you which `rustc` is active. The project targets **Rust 1.99.1, edition 2024**.

> **Professional habit.** Start a work session with `rustup show` and `cargo --version`. When a
> diagnostic looks unfamiliar, knowing the exact version prevents you from chasing advice written for
> a different release.

---

## 2. `rustc` versus `cargo`

`rustc` compiles one crate. It is ideal for a one-file experiment or for seeing a raw diagnostic.
`cargo` manages a package: it finds the crate roots, passes the right flags, tracks dependencies,
runs tests, formats code, and runs lints.

A standalone experiment:

```rust
// note.rs
fn main() {
    let note = "remember milk";
    println!("{note}");
}
```

```bash
rustc --edition 2024 note.rs -o note
./note
```

```text
remember milk
```

The same experiment inside a Cargo package is `cargo run`, with no explicit compiler flags and no
output binary name to remember.

**Rule of thumb:** use `cargo` for every project action; use `rustc` only for throwaway single files
and for `rustc --explain` (section 8). Cargo is not an extra layer of magic — it calls `rustc` for
you with sensible arguments.

---

## 3. Crates and packages

**Crate** is the unit the compiler works on. Every crate has a **crate root**, the file where its top
level begins. **Package** is a directory containing a `Cargo.toml` and one or more crates.

| Crate root | Produces | Present here? |
|---|---|---|
| `src/main.rs` | a binary crate (an executable with `fn main`) | yes, in `starter-project/` |
| `src/lib.rs` | a library crate (reusable items, no `main`) | not yet; you may add modules under `src/` later |
| `src/bin/*.rs` | additional binary crates in the same package | not needed for this CLI |

**Edition** is a language version that opts a crate into a set of syntax and prelude changes.
Edition 2024 is the current edition and is already selected in the starter crate. Editions are not
compiler versions: you can compile a 2024 crate with newer compilers, but changing edition mid-project
can change how existing code is parsed. Do not change it without a reason.

---

## 4. `Cargo.toml`, `Cargo.lock`, and `target/`

This is the entire manifest of the starter crate:

```toml
[package]
name = "starter-project"
version = "0.1.0"
edition = "2024"

[dependencies]
```

| Field / file | Meaning | What you do with it |
|---|---|---|
| `[package]` | table of metadata for the package | keep it at the top |
| `name` | package name; also the default binary name | may become `task-cli` if you choose deliberately |
| `version = "0.1.0"` | semantic version: major.minor.patch | before `1.0`, the minor number may still break compatibility |
| `edition = "2024"` | language edition | leave it at 2024 |
| `[dependencies]` | external crates to fetch and compile | keep it empty |
| `Cargo.lock` | exact resolved versions, including **transitive dependencies** (dependencies of dependencies) | let Cargo maintain it; commit it for an application |
| `target/` | generated build artifacts: `debug/`, `release/`, incremental data, and compiled dependencies | never edit by hand; never commit |

`target/` can be large and can always be regenerated. Delete it with `cargo clean`, not with a manual
recursive removal if you want Cargo's bookkeeping to stay consistent.

If `[dependencies]` stays empty, `Cargo.lock` should contain only your own package. That is one of
the acceptance checks in `LEARNING_PATH.md`.

---

## 5. Why the CLI track has no dependencies

The required CLI constraint is **standard library only**. That means no `serde`, `serde_json`,
`chrono`, `tempfile`, `anyhow`, `thiserror`, `clap`, or any other crate — not even "just for tests"
— while you are building chapters `00`–`13`. The optional API track in chapters `14`–`16` changes
that deliberately and explains each crate it adds.

Consequences you should accept up front:

1. **JSON is yours to write.** `06-json-without-crates.md` teaches the reader and writer design.
2. **UTC date formatting is yours to write.** `08-time-without-chrono.md` teaches the epoch
   conversion algorithm.
3. **Argument parsing is yours to write.** `std::env::args` and `match` are enough for positional
   commands; see `07-filesystem-and-cli.md`.
4. **Tests use std only.** Temporary directories and fixtures are made by hand; see
   `10-testing-and-debugging.md`.

How to keep the manifest honest:

```bash
cat Cargo.toml          # [dependencies] must be empty during the CLI track
cat Cargo.lock          # only your own package should appear during the CLI track
cargo tree              # no external names should show up until chapter 15
```

A "temporary" dependency tends to become permanent. If a task feels impossible without a crate, that
is the moment to read the relevant chapter, not to add a line to `[dependencies]`.

> **Professional habit.** Treat the empty `[dependencies]` section as an architectural constraint,
> not an inconvenience. It guarantees offline builds, a tiny lockfile, and that you understand every
> line of code in the binary.

---

## 6. Cargo commands you will use daily

Run these from the package directory (for example `starter-project/`).

| Command | What it does | When |
|---|---|---|
| `cargo new --edition 2024 NAME` | creates a new binary package | scratch experiments |
| `cargo build` | compiles debug artifacts into `target/debug/` | when you need the binary |
| `cargo check` | type-checks without producing a binary | after every small edit; fastest feedback |
| `cargo run` | builds and runs the binary | smoke tests |
| `cargo test` | builds and runs unit, integration, and doc tests | after every feature phase |
| `cargo fmt` | rewrites source with standard formatting | before each commit |
| `cargo fmt --check` | reports formatting differences without writing | verification |
| `cargo clippy --all-targets -- -D warnings` | lints all targets, treating warnings as errors | verification |
| `cargo clean` | removes `target/` | when artifacts are stale or disk is tight |

The `--` separator divides Cargo's arguments from arguments passed to the tool or the program. For
example, `cargo run -- add "Buy groceries"` passes `add` and `Buy groceries` to the binary, not to
Cargo. `-D warnings` means "deny warnings": a lint finding fails the command.

`cargo check` is your loop inner tool. It catches type and borrow errors much faster than a full
build.

---

## 7. The offline reference shelf

These commands open locally installed documentation; no network is needed.

```text
rustup doc --std          # standard library API docs
rustup doc --book         # "The Rust Programming Language"
rustup doc --reference    # syntax and language semantics
rustup doc --cargo        # the Cargo book
rustc --print sysroot     # locate installed source and docs on disk
rustc --explain E0308     # long explanation of a diagnostic code
cargo help <command>      # e.g. cargo help test
cargo <command> --help    # e.g. cargo clippy --help
```

The sysroot contains generated documentation and the standard library source under a path like
`lib/rustlib/src/rust/library`. Reading the source of `String`, `Option`, or `Result` is a legitimate
way to answer "what exactly does this method do?".

There is also a built-in Book search and index in the generated docs. Use it instead of a web search
when you forget syntax. `13-troubleshooting-and-cheatsheet.md` collects the lookups you will repeat.

---

## 8. Reading a compiler error

Rust diagnostics contain four useful parts:

1. **Error code**, such as `E0308`: a stable identifier you can explain offline.
2. **Span**: a file, line, column, and caret showing the exact expression at fault.
3. **Help**: a concrete change the compiler can often suggest.
4. **Notes**: extra context, such as where a type was inferred or which trait is missing.

A type mismatch, entered in a scratch crate:

```rust
fn main() {
    let count: u32 = "3";
}
```

Representative output (wording can vary slightly by compiler version):

```text
error[E0308]: mismatched types
 --> src/main.rs:2:22
  |
2 |     let count: u32 = "3";
  |                ---   ^^^ expected `u32`, found `&str`
  |                |
  |                expected due to this

error: aborting due to 1 previous error

For more information about this error, try `rustc --explain E0308`.
```

The annotation says the type was *declared* `u32` and the value is a string slice (`&str`). The fix is
to parse the string into a number or to change the declared type — not to add a cast and hope. The
`E0277` example in `01-rust-basics.md` shows the `help:` and `= note:` parts of a diagnostic.

Reading routine:

1. Read only the **first** error; later errors are often consequences of it.
2. Locate the span and state out loud what the compiler expected versus what it found.
3. Read every `help:` and `note:` line, including secondary spans.
4. If the code is unfamiliar, run `rustc --explain E0308`.
5. Make one change, then re-run `cargo check`. Do not stack three guesses.

> **Professional habit.** Copy an error you cannot fix into your build journal with the code, the
> span, and your eventual fix. After a week, patterns appear, and `13-troubleshooting-and-cheatsheet.md`
> becomes your own document.

---

## 9. Scratch space: `/tmp/opencode/rust-scratch`

Use a disposable directory outside the repository for experiments. Nothing there can pollute git
status, and losing it costs nothing.

```bash
mkdir -p /tmp/opencode/rust-scratch
cd /tmp/opencode/rust-scratch
cargo new --vcs none --edition 2024 note-lab
cd note-lab
cargo run
```

`--vcs none` avoids nesting a git repository inside your workspace. `--edition 2024` makes the scratch
crate match the real project. Every chapter in this path expects you to retype its short snippets
into a scratch crate, predict the result, then run them.

Keep the real crate (`starter-project/`) for actual Task Tracker work. Scratch is where you break
things deliberately.

---

## 10. Git basics for this workspace

The git repository root is `/home/eliot/projects/code-labs`, so `rust/` is a subdirectory of a larger
workspace. Other directories may have unrelated changes; never stage the whole tree blindly.

| Command | Purpose |
|---|---|
| `git status --short` | see modified (`M`), deleted (`D`), and untracked (`??`) paths |
| `git diff -- PATH` | inspect unstaged changes to one path |
| `git add PATH` | stage only that path |
| `git diff --cached --stat` | review exactly what is staged |
| `git commit -m "..."` | record the staged snapshot |
| `git log --oneline -5` | see recent commits |

Practical rules here:

- Stage explicit paths such as `rust/docs/` or `rust/starter-project/`, not `.`.
- Never commit `target/`, compiled binaries, or `/tmp/opencode/rust-scratch`.
- Add `target/` to a `.gitignore` at the appropriate level if you create one; do not edit unrelated
  ignore files without asking.
- Run `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings`, and `cargo test` before a
  commit that touches Rust code.

> **Professional habit.** Before every commit, compare `git status --short` with what you intended to
> change. A clean diff is a review you can actually perform.

---

## 11. Repo layout and path resolution

```text
rust/
├── learnrust/            # loose rustc exercise, not a Cargo package
│   ├── main.rs           # formatting experiments; contains a typo
│   ├── main              # an old compiled binary — ignore it
│   └── Cargo.lock        # stale leftover; no Cargo.toml here
├── starter-project/      # real Cargo package, edition 2024
│   ├── Cargo.lock
│   ├── Cargo.toml
│   └── src/main.rs
└── docs/                 # this learning path and its chapters
```

`learnrust/main.rs` contains `printl!` instead of `println!`, so it does not compile. Treat the whole
directory as scratch. Start from `starter-project/`.

A subtle but important point: `tasks.json` is a relative path. It resolves against the **shell's
current working directory**, not the crate directory. Running `cargo run` from `starter-project/`
writes into `starter-project/`; running it from a scratch directory writes there. Choose your smoke-test
directory deliberately, and record the choice in your journal.

---

## 12. First 20 minutes: hands-on sequence

Do these in order, in your own scratch crate. Type the commands; do not just read them.

1. Inspect the environment.
   ```bash
   rustup show
   rustc --version
   rustc --print sysroot
   ```
2. Create the scratch crate.
   ```bash
   mkdir -p /tmp/opencode/rust-scratch
   cd /tmp/opencode/rust-scratch
   cargo new --vcs none --edition 2024 note-lab
   cd note-lab
   ```
3. Read the generated manifest; confirm the same fields as `starter-project/Cargo.toml`.
   ```bash
   cat Cargo.toml
   ```
4. Run the generated program and observe `target/` appearing.
   ```bash
   cargo run
   ls -a
   ```
5. Open `src/main.rs`, add `let count: u32 = "3";` inside `main`, then run `cargo check`.
   Read the whole `E0308` diagnostic before changing anything.
6. Ask the compiler for the long explanation.
   ```bash
   rustc --explain E0308
   ```
7. Fix the line by parsing or by changing the declared type, then re-run `cargo check`.
8. Format, lint, and test the (still trivial) crate.
   ```bash
   cargo fmt --check
   cargo clippy --all-targets -- -D warnings
   cargo test
   ```
9. Open the local Book for the chapters on variables and types.
   ```bash
   rustup doc --book
   ```
10. Return to the repository root and look at git state without changing it.
    ```bash
    git status --short
    ```

You now have the whole workflow: edit, `cargo check`, read diagnostics, offline docs, format, lint,
test, inspect git state.

---

## 13. Common mistakes

| Mistake | Why it hurts | Correct habit |
|---|---|---|
| Editing `learnrust/main.rs` | It is scratch with a deliberate typo and no manifest | Use `starter-project/` or a scratch crate |
| Adding "just one" helper crate | Violates the constraint and hides logic from you | Read the relevant chapter instead |
| Running `rustup --version` to check the compiler | Reports rustup, not `rustc` | Use `rustc --version` |
| Fixing the last error in a long diagnostic list | Later errors are often cascading | Fix the first error, then re-check |
| Committing `target/` or a compiled binary | Large, machine-specific noise | `cargo clean`; ignore build output |
| Searching the web for syntax | Slower and often version-mismatched | `rustup doc`, `rustc --explain`, `cargo help` |
| Switching edition mid-project | Can change parsing and prelude behavior | Keep `edition = "2024"` |
| Running `cargo run` from an unknown directory | Relative paths land somewhere unexpected | `pwd` first; use a deliberate scratch directory |

---

## Apply it to Task Tracker

No code here — these are decisions to make and questions to answer before the next chapter.

- Which directory will be your real crate, and will you rename the package? Where will you record that
  decision?
- Where will the binary's entry point live, and which future modules will be library-like?
- What does an empty `[dependencies]` section guarantee about `Cargo.lock` and offline builds?
- Where will you put disposable experiments so that `git status --short` stays readable?
- When you run smoke tests, what will `pwd` be, and therefore where will `tasks.json` be created?
- Which three commands will you run before every commit?
- What will you write in your build journal the first time a compiler error confuses you?
- If you are tempted to add a crate for JSON or dates, which chapter do you open instead?

---

## Checkpoint

1. Explain the difference between `rustup`, `rustc`, and `cargo` in one sentence each.
2. What is the difference between a crate and a package?
3. What does `edition = "2024"` control, and why should you not change it casually?
4. What should `Cargo.lock` contain when `[dependencies]` is empty?
5. Which Cargo command gives the fastest feedback after a small edit, and why?
6. Name the four parts of a compiler diagnostic and the offline command for a long explanation of an
   error code.
7. Where do you create scratch crates, and which two flags do you pass to `cargo new`?
8. Why must you stage explicit paths rather than `git add .` in this workspace?
9. If `cargo run` creates `tasks.json`, which directory decides where the file lands?

## Next

Continue with `01-rust-basics.md` for bindings, types, functions, expressions, control flow, and
printing.
