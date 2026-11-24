# 03 — Structs, enums, and `match`

This chapter turns loose values into named shapes. You will model records with structs,
model "one of these possibilities" with enums, and handle every possibility explicitly with
`match`. These are the tools you will use for your own record type and your own status type —
but the versions you write in the project will be yours, not copies from here.

## Why this matters for Task Tracker

A CLI command receives text, and the program needs structured values: something with an id, a
description, and a status; something that represents *exactly* one status; something that says
"there may be no matching task". Rust has no `null` and no "stringly typed" status by default.
Structs give you named fields, enums let the compiler prove you handled every status, and
`Option<T>` makes "not found" a value you must deal with instead of a surprise.

The requirement says the JSON file must contain `todo`, `in-progress`, and `done` strings.
That is a *wire format* decision. Your Rust representation is a separate decision, and this
chapter gives you the questions to make it deliberately. `06-json-without-crates.md` covers
the codec; `04-errors-and-validation.md` covers what to do when input is invalid.

## What you will learn

- The three struct forms: named fields, tuple structs, and unit structs.
- Keyed construction and field access.
- `impl` blocks, methods (`&self`, `&mut self`, `self`), and associated functions (`Self::new`).
- What `Debug`, `Clone`, `PartialEq`, `Eq`, and `Copy` mean, and when to derive them.
- Enums as **sum types** (a value that is exactly one of several variants).
- Exhaustive `match`, `_`, `|` patterns, `if let`, `while let`, and destructuring.
- Idioms for `Option<T>`, plus a first look at `Result<T, E>`.
- How to think about a status enum and about Rust-name-to-JSON-key mapping without copying an answer.

---

## 1. Three kinds of struct

A **struct** is a **product type**: a value that holds *all* of its fields at once.

```rust
struct Note {                 // named fields — the common case
    title: String,
    done: bool,
}

struct Meters(i32);            // tuple struct — one unnamed field, handy for newtypes

struct EndOfFile;              // unit struct — a marker with no data
```

Tuple structs are constructed with `Meters(3)` and their field is `value.0`. Unit structs are
constructed by naming them: `EndOfFile`. Use tuple structs when the field name adds nothing;
use named structs everywhere else, because `note.done` reads better than `note.1`.

### Keyed construction and field access

Named structs are built with **keyed construction**: you write every field name and its value.
Field order does not matter, but *completeness* does.

```rust
#[derive(Debug)]
struct Note {
    title: String,
    done: bool,
}

fn main() {
    let note = Note {
        done: false,
        title: String::from("Draft"),
    };
    println!("{}", note.title);     // move? No: println! borrows by reference
    println!("{:?}", note.done);
}
```

Expected output:

```text
Draft
false
```

`println!` takes its arguments by reference, so `note.title` is borrowed here and the `note`
is still usable afterwards. Passing `note.title` to a function that takes `String` *would*
move the field out and leave `note` partially moved.

Forgetting a field produces an abridged diagnostic like this:

```text
error[E0063]: missing field `done` in initializer of `Note`
 --> src/main.rs:9:15
  |
9 |     let note = Note { title: String::from("Draft") };
  |               ^^^^ missing `done`
```

A related shortcut, `Note { done: true, ..note }`, fills omitted fields from a base value. It
moves those fields out of `note`, so the original cannot be used as a whole afterwards.

---

## 2. `impl` blocks: methods and associated functions

An `impl` block attaches functions to a type. There are two flavors:

| Flavor | First parameter | Called as | Can change the value? |
|---|---|---|---|
| Method | `&self` | `book.title()` | No, borrowed immutably |
| Method | `&mut self` | `book.add_copy()` | Yes, borrowed mutably |
| Method | `self` | `book.into_title()` | Consumes (moves) the value |
| Associated function | no `self` | `LibraryBook::new(...)` | Nothing to change yet |

"Associated function" is the Rust name for what other languages call a static method. `Self`
inside an `impl` means "the type this block belongs to".

```rust
#[derive(Debug)]
struct LibraryBook { title: String, copies: u32 }

impl LibraryBook {
    fn new(title: &str) -> Self {
        Self { title: title.to_string(), copies: 0 }
    }
    fn title(&self) -> &str { &self.title }   // &String coerces to &str
    fn add_copy(&mut self) { self.copies += 1; }
    fn into_title(self) -> String { self.title }
}

fn main() {
    let mut book = LibraryBook::new("The Rust Book");
    book.add_copy();
    println!("{}: {}", book.title(), book.copies);
    let title = book.into_title();
    println!("{title}");
}
```

Expected output:

```text
The Rust Book: 1
The Rust Book
```

Choose the receiver by asking: does the call need to change the value? Does it need to keep the
value afterwards? A `self` method is right when the value is being transformed into something
else; a `&self` method is right for queries.

> **Professional habit.** A function named `new` conventionally returns `Self` directly when
> construction cannot fail. If it *can* fail (parsing, validation, I/O), name the fallible
> version something like `try_new` and return `Result<Self, E>`. That naming promise is worth
> keeping — see `04-errors-and-validation.md`.

---

## 3. Derivable traits: `Debug`, `Clone`, `PartialEq`, `Eq`, `Copy`

A **trait** is a named set of behavior that types can implement. `#[derive(...)]` asks the
compiler to write a straightforward implementation for you.

| Derive | What it gives you | Watch out for |
|---|---|---|
| `Debug` | `{:?}` formatting, and diagnostics in tests/errors | Needed if you return `Result<_, E>` from `main` |
| `Clone` | `.clone()` making a new value | Deep-copies heap data such as `String` |
| `PartialEq` | `==` and `!=` | Required before `Eq` |
| `Eq` | Marks equality as reflexive/consistent; used by keys | Only when equality really behaves that way |
| `Copy` | Implicit bit-for-bit copy on move | Requires `Clone`; only if *every* field is `Copy` |

```rust
#[derive(Debug, Clone, PartialEq, Eq, Copy)]
struct StageId(i32);            // i32 is Copy, so this works

#[derive(Debug, Clone, PartialEq, Eq)]
struct Measurement {
    label: String,              // String is NOT Copy
    value: i32,
}
```

Adding `Copy` to `Measurement` fails with `error[E0204]: the trait Copy cannot be implemented
for this type — this field does not implement Copy`. `Copy` is for small, plain values (`i32`,
`bool`, `char`, references). Owners of heap memory are deliberately not `Copy`, so a move stays
visible in the code. Derive `Debug` on nearly every type you define: it makes assertions and
compiler messages more useful.

---

## 4. Enums: sum types

An **enum** is a **sum type**: a value that is exactly one of a fixed set of **variants**.
Think "one of", not "all of" — that is the opposite of a struct.

```rust
#[derive(Debug, Clone, PartialEq, Eq)]
enum Stage {
    Planned,
    Running { since: u64 },     // variant with named data
    Finished,
}
```

`Stage::Running { since: 42 }` is one `Stage` value carrying extra information. Other
variants carry nothing. You never have a "Planned and Finished" value, and you never have an
arbitrary integer pretending to be a status. That is the whole point.

### `match` and exhaustiveness

`match` compares a value against patterns and runs the matching arm. It is an **expression**:
every arm must produce the same type.

```rust
fn label(stage: &Stage) -> &'static str {
    match stage {
        Stage::Planned => "planned",
        Stage::Running { .. } => "running",
        Stage::Finished => "finished",
    }
}
```

Rust requires **exhaustiveness**: every possible value must be covered. Delete one arm and you
get:

```text
error[E0004]: non-exhaustive patterns: `Stage::Finished` not covered
 --> src/main.rs:3:5
  |
3 |     match stage {
  |     ^^^^^^^^^^^ pattern `Stage::Finished` not covered
```

This is a feature. Add a variant next month and the compiler points at every `match` you must
update.

### The wildcard `_` and or-patterns `|`

`_` matches anything and binds nothing. `|` lets one arm handle several patterns.

```rust
fn is_open(stage: &Stage) -> bool {
    match stage {
        Stage::Planned | Stage::Running { .. } => true,
        _ => false,
    }
}
```

Use `_` when "everything else behaves the same" is genuinely true. Be aware of the tradeoff:
a wildcard silently absorbs a *new* variant you add later, so exhaustive arms are safer for
status-like enums.

### `if let` and `while let`

When you care about one pattern only, `if let` is a shorter `match` with a `_` arm:

```rust
if let Stage::Running { since } = stage {
    println!("running since {since}");
}
```

`while let` repeats as long as a pattern keeps matching. It is useful with iterators:

```rust
let mut stages = vec![Stage::Planned, Stage::Finished].into_iter();
while let Some(stage) = stages.next() {
    println!("{stage:?}");
}
```

This prints `Planned` and then `Finished`.

### Destructuring structs and enums

**Destructuring** means taking a value apart into its fields in a pattern.

```rust
struct Measurement { label: String, value: i32 }

let m = Measurement { label: String::from("width"), value: 7 };
let Measurement { label, value } = m;      // moves both fields
println!("{label} = {value}");

let m = Measurement { label: String::from("height"), value: 4 };
let Measurement { label, value } = &m;     // label: &String, value: &i32
println!("{label} = {value}");
```

Expected output:

```text
width = 7
height = 4
```

In edition 2024, matching a reference with a non-reference pattern already binds *by
reference* (this is called **match ergonomics**). Do not write `ref label` there: the compiler
says `cannot explicitly borrow within an implicitly-borrowing pattern` and suggests removing it.

Enum destructuring works the same way: `if let Stage::Running { since } = stage` pulls `since`
out of the one variant that has it.

---

## 5. `Option<T>` idioms

`Option<T>` is an enum with two variants: `Some(T)` and `None`. It replaces `null` and makes
absence explicit in the type.

| Tool | Result | Use when |
|---|---|---|
| `match opt { Some(v) => ..., None => ... }` | anything | Both cases need different logic |
| `opt.map(f)` | `Option<U>` | Transform a value that may be absent |
| `opt.unwrap_or(fallback)` | `T` | A plain default is correct |
| `opt.unwrap_or_else(|| fallback())` | `T` | The default is expensive to build |
| `opt.expect("reason")` | `T` or **panic** | Almost never in normal program paths |
| `opt.ok_or(err)` | `Result<T, E>` | Absence is an error (full version in `04-errors-and-validation.md`) |

```rust
let found: Option<&str> = Some("draft");
let name = found
    .map(str::to_uppercase)
    .unwrap_or_else(|| String::from("untitled"));
println!("{name}");                       // DRAFT
```

`expect` is not validation. It is a claim that `None` is impossible, and if you are wrong the
program panics:

```rust
let id: Option<i32> = None;
let id = id.expect("id must exist");
```

```text
thread 'main' panicked at src/main.rs:2:22:
id must exist: None
```

A user typo producing `None` should become a friendly message and a non-zero exit code, not a
panic. Chapter `04-errors-and-validation.md` shows how.

Preview of the bridge to errors:

```rust
let found: Option<&str> = None;
let result = found.ok_or("not found");    // Result<&str, &str>
```

`ok_or` builds its error eagerly; `ok_or_else` builds it only when needed. Prefer the latter
when the error involves allocation.

---

## 6. `Result<T, E>` preview

`Result<T, E>` is also an enum: `Ok(T)` or `Err(E)`. It is the topic of the next chapter. For
now, note that it can be matched, mapped, and shortened with `?`:

```rust
#[derive(Debug)]
enum LoadError { Missing }

fn load(raw: Result<String, LoadError>) -> Result<usize, LoadError> {
    let text = raw?;        // on Err, return early with that error
    Ok(text.len())
}
```

`?` works inside a function that returns `Result` (or `Option`). In `Result` functions it also
converts the error type via `From`, which is why custom error types and `impl From` matter so
much in `04-errors-and-validation.md`.

---

## 7. Designing a status representation (your task, not a copy-paste)

Use a generic example as a thinking aid. Suppose a `Stage` enum must survive a round trip
through JSON:

- Rust field names are `snake_case`; JSON keys are often `camelCase`. For example, a generic
  `LibraryBook` might have a Rust field `checked_out_at` but a JSON key `checkedOutAt`.
- The enum variant names are Rust identifiers; the JSON values are **exact strings** with
  their own spelling.

Answer these in your journal before writing code:

1. How many variants does your status need, and what is the default?
2. Which exact strings does the JSON format accept (`todo`, `in-progress`, `done`), and which
   variant corresponds to each? How will you test value → string → value → equality?
3. Where will that correspondence live: a method on the enum, the JSON module, or scattered at
   every call site? Which choice gives you one place to change?
4. What happens when the file contains an unrecognized status string — error, default, or silent
   loss of data? Who reports it?
5. Which Rust field names differ from their JSON keys? Who owns the rename: the type, the codec,
   or a helper used by the codec?

Do not write the project's type while answering. Decide the mapping on paper, then implement
it in `11-feature-by-feature.md` when that phase asks for it. `06-json-without-crates.md`
covers writing and reading strings safely.

---

## Common mistakes

- **Confusing `&self` with `self`.** `self` moves the value; a later use of it fails to compile.
  Reach for `&self` first.
- **Assuming struct construction is positional.** It is keyed: `Note { done, title }` is fine.
- **Deriving `Copy` on anything containing `String`.** If the compiler rejects it, that is the
  answer, not an obstacle.
- **Using `_` in every `match`.** It compiles, but it also hides missing status cases. Prefer
  exhaustive arms for enums you control.
- **Calling `unwrap()` on `Option` during normal execution.** `Some` is not guaranteed; handle
  `None` with `match`, `map`, or `ok_or`.

> **Professional habit.** Put the type decision in writing before the code: "this status is an
> enum with these variants; this string mapping is owned by the codec; unknown input is an
> error." When the compiler complains three chapters later, you can check the complaint
> against your decision instead of improvising.

---

## Apply it to Task Tracker

Questions only — write your answers in your journal, then implement later.

1. What named fields does your record need, and which ones can be derived rather than
   stored (for example, an id assigned by the store)?
2. For each field, is `String`, `&str`, an integer, or an enum the right type? Where does
   ownership sit: in the record or in a temporary borrow?
3. Which methods on your record take `&self`, which take `&mut self`, and do you need any
   that consume `self`? Can you name a real call site for each?
4. What is your status representation? How many variants, and which exact JSON strings map
   to them? Where does that mapping live?
5. Which lookups in your CLI can legitimately return "nothing"? Where will you use `match`,
   `map`, `unwrap_or`, or `ok_or` for each one?
6. Which derives do you need on each of your types, and why? In particular, will any type be
   compared in tests or printed in an error?

## Checkpoint

- [ ] I can construct a named struct, explain keyed construction, and write `new`, a `&self`
      query, a `&mut self` mutator, and a consuming method.
- [ ] I can explain why `String` cannot be part of a `Copy` type.
- [ ] I can write an exhaustive `match` over all variants and describe what happens when a
      variant is added, and use `map`/`unwrap_or`/`ok_or` on `Option` without `unwrap`.
- [ ] I can list the exact JSON status strings and describe where the string mapping lives
      without having written the project's type yet.

Next: `04-errors-and-validation.md`
