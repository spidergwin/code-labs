# 02 — Ownership and Borrowing

## Why this matters for Task Tracker

A task description is text that must be created from user input, stored in a record, printed in a
listing, serialized to JSON, and edited later. Each step raises the same question: who owns the text,
and who is merely looking at it? Rust answers at compile time through **ownership** and **borrowing**.

Most early Rust frustration is borrow-checker frustration. This chapter gives you the rules, the
vocabulary, the four diagnostics you will see most often, and a checklist for resolving them. Examples
use generic domains — notes, measurements, excerpts — not Task Tracker code.

## What you will learn

- The three ownership rules and what `drop` means
- Move semantics for assignments, function arguments, and returns
- The difference between `Copy` and `Clone`
- Borrowing with `&T` and `&mut T`, and the two borrow rules
- Non-lexical lifetimes and why reordering statements often fixes an error
- Slice types `&str` and `&[T]`, including UTF-8 slicing caveats
- Lifetime elision and the one explicit `'a` case you need for a struct holding `&str`
- How to diagnose `E0382`, `E0502`, `E0503`, and `E0597`
- How to choose between an owned `String` and a borrowed `&str` for a description argument
- Why interior mutability is out of scope for this project
- A mental checklist for the borrow checker

## 1. The three ownership rules

**Ownership** decides which binding is responsible for releasing a value's resources.

1. Each value in Rust has an **owner** (a binding).
2. There can only be one owner at a time.
3. When the owner goes out of **scope**, the value is **dropped**.

**Scope** is the region where a binding is valid. **Drop** means running cleanup code automatically —
for a `String`, releasing its heap buffer. There is no manual `free`.

The **stack** holds fixed-size data such as integers and a `String` descriptor (pointer, length,
capacity). The **heap** holds dynamically sized data such as the bytes of a growable `String`. If two
bindings both believed they owned one buffer, it could be released twice; rule 2 forbids that.

## 2. Move semantics

A **move** transfers ownership. After the move, the original binding cannot be used.

```rust
fn main() {
    let first = String::from("draft");
    let second = first;                 // ownership moves
    println!("{first}");                // error: first is no longer valid
    println!("{second}");
}
```

```text
error[E0382]: borrow of moved value: `first`
 --> src/main.rs:4:16
  |
2 |     let first = String::from("draft");
  |         ----- move occurs because `first` has type `String`, which does not implement the `Copy` trait
3 |     let second = first;
  |                  ----- value moved here
4 |     println!("{first}");
  |                ^^^^^ value borrowed here after move
  |
help: consider cloning the value if the performance cost is acceptable
  |
3 |     let second = first.clone();
  |                       ++++++++

error: aborting due to 1 previous error

For more information about this error, try `rustc --explain E0382`.
```

Moves happen on assignment (`let b = a;`), on calls to functions taking an owned parameter, and when
an owned value is returned. Three fixes, depending on intent:

1. **Borrow** if the callee only reads: pass `&first` to a `&str` parameter.
2. **Clone** if both bindings truly need their own copy: `first.clone()`.
3. **Return ownership** if the callee transforms the value: rebind at the call site.

An owned parameter does not mean the function discards the data; it means the function may keep or
modify it. If a function only measures text, a borrow is the honest type.

## 3. `Copy` versus `Clone`

**`Copy`** is a marker trait for values duplicable by a simple bit copy: assignment leaves the original
usable. **`Clone`** is an explicit operation that may allocate.

| Type | `Copy`? | Consequence |
|---|---|---|
| `i32`, `u32`, `usize`, `f64`, `bool`, `char` | yes | `let b = a;` leaves `a` usable |
| `&T` (shared reference) | yes | copying a pointer is cheap |
| `&mut T` | no | exclusivity must not be duplicated |
| `String`, `Vec<T>` | no | assignment moves; use `.clone()` for a copy |
| A struct of only `Copy` fields | can derive `Copy` | see `03-structs-enums-and-match.md` |

```rust
fn main() {
    let a = 7u32;
    let b = a;                    // copy, not a move
    println!("{a} and {b}");
    let text = String::from("draft");
    let copy = text.clone();      // explicit second buffer
    println!("{text} and {copy}");
}
```

Do not reach for `.clone()` automatically: it asserts "I need two independent owners." Often the real
answer is a borrow or a better statement order.

> **Professional habit.** When the compiler mentions a move, ask whether the callee needs ownership
> before adding `.clone()`.

## 4. Borrowing with `&T` and `&mut T`

A **borrow** is a reference to a value you do not own; creating it does not move the value. `&T` is a
shared (immutable) borrow: read but not modify. `&mut T` is a mutable borrow: read and modify, but
nobody else may borrow at the same time. The `&` in a type is a reference; the `&` in an expression
creates one. To assign through a mutable reference, **dereference** it with `*`.

```rust
fn add_one(n: &mut i32) {
    *n += 1;
}

fn length(text: &str) -> usize {
    text.len()                // method calls auto-deref
}

fn main() {
    let mut counter = 1;
    add_one(&mut counter);
    let note = String::from("draft");
    println!("{} / {}", counter, length(&note));
}
```

Field access and method calls auto-deref, so `book.copies` works through `&Book`; direct assignment
through `&mut` needs `*n = ...`.

## 5. The borrow rules

1. At any time you can have **one mutable borrow or any number of immutable borrows**, never both.
2. A reference must always point to a value that is still alive — no **dangling references**.

The first rule prevents data races and inconsistent reads. While a shared borrow exists the value
cannot change; while a mutable borrow exists nobody else can read it.

An immutable borrow used after a mutable borrow starts fails:

```rust
fn main() {
    let mut text = String::from("draft");
    let view = &text;
    text.push_str("!");
    println!("{view}");
}
```

```text
error[E0502]: cannot borrow `text` as mutable because it is also borrowed as immutable
 --> src/main.rs:4:5
  |
3 |     let view = &text;
  |                ----- immutable borrow occurs here
4 |     text.push_str("!");
  |     ^^^^^^^^^^^^^^^^^^ mutable borrow occurs here
5 |     println!("{view}");
  |                ---- immutable borrow later used here

error: aborting due to 1 previous error

For more information about this error, try `rustc --explain E0502`.
```

`E0503` appears when the *original binding itself* is used while it is mutably borrowed:

```rust
fn main() {
    let mut n = 1;
    let r = &mut n;
    n += 1;                  // direct use of n while r is alive
    *r += 1;
}
```

```text
error[E0503]: cannot use `n` because it was mutably borrowed
 --> src/main.rs:4:5
  |
3 |     let r = &mut n;
  |             ------ `n` is borrowed here
4 |     n += 1;
  |     ^^^^^^ use of borrowed `n`
5 |     *r += 1;
  |     ------- borrow later used here

error: aborting due to 1 previous error

For more information about this error, try `rustc --explain E0503`.
```

Fix both by ending the borrow before using the owner: use `r` first, then use `n` again, or separate
the mutable section from the read-only section into distinct functions.

## 6. Non-lexical lifetimes

**Non-lexical lifetimes** (NLL) means a borrow ends at its **last use**, not necessarily at the closing
brace of its block.

```rust
fn main() {
    let mut text = String::from("draft");
    let view = &text;
    println!("{view}");          // last use: borrow ends here
    text.push_str("!");          // allowed
    println!("{text}");
}
```

The `E0502` example failed only because `view` was used *after* the mutation. Moving the `println!`
above `push_str` is enough. Before cloning, try reordering statements. NLL does not make rules
optional: if a reference is used later, its borrow is still live.

> **Professional habit.** On `E0502` or `E0503`, mark the last use of each reference by hand. The fix
> is usually statement order or a smaller scope, not a lifetime annotation.

## 7. Slices: `&str` and `&[T]`

A **slice** is a borrowed, contiguous view into a sequence: a pointer plus a length, hence a "fat
pointer" and always a reference.

```rust
fn main() {
    let title = String::from("A tidy note");
    let first: &str = &title[0..1];      // byte indices: "A"
    let values = [10, 20, 30, 40];
    let middle: &[i32] = &values[1..3];  // [20, 30]
    println!("{first} / {middle:?}");
}
```

Slicing a `String` uses **byte** indices that must land on UTF-8 character boundaries. `&title[0..1]`
is fine for ASCII `A`; slicing in the middle of `é` panics. For checked slicing use `str::get(range)`,
which returns `Option<&str>` (`03-structs-enums-and-match.md` covers `Option`). An array coerces to
`&[T]`, so `fn total(values: &[i32])` accepts both `&values` and `&values[1..3]`.

## 8. Lifetimes: only what you need

A **lifetime** is the scope over which a reference is valid. **Lifetime elision** lets the compiler
infer most lifetimes from usage:

```rust
fn same_view(text: &str) -> &str {
    text                         // output borrows from the input
}
```

With one input reference and a reference return, the output gets the input's lifetime. The same rule
applies to methods taking `&self`: the output borrows from the receiver.

The case you need explicitly is a **struct that stores a reference**:

```rust
struct Excerpt<'a> {
    text: &'a str,
}

impl<'a> Excerpt<'a> {
    fn preview(&self) -> &str {
        self.text                // elided: tied to &self
    }
}
```

`'a` (pronounced "tick a") does not choose a duration; it *connects* the stored reference to the
buffer it points into. That buffer must outlive every `Excerpt` borrowing it. Without the annotation:

```text
error[E0106]: missing lifetime specifier
 --> src/main.rs:1:24
  |
1 | struct Excerpt { text: &str }
  |                        ^ expected named lifetime parameter
  |
help: consider introducing a named lifetime parameter
  |
1 | struct Excerpt<'a> { text: &'a str }
  |               ++++          ++

error: aborting due to 1 previous error

For more information about this error, try `rustc --explain E0106`.
```

### Avoid premature references and lifetimes

- Store an owned `String` in a struct until you have a concrete reason to borrow.
- Borrow at function boundaries (`&str`, `&[T]`) when the function only inspects data.
- Do not add `'static` to silence an error: it claims the reference is valid for the whole program.
- Do not add lifetimes to solve `E0382`; moves are solved by cloning, borrowing, or returning.
- Every struct with a lifetime parameter constrains its callers. Pay that cost only for a genuine view.

Structs, `impl`, and `Option` are introduced in `03-structs-enums-and-match.md`.

## 9. `E0597`: the dangling case

Rule 2 produces `E0597` when the owner dies before the reference:

```rust
fn main() {
    let excerpt;
    {
        let text = String::from("draft");
        excerpt = &text;
    }
    println!("{excerpt}");
}
```

```text
error[E0597]: `text` does not live long enough
 --> src/main.rs:5:19
  |
4 |         let text = String::from("draft");
  |             ---- binding `text` declared here
5 |         excerpt = &text;
  |                   ^^^^^ borrowed value does not live long enough
6 |     }
  |     - `text` dropped here while still borrowed
7 |     println!("{excerpt}");
  |                ------- borrow later used here

error: aborting due to 1 previous error

For more information about this error, try `rustc --explain E0597`.
```

Fix it by keeping the owner alive as long as the reference, or by storing an owned `String` instead.

## 10. Interior mutability is out of scope

`std::cell::Cell` and `std::cell::RefCell` allow mutation through a shared reference; `RefCell`
enforces borrow rules at **runtime** and can panic. You do not need them here. Design functions so
mutation appears in signatures as `&mut`, pass state explicitly, and keep the compiler as your
reviewer. Read `rustup doc --std` for `std::cell` later if curious; using it now would hide exactly
the lessons in this chapter.

## 11. Ownership strategy for a description argument

A generic note record must keep its description; a helper that only measures it need not own it.

```rust
struct Note {
    description: String,
}

fn summarize(description: &str) -> usize {
    description.len()
}

fn main() {
    let note = Note { description: String::from("buy milk") };
    println!("length: {}", summarize(&note.description));
}
```

| Situation | Prefer |
|---|---|
| Value must outlive the call | owned `String` |
| Value must be modified | `&mut String` or ownership plus return |
| Function only reads or measures | `&str` |
| Function returns a view into its input | `&'a str` with an explicit lifetime |
| You are unsure | own the `String` first; borrow later if clarity demands it |

`summarize` accepts a literal, a borrowed `String`, or a slice of one. Borrow at the boundary, own in
storage.

## 12. Mental checklist for the borrow checker

1. Which binding owns the value, and where is it dropped?
2. Was ownership moved by assignment, a call, or a return?
3. Does the callee truly need ownership, or would `&T` / `&str` be enough?
4. Is a shared borrow live at the same time as a mutable one?
5. What is the **last use** of each reference? Can statements be reordered?
6. Can the borrow be confined to a smaller block?
7. Is this an `E0597` (owner dies too early)? Should the data be owned instead?
8. Are you trying to solve a move with a lifetime or an `allow` attribute? Re-read steps 2–3.
9. If cloning is genuinely correct, clone at the narrowest point and say why in a comment.

> **Professional habit.** Record the error code and your answer to question 5 in your build journal;
> most borrow errors are statement-order errors.

## 13. Common mistakes

| Mistake | Error or symptom | Fix |
|---|---|---|
| Reusing a moved binding | `E0382` | Borrow, clone deliberately, or return ownership |
| Cloning by reflex | extra allocations, unclear design | Ask whether a borrow suffices |
| Adding `.clone()` to silence `E0502` | hides a sequencing problem | Shorten the borrow or reorder |
| Taking `&mut` when reading only | blocks other borrows | Use `&T` |
| Adding `'static` to fix a move | error changes or spreads | Diagnose ownership first |
| Slicing at a non-boundary byte index | runtime panic | Use `str::get` or char-safe methods |
| Storing `&str` with no lifetime | `E0106` | Add `'a`, or store `String` |
| Assuming references keep owners alive | `E0597` | Extend the owner's scope or own the data |
| Using `static mut` for shared state | `unsafe`, out of scope | Pass `&mut` explicitly |

## Apply it to Task Tracker

Questions and a decision checklist only — do not write implementation code here.

- Which fields must be owned so they survive after the argument parser returns?
- Which functions can accept `&str` because they only read a description?
- Where will you need `&mut` to update a record, and how will other borrows stay out of that scope?
- If a display function borrows several fields, in what order should printing and updating happen?
- Would a struct holding `&str` help or hurt? What lifetime would it force on every caller?
- What would `'static` be claiming if you used it to silence an error? Is that claim true?

## Checkpoint

1. State the three ownership rules.
2. Why does `let b = a;` make `a` unusable for a `String` but not for a `u32`?
3. Name three ways to fix a move error.
4. What is the difference between `Copy` and `Clone`?
5. Can two shared borrows coexist? Can a shared and a mutable borrow coexist?
6. What does non-lexical lifetimes mean for the last use of a reference?
7. When do you need an explicit lifetime parameter on a struct?
8. Match each code to its meaning: `E0382`, `E0502`, `E0503`, `E0597`.

## Next

Continue with `03-structs-enums-and-match.md` to model records and statuses with structs, enums,
`Option`, and exhaustive `match`.
