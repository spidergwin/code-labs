# Troubleshooting and Cheat Sheet

> **Purpose.** A practical, offline reference for the moments when the compiler disagrees with you
> or a syntax detail slips your mind. Keep it open while following
> `11-feature-by-feature.md`. Every snippet below illustrates isolated Rust syntax or a generic
> shape; none of it is a Task Tracker implementation.

## Why this matters for Task Tracker

Rust's errors are precise but dense, and a std-only CLI uses the awkward corners of the language:
byte-level JSON parsing, `Result` propagation, borrowing across loops, and custom error types.
Knowing the vocabulary of an error message (`moved`, `borrowed`, `trait bound`, `out of scope`)
turns a ten-minute fight into a two-minute fix. This chapter also freezes the small facts—format
strings, iterator adapters, Cargo commands—that are easy to forget between sessions.

## What you will learn

- Run the right Cargo and offline-documentation command for each situation.
- Recall core syntax shapes: bindings, functions, control flow, types, traits, modules, and iterators.
- Decode the ten compiler errors you are most likely to meet in this project.
- Separate compile-time errors from runtime panics and logic bugs.
- Recognize JSON, time, `Option`, and `Result` pitfalls before they become test failures.
- Work through a repeatable debugging checklist instead of guessing.

## 1. Command reference

Run Cargo commands from the crate directory (`starter-project/`) unless noted.

| Command | Purpose |
|---|---|
| `cargo new name` | Create a new binary crate skeleton (use for scratch experiments) |
| `cargo check` | Type-check without producing a binary; fastest feedback loop |
| `cargo build` | Build debug binary into `target/debug/` |
| `cargo build --release` | Build optimized binary into `target/release/` |
| `cargo run -- add "Buy groceries"` | Build and run, passing arguments after `--` |
| `cargo test` | Run unit, integration, and doc tests |
| `cargo test json::` | Run only tests whose path contains `json::` |
| `cargo fmt` / `cargo fmt --check` | Format source / verify formatting without changing it |
| `cargo clippy --all-targets -- -D warnings` | Lint all targets, treating warnings as errors |
| `cargo clean` | Remove `target/` and rebuild from scratch |
| `rustup doc --std` | Open standard-library API documentation |
| `rustup doc --book` | Open *The Rust Programming Language* |
| `rustup doc --reference` | Open the language reference |
| `rustup doc --cargo` | Open the Cargo book |
| `rustc --print sysroot` | Print toolchain root; std source is under `lib/rustlib/src/rust/library` |
| `cargo help test` | Explain a Cargo subcommand |

For scratch smoke tests, build once and execute the binary from a temporary directory so relative
paths resolve there; see the scratch checklist in `11-feature-by-feature.md`.

## 2. Syntax cheat sheet

### Bindings, constants, functions

```rust
let x = 1;              // immutable binding
let mut y = 2;          // mutable binding
const LIMIT: usize = 5; // compile-time constant, no storage address concept
static LABEL: &str = "hi"; // one static value for the whole program

fn add(a: i32, b: i32) -> i32 { a + b } // expression body: no `return` needed
```

Types are written `name: Type`. Integers default to `i32`; use `u32`/`u64`/`usize` deliberately.
`usize` is for sizes and indices. A trailing semicolon turns an expression into a statement and
therefore makes the block return `()`.

### Control flow as expressions

```rust
let label = if n > 0 { "positive" } else { "other" }; // both arms same type
let value = loop { if done { break 42; } };            // `break value` returns from loop
while condition { /* ... */ }
for item in collection { /* ... */ }
```

`if` conditions must be `bool`; there is no truthiness. `loop` is infinite until `break`, `while`
checks before each pass, and `for` consumes an **iterator**. Use `for i in 0..10` for a range and
`0..=10` when the end is included.

### `match`, structs, enums, `impl`

```rust
match status {
    "todo" => 0,
    "done" => 2,
    other => 1, // catch-all; put it last
}

struct Point { x: i32, y: i32 }
enum Color { Red, Green, Blue }

impl Point {
    fn origin() -> Self { Self { x: 0, y: 0 } }
}
```

`match` must cover every possibility; that is its power. An enum variant can carry data:
`Message::Text(String)`. `Self` inside `impl` means the type being implemented. Associated
functions like `origin()` have no `self` parameter.

### Traits and generics

```rust
trait Describe { fn describe(&self) -> String; }

fn first<T: Clone>(items: &[T]) -> Option<T> { items.first().cloned() }
fn second<T>(items: &[T]) -> Option<&T> where T: Clone { items.first() }
```

A **trait** is a set of methods a type promises to implement; it is Rust's version of an interface.
A **generic** is a type parameter: `T` stands for a caller-chosen type. `T: Clone` is a **trait
bound** restricting which types are allowed. `where` clauses are the same idea written separately.

### `Option`, `Result`, and `?`

```rust
let a: Option<i32> = Some(1);
let b: Option<i32> = None;
let r: Result<i32, String> = Ok(1);

fn combine() -> Result<i32, String> {
    let n = "12".parse::<i32>().map_err(|e| e.to_string())?;
    Ok(n + 1)
}
```

`Option<T>` means "possibly absent"; `Result<T, E>` means "success value or error". The `?`
operator, valid only in functions returning a compatible `Result`/`Option`, unwraps success or
returns early with the failure. See `04-errors-and-validation.md`.

### References, collections, iterators, modules

```rust
let s = String::from("hello");
let r: &str = &s;        // shared borrow, read only
let n = s.len();         // borrowing does not move `s`

let mut v = vec![1, 2];
v.push(3);
let total: i32 = v.iter().map(|x| x * 2).sum();
let words: Vec<&str> = "a b".split_whitespace().collect();

mod store;                // declare a module from another file
use std::collections::BTreeMap;
pub fn visible() {}       // public outside the module
```

`&mut T` is an exclusive borrow, allowing mutation. `Vec<T>` is a growable list; `String` is an
owned UTF-8 text buffer; `&str` is a borrowed slice of text. `iter()` borrows, `into_iter()` owns,
and `iter_mut()` allows mutation. Adapters such as `map`, `filter`, and `take` are lazy until a
consumer like `collect`, `sum`, or `for` drives them. `pub` marks public items; sibling private
items are visible within the crate's module tree as governed by `mod`/`use`.

## 3. Format string reminders

| Specifier | Meaning | Example output |
|---|---|---|
| `{}` | Display formatting via `Display` | `7`, `hello` |
| `{:?}` | Debug formatting via `Debug` | `Point { x: 1, y: 2 }` |
| `{name}` | Named argument | `println!("{name} done", name = "task")` |
| `{:04}` | Zero-pad to width 4 | `0007` |
| `{:02}` | Zero-pad to width 2 | `07` |

```text
"{}"    plain value      "{:?}"  diagnostic view
"{:02}" month/day/hour   "{:04}" year in a UTC timestamp
```

`format!` builds a `String`, `println!` prints to stdout, and `eprintln!` prints to stderr.
Add `#[derive(Debug)]` on your own types to use `{:?}`. For the UTC layout in
`08-time-without-chrono.md`, combine literal separators with `{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z`.

## 4. Ownership quick rules

1. Every value has one owner; when the owner goes out of scope, the value is dropped.
2. Assigning or passing an owned `String`/`Vec` by value **moves** it; the old name is then unusable.
3. Pass `&T` for reads and `&mut T` for writes; you can have many shared borrows **or** one mutable borrow, never both at once.
4. Borrows must not outlive the value they point to.
5. To keep data after the call, return an owned value or accept ownership.
6. `.clone()` makes a deep copy: correct when you need two owners, wasteful in a hot loop.
7. `Copy` types (integers, `bool`, `char`) duplicate implicitly instead of moving.

## 5. Common compiler errors

| Code | Typical message hint | Cause | Fix |
|---|---|---|---|
| `E0382` | use of moved value | A `String`/`Vec` was passed by value, then used again | Pass `&value`, or clone only if two owners are truly needed |
| `E0502` | cannot borrow as mutable because also borrowed as immutable | A shared borrow is alive while you mutate the same value | End the first borrow (use its result, or put it in a smaller scope) |
| `E0503` | cannot use because it was mutably borrowed | You read a value while a `&mut` to it is active | Shorten the mutable borrow's scope |
| `E0499` | cannot borrow as mutable more than once at a time | Two overlapping `&mut` borrows, often from a loop or iterator | Restructure the loop, use indices carefully, or collect first |
| `E0597` | does not live long enough | A reference outlives the local it points to | Return an owned `String` instead of a borrow, or extend the owner's scope |
| `E0277` | trait bound not satisfied | Calling `.clone()`, `.collect()`, or `?` on a type that lacks the needed trait | Add the bound, convert the type, or implement the trait |
| `E0432` | unresolved import | Wrong module path or missing `mod` declaration | Check the module tree, `use` path, and `pub` visibility |
| `E0609` | no field on type of kind | Field name misspelled or type is not the one you think | Check the type with a deliberate type error or read the struct definition |
| `E0308` | mismatched types | `u32` vs `usize`, `&str` vs `String`, `Option` vs raw value | Parse/convert explicitly: `.parse()`, `.as_str()`, `.to_string()` |
| `E0004` | non-exhaustive patterns | `match` does not cover every variant/value | Add all variants, or a deliberate catch-all arm |

Read the compiler's span underlines: they show exactly where each conflicting borrow begins and
ends. Error codes also have offline explanations in `rustup doc --reference` and in the compiler's
own help output.

## 6. Runtime failures and panics

| Symptom | Likely cause | What to change |
|---|---|---|
| `called Result::unwrap() on an Err value` | `unwrap`/`expect` on bad input | Return `Result` with context; print once at `main` |
| `index out of bounds` | Using a raw index after filtering/removing | Iterate with `get`, or use `iter()` and `enumerate()` safely |
| `byte index is not a char boundary` | Slicing a UTF-8 `String` at an arbitrary byte | Iterate `char_indices()` or operate on whole characters |
| `attempt to multiply with overflow` | Arithmetic on unchecked integers in debug builds | Use checked arithmetic where overflow is plausible |
| Program exits `101` | A panic reached the top level | Find the panic site in the message; convert to a handled error |
| Wrong exit code but correct output | Printing success before validating failure | Validate first, then print, then return the exit status |

A panic is not an error-handling strategy: it terminates the program and prints implementation
details. Handled errors produce one friendly line and the exit codes in
`11-feature-by-feature.md`.

## 7. JSON pitfalls

| Pitfall | Why it breaks | Prevention |
|---|---|---|
| Unescaped quote or backslash inside a description | Produces invalid JSON and desynchronizes the parser | Escape on write, unescape on read; test both directions |
| Newline/tab written literally inside a string | JSON strings cannot contain raw control characters | Emit `\n`, `\t`, `\r` escapes |
| Number spellings that are not valid IDs: `01` (invalid JSON), `1.0`, `1e3`, `-1` | `1.0` and `1e3` are legal JSON but not plain integers; `-1` is not a positive ID | Accept the JSON grammar in the decoder, then validate the ID at the model boundary |
| Trailing comma after the last element | Not legal JSON | Track whether you are writing the first item; never add a comma after the last |
| Duplicate keys | Readers may disagree on which wins | Reject duplicates while parsing, or define last-wins and document it |
| Invalid UTF-8 or a BOM | `read_to_string` fails or the first string has an odd character | Read as UTF-8, reject BOM explicitly, test a BOM fixture |
| Hand-written file with extra fields | Your schema requires exactly five keys | Decide: reject unknown keys or ignore them; be consistent |

Round-trip tests (encode → decode → compare) catch most of these. More grammar detail is in
`06-json-without-crates.md`.

## 8. Time pitfalls

| Pitfall | Consequence | Prevention |
|---|---|---|
| Using local time | Timestamp changes when the machine's timezone changes | Use UTC epoch seconds throughout |
| Reading the clock twice in `add` | `createdAt` and `updatedAt` differ | Capture one instant and format it once |
| Wrong leap-year rule | February 29 missing or extra | Year divisible by 4, except centuries unless divisible by 400 |
| Forgetting zero padding | `2026-1-2T9:5:3Z` fails the schema | Use `{:02}` for all but the year |
| Storing seconds in `i32` | Year-2038 overflow on narrow platforms | Use `i64` for epoch seconds |
| Tests using the real clock | Flaky tests at midnight or on slow machines | Inject a fixed instant |
| Accepting impossible dates on load | Corrupt file silently produces nonsense | Validate month/day/hour ranges after parsing |

The full algorithm and reasoning are in `08-time-without-chrono.md`.

## 9. `Option` / `Result` idiom table

| Tool | Use it when | Generic shape |
|---|---|---|
| `map` | Transform the success/inner value, keep the wrapper | `option.map(f)`, `result.map(f)` |
| `and_then` | Transform to another wrapper, avoiding nesting | `option.and_then(f)` |
| `unwrap_or` | Supply a plain default | `option.unwrap_or(0)` |
| `unwrap_or_else` | Default is expensive or needs context | `option.unwrap_or_else(|| compute())` |
| `ok_or` | Turn `Option` into `Result` with an error value | `item.ok_or(AppError::NotFound)` |
| `ok_or_else` | Build the error only on failure | `item.ok_or_else(|| AppError::NotFound(id))` |
| `?` | Propagate failure to a compatible caller | `let value = operation()?;` |
| `if let` | Act on one variant without handling all | `if let Some(x) = option { ... }` |
| `match` | Handle every variant explicitly | `match result { Ok(v) => ..., Err(e) => ... }` |

Prefer `?` for propagation and `match`/`if let` where the difference matters. Avoid
`unwrap`/`expect` outside tests.

### Common mistakes

- Changing code before reading the entire compiler message, including the "help:" section.
- Fixing an ownership error with `.clone()` everywhere instead of restructuring a borrow.
- Assuming a flaky test is "just timing"; look for hash order, shared files, or a real clock.
- Reaching for a crate to replace a std-only utility you already wrote and tested.

> **Professional habit.** Keep a short error journal: error code, what the compiler meant, and the
> fix. After a few weeks, most of your errors become two-minute fixes.

### Apply it to Task Tracker

1. Which error in section 5 have you seen most often in your own code, and what pattern triggers it?
2. Where could an unescaped quote in a description break your JSON writer?
3. Which of your tests currently depend on the real clock or on file order?

## 10. Glossary (43 terms)

| Term | Meaning |
|---|---|
| crate | A Cargo package or library; the unit Rust compiles |
| module | A namespace grouping items, usually one file or an inline `mod` block |
| `Cargo.toml` | Manifest declaring package metadata and dependencies |
| `Cargo.lock` | File pinning exact resolved dependency versions |
| edition | Language version chosen per crate; this project uses 2024 |
| binding | A name bound to a value with `let` |
| shadowing | Declaring a new `let` with an existing name, producing a new binding |
| move | Transferring ownership so the previous name can no longer be used |
| borrow | Accessing a value through a reference without taking ownership |
| mutable borrow | Exclusive `&mut` reference allowing mutation |
| lifetime | The scope for which a reference remains valid |
| lifetime elision | Compiler rules that infer lifetimes when they are unambiguous |
| `Copy` | Trait for types duplicated implicitly on assignment |
| `Clone` | Trait for explicitly duplicating a value, often allocating |
| slice | A borrowed view into a contiguous sequence, e.g. `&[u8]`, `&str` |
| `String` | Owned, growable UTF-8 text |
| `&str` | Borrowed UTF-8 string slice |
| `Vec<T>` | Owned, growable contiguous list |
| iterator | A lazy sequence producer consumed by `for` or adapters |
| adapter | Iterator transformer such as `map`, `filter`, `enumerate` |
| consumer | Iterator operation producing a value, e.g. `collect`, `sum`, `find` |
| struct | A named collection of fields |
| enum | A type with several possible variants |
| variant | One named case of an enum, optionally carrying data |
| `impl` | Block defining methods for a type |
| trait | A set of methods describing shared behavior |
| trait bound | Constraint requiring a generic type to implement a trait |
| generic | A type or lifetime parameter, e.g. `T` in `Option<T>` |
| `Self` | Alias for the type inside an `impl` or trait |
| `Option<T>` | Either `Some(value)` or `None` |
| `Result<T, E>` | Either `Ok(value)` or `Err(error)` |
| `?` | Operator that propagates `Err`/`None` early |
| `Display` | Trait for user-facing `{}` formatting |
| `Debug` | Trait for diagnostic `{:?}` formatting |
| `derive` | Attribute asking the compiler to implement common traits |
| panic | Unwinding/abort caused by an unrecoverable mistake |
| `unwrap` | Method that panics on `Err`/`None`; avoid in production paths |
| `#[cfg(test)]` | Attribute compiling an item only for tests |
| unit test | Test of a small piece of code in the same module |
| integration test | Test in `tests/` that uses the crate's public API |
| interior mutability | Mutation through a shared reference via cell types (rare in this project) |
| RAII | Resources released when an owner is dropped, e.g. a closed `File` |
| `ExitCode` | Standard type for returning a process exit status from `main` |

## 11. Offline documentation commands

```bash
rustup doc --std          # standard library API
rustup doc --book         # The Rust Programming Language
rustup doc --reference    # syntax and semantics
rustup doc --cargo        # Cargo book
rustc --print sysroot     # locate installed source and docs
cargo help <command>      # e.g. cargo help test
cargo <command> --help    # e.g. cargo clippy --help
```

Search the offline docs by type or trait name (`Result`, `Iterator`, `BTreeMap`, `String`). The
standard-library source under the sysroot is useful when a doc page is ambiguous about behavior.

## 12. Numbered debugging checklist

1. Reproduce the failure with the smallest command that shows it.
2. Read the entire compiler or runtime message, including notes and help text.
3. Identify the exact line and the exact type involved; use `cargo check` for a fast loop.
4. Ask whether this is syntax, ownership, trait bounds, logic, or I/O.
5. For ownership errors, mark on paper where each borrow starts and ends.
6. For trait errors, check the concrete type with a temporary type annotation or a tiny scratch crate.
7. For logic errors, add a focused test that fails before fixing the code.
8. For file errors, run from a fresh scratch directory and inspect `tasks.json` directly.
9. Change one thing, then re-run `cargo check` and the focused test.
10. Run `cargo test` for the whole crate to catch regressions.
11. Run `cargo fmt --check` and `cargo clippy --all-targets -- -D warnings`.
12. Record the cause and fix in your build journal; look for the same pattern next time.

### Checkpoint / self-test

Without looking anything up, answer:

1. What is the difference between `E0382` and `E0502`?
2. Why can `{:02}` matter in a UTC timestamp but not in a success message?
3. Which three JSON mistakes would make your own parser reject a file it wrote itself?
4. What does `?` do, and where can you use it?
5. When would you choose `BTreeMap` over `HashMap` in this project?

## Next

Return to `11-feature-by-feature.md` for the next CLI phase, and use
`12-professional-habits.md` for the pre-commit routine once the phase passes its checks. When
the CLI is complete, continue with `14-api-barebones.md` for the optional API track.
