# 01 — Rust Basics: Bindings, Types, Expressions, and Control Flow

## Why this matters for Task Tracker

Every later chapter assumes you can read a Rust function without pausing. The CLI deals with counts,
identifiers, descriptions, booleans, and formatted output; the syntax is small but precise. Rust
differs from many languages in one crucial way: almost everything is an **expression** (code that
produces a value). Learning that early makes `if` and `match` feel natural. Examples below use generic
domains — notes, library books, counters — and do not implement the CLI.

## What you will learn

- `let` bindings, mutability, type inference, and shadowing
- The common scalar types and when `usize` is the right integer
- The difference between `&str` and `String` at an introductory level
- Functions with parameters, return types, and the unit type `()`
- Statements versus expressions, including blocks and `if` as an expression
- `loop`, `while`, `for`, ranges, `break`, `continue`, and labels
- `match` as an exhaustive expression
- `const` versus `static`
- Line comments, block comments, and rustdoc comments
- `println!`, `eprintln!`, `format!`, captured format arguments, and `Debug` versus `Display`
- Why `#[allow(unused_variables)]` usually hides a mistake

## 1. Bindings and mutability

`let` introduces a **binding**: a name for a value. Bindings are immutable by default; add `mut` only
when you intend to reassign.

```rust
fn main() {
    let mut counter = 0;
    counter += 1;
    let label = "notes";
    println!("{label}: {counter}");
}
```

```text
notes: 1
```

**Type inference** means the compiler deduces a type from how a value is created and used. You can
always state it explicitly:

```rust
let guessed = 5;          // inferred as i32
let price = 9.99;         // inferred as f64
let count: usize = 0;     // usize for counts, lengths, and indices
let active: bool = true;
```

The defaults are `i32` for integer literals and `f64` for floating-point literals. Annotate when
inference has too little information or when you want to document intent.

### Shadowing

A second `let` with the same name **shadows** the first binding. This is not mutation, and the new
binding may have a different type.

```rust
fn main() {
    let note = "draft";
    let note = note.len();       // shadows a &str with a usize
    println!("note length: {note}");
}
```

Use `mut` when the same value changes; use shadowing when the new value is conceptually different.

> **Professional habit.** Prefer immutable bindings. When a warning says a variable does not need
> `mut`, remove the `mut` rather than suppressing the warning.

## 2. Scalar types

A **scalar** is a single value, as opposed to a collection.

| Type | Meaning | Notes |
|---|---|---|
| `i32` | signed 32-bit integer | default for integer literals |
| `i64` | signed 64-bit integer | use for counts that may exceed 32 bits |
| `u32` | unsigned 32-bit integer | non-negative counts |
| `usize` | pointer-sized unsigned integer | lengths, indices, and collection sizes |
| `f64` | 64-bit float | default for decimal literals |
| `bool` | `true` or `false` | conditions must be `bool` — no truthiness |
| `char` | a Unicode scalar value | written with single quotes, e.g. `'x'` |

Related types come later: `&str`/`String` in `05-collections-and-strings.md`, `Option<T>` in
`03-structs-enums-and-match.md`, and `Result<T, E>` in `04-errors-and-validation.md`. Integer
arithmetic can overflow; do not rely on wraparound. Parse user input explicitly
(`07-filesystem-and-cli.md`).

## 3. `str` versus `String`, briefly

A **string slice** `&str` is a borrowed view of UTF-8 text. A `String` is an owned, growable text
buffer. A literal such as `"fixed text"` has type `&str`.

```rust
fn main() {
    let borrowed: &str = "fixed text";
    let owned = String::from("growable text");
    let view: &str = &owned;       // borrow the owned buffer
    println!("{borrowed} / {view}");
}
```

Literals and read-only views are `&str`; text you must store, modify, or return without a borrow is
`String`. Borrowing is cheap, while `to_string()` and `String::from` create owned data. Ownership and
lifetimes are covered in `02-ownership-and-borrowing.md`; string methods in
`05-collections-and-strings.md`.

## 4. Functions

Functions use `fn`, typed parameters, and an optional return type after `->`. The last expression is
the return value.

```rust
fn is_available(copies: u32) -> bool {
    copies > 0
}

fn main() {
    println!("available: {}", is_available(3));
}
```

A function with no return type returns the **unit type** `()`, a value carrying no information.
Parameters each need a type annotation; there are no implicit returns or default arguments.

## 5. Statements and expressions

A **statement** performs an action and ends in a semicolon. An **expression** evaluates to a value.
A semicolon turns an expression into a statement, discarding its value.

```rust
fn next_count(current: u32) -> u32 {
    let doubled = current * 2;   // statement
    doubled + 1                  // expression: the return value
}
```

A block `{ ... }` is an expression whose value is its last expression:

```rust
let label = {
    let ready = true;
    if ready { "ready" } else { "waiting" }
};
println!("{label}");
```

A semicolon on the final expression makes the function return `()`:

```rust
fn double(n: i32) -> i32 {
    n * 2;
}
```

```text
error[E0308]: mismatched types
 --> src/main.rs:1:22
  |
1 | fn double(n: i32) -> i32 {
  |    ------            ^^^ expected `i32`, found `()`
  |    |
  |    implicitly returns `()` as its body has no tail or `return` expression
2 |     n * 2;
  |          - help: remove this semicolon to return this value

error: aborting due to 1 previous error

For more information about this error, try `rustc --explain E0308`.
```

"Expected X, found `()`" often means a stray semicolon.

### Explicit `return`

`return` exits a function immediately. It is useful for an early exit; the tail expression is
preferred for the normal path.

```rust
fn describe(value: i32) -> &'static str {
    if value < 0 {
        return "negative";
    }
    "non-negative"
}
```

`return` has the **never** type `!`: it produces no value, so it can appear where any type is expected.

## 6. `if` as an expression

Conditions must be `bool`; there is no "truthiness".

```rust
fn status_word(copies: u32) -> &'static str {
    if copies == 0 {
        "empty"
    } else if copies < 5 {
        "few"
    } else {
        "many"
    }
}
```

All branches must produce the same type. An `if` without `else` used as a statement produces `()`.
Pattern-based `if let` appears in `03-structs-enums-and-match.md`.

> **Professional habit.** Keep branch bodies short. Extract a named function when an `if` expression
> grows long.

## 7. Loops and ranges

`loop` repeats until `break`; `while` repeats while a boolean condition holds; `for` iterates over a
collection or range.

```rust
fn main() {
    let mut total = 0;
    for n in 1..=5 {           // inclusive: 1, 2, 3, 4, 5
        total += n;
    }
    while total < 20 {
        total += 1;
    }
    loop {
        if total > 25 { break; }
        total += 1;
    }
    println!("{total}");       // 26
}
```

`0..5` is **exclusive** of 5; `0..=5` is **inclusive**. `usize` is the usual type for collection
indices. `continue` skips the rest of the current iteration. A **label** (written `'name:`) lets
`break` or `continue` target an outer loop:

```rust
'search: for row in 0..3 {
    for col in 0..3 {
        if row == 1 && col == 1 { continue 'search; }
        if row == 2 { break 'search; }
    }
}
```

`loop` can produce a value with `break value`:

```rust
let mut counter = 1;
let first_even = loop {
    if counter % 2 == 0 { break counter; }
    counter += 1;
};
```

## 8. `match`, briefly

`match` compares a value against patterns and evaluates to the chosen arm's expression. It is
**exhaustive**: every possible value must be covered, hence the common catch-all `_` arm.

```rust
fn describe_kind(kind: char) -> &'static str {
    match kind {
        'n' => "note",
        'b' => "book",
        _ => "unknown",
    }
}
```

All arms must have the same result type, and the whole `match` can be assigned or returned. In
`03-structs-enums-and-match.md` you will match enums and `Option`, where exhaustiveness becomes a
safety feature.

## 9. `const` and `static`

```rust
const MAX_NOTES: usize = 100;
static APP_NAME: &str = "note-lab";
```

| Feature | `const` | `static` |
|---|---|---|
| Meaning | compile-time constant, substituted where used | one value at a fixed memory address |
| Initializer | must be const-evaluable | must be const-evaluable |
| Lifetime | not a reference lifetime | references to it have the `'static` lifetime |
| Naming | `SCREAMING_SNAKE_CASE` | `SCREAMING_SNAKE_CASE` |

`static mut` requires `unsafe` and is out of scope. Prefer passing state explicitly into functions;
see `02-ownership-and-borrowing.md`.

## 10. Comments and doc comments

```rust
// A line comment: explain *why*, not what the code already says.
/* A block comment, useful for a temporary note. */

/// Returns true when a library book can be borrowed.
///
/// A book with zero copies is unavailable.
fn is_available(copies: u32) -> bool {
    copies > 0
}
```

- `///` is an **outer doc comment**, attached to the following item.
- `//!` is an **inner doc comment**, for the enclosing module or crate, usually at the file top.
- `rustdoc` turns doc comments into local HTML; `cargo doc --open` displays it.
- `cargo test` runs code examples in doc comments as **doc tests**, so keep them correct.

A comment that explains a subtle invariant is valuable; one that repeats the code goes stale.

## 11. Printing and formatting

`println!` writes a line to standard output, `print!` writes without a newline, `eprintln!` writes to
standard error, and `format!` builds a `String` instead of writing.

```rust
fn main() {
    let owner = "Ada";
    let copies = 3u32;
    println!("{owner} has {copies} copies");       // captured identifiers
    println!("{0}: {1}", "note", 7);               // positional
    eprintln!("warning: low stock");               // stderr
    let message = format!("{owner} has {copies}");
    println!("{message}");
}
```

Captured arguments such as `{owner}` require Rust 1.58 or newer, so they work on 1.99.1. Format
specifiers include `{value:>5}` (right-align in width five) and `{value:.2}` (two decimal places);
see `rustup doc --std` under `std::fmt`.

### `Debug` versus `Display`

`{}` uses `Display`: human-readable output that user-defined types do not get automatically. `{:?}`
uses `Debug`: developer-oriented output that can be derived.

```rust
#[derive(Debug)]
struct LibraryBook {
    title: String,
    copies: u32,
}

fn main() {
    let book = LibraryBook { title: String::from("Notes"), copies: 2 };
    println!("compact: {:?}", book);
    println!("pretty: {:#?}", book);
    println!("{} copies", book.copies);
}
```

Using `{}` on the struct itself produces:

```text
error[E0277]: `LibraryBook` doesn't implement `std::fmt::Display`
 --> src/main.rs:6:20
  |
6 |     println!("{}", book);
  |               --   ^^^^ `LibraryBook` cannot be formatted with the default formatter
  |               |
  |               required by this formatting parameter
  |
help: the trait `std::fmt::Display` is not implemented for `LibraryBook`
  = note: in format strings you may be able to use `{:?}` (or `{:#?}` for pretty-print) instead

error: aborting due to 1 previous error

For more information about this error, try `rustc --explain E0277`.
```

`#[derive(Debug)]` fixes debug formatting; implementing `Display` is a deliberate choice, which you
will do for error types in `04-errors-and-validation.md`. Use `eprintln!` for user-facing errors so
redirecting standard output does not hide them.

> **Professional habit.** Derive `Debug` on small data types while developing. It costs one line and
> makes `dbg!` and `{:?}` useful without designing a public display format.

## 12. The `#[allow(unused_variables)]` trap

`#[allow(unused_variables)]` tells the compiler to stop warning about unused bindings. The scratch
file `learnrust/main.rs` uses it at crate scope, hiding every such warning.

Warnings are diagnostic, not noise: an unused variable often means a typo, a forgotten argument, or a
logic step you never connected. Prefer these fixes:

- delete the binding if it is unnecessary;
- rename it to `_name` when you must bind but will not use it;
- use the value where it belongs;
- scope an `allow` to one item, with a comment explaining why.

The same caution applies to `#[allow(dead_code)]` and crate-wide `allow` attributes. Lint output is
part of the feedback loop built in `00-toolchain-and-cargo.md`.

## 13. Common mistakes

| Mistake | Symptom | Fix |
|---|---|---|
| Adding `;` to a tail expression | `expected T, found ()` | Remove the semicolon |
| Expecting non-`bool` conditions | type error on `if` | Compare explicitly: `n > 0` |
| Confusing `0..5` and `0..=5` | off-by-one results | `..` excludes the end; `..=` includes it |
| Using `{}` for a derived-Debug struct | `E0277` about `Display` | Use `{:?}`, or implement `Display` |
| Assuming shadowing mutates the original | unexpected old value | Use `mut` to reassign |
| Defaulting every number to `i32` | mismatches with indices | Use `usize` for sizes and indices |
| Silencing warnings with crate-wide `allow` | mistakes disappear | Fix or scope the warning |
| Using `return` everywhere | harder-to-read flow | Prefer a tail expression |

## Apply it to Task Tracker

Questions only — decide these yourself; no solution code belongs here.

- Which values will be counts or indices, and therefore candidates for `usize`?
- Which text will be a literal or temporary view (`&str`), and which must be stored (`String`)?
- Where does a user-facing error belong: standard output or standard error? Why?
- Which logic can be expressions (`if`, `match`, blocks) rather than assignment sequences?
- Where would `#[derive(Debug)]` help you inspect a value while developing?
- What condition makes a task count as "done" in a filtered listing?

## Checkpoint

1. What is the difference between a statement and an expression?
2. Why does `fn f() -> i32 { 1 + 1; }` fail to compile?
3. What values do `0..4` and `0..=4` produce in a `for` loop?
4. How do you break out of an outer loop from inside a nested loop?
5. Why must every arm of a `match` produce the same type?
6. Name one difference between `const` and `static`.
7. When would you use `{:?}` instead of `{}`?
8. Why is `#[allow(unused_variables)]` at the top of a file usually a bad idea?

## Next

Continue with `02-ownership-and-borrowing.md` to understand moves, borrows, slices, and lifetimes.
