# 05 — Collections and strings

This chapter is the working vocabulary of Rust data handling: `Vec` for ordered sequences,
`String` and `&str` for text, maps for keyed lookup, iterators for transformations, slices for
borrowed views, and formatting for output. All examples are generic; your Task Tracker code
comes later.

## Why this matters for Task Tracker

A CLI reads text from arguments and from a JSON file, keeps a collection of records in memory,
looks some of them up by id, filters a list by status, and prints a stable, readable listing.
Every one of those steps uses this chapter. Two details cause more confusion than any others:
the difference between an owned `String` and a borrowed `&str`, and the fact that Rust strings
are UTF-8 bytes, not arrays of characters. Both are covered here so you never need to guess.

## What you will learn

- Create and inspect a `Vec`; choose `get` over indexing when absence is expected.
- Move versus borrow items in `for` loops; compare `.iter()`, `.iter_mut()`, `.into_iter()`.
- Convert between `String` and `&str`, and avoid invalid UTF-8 byte slicing.
- Choose between `HashMap` and `BTreeMap`, especially when output order matters.
- Use `map`, `filter`, `find`, `any`, `all`, `enumerate`, and `collect` fluently.
- Sort explicitly with `sort_by_key` for deterministic output.
- Write helpers that take slices `&[T]`.
- Build output with `format!`, `write!`, `writeln!`, and `std::fmt::Write`.
- Parse integers with `parse::<i32>()` and understand `ParseIntError`.
- Recognize why JSON needs string escaping (the full design is in `06-json-without-crates.md`).

---

## 1. `Vec`: creation, access, and capacity

`Vec<T>` is a growable, ordered sequence stored on the heap. `vec![...]` and `Vec::new()` are
the common constructors; `Vec::with_capacity(n)` pre-allocates when you know roughly how many
items you will push.

```rust
let mut readings: Vec<i32> = Vec::new();
readings.push(3);
readings.push(9);

println!("len = {}, capacity = {}", readings.len(), readings.capacity());
println!("empty = {}", readings.is_empty());
println!("index 0: {}", readings[0]);
println!("get 1: {:?}", readings.get(1));
println!("get 9: {:?}", readings.get(9));

for value in readings.iter() {
    println!("{value}");
}
```

Expected output (capacity is an implementation detail):

```text
len = 2, capacity = <at least 2, often 4>
empty = false
index 0: 3
get 1: Some(9)
get 9: None
3
9
```

`len()` is the number of items; `capacity()` is how many the allocation can hold before it
grows. Never assert an exact capacity in a test. Indexing `readings[9]` **panics**; `get(9)`
returns `Option<&i32>`, which you can handle. Use indexing only when out-of-range is a bug.

## 2. Moving versus borrowing in loops

| Loop form | Item you receive | Effect on the original |
|---|---|---|
| `for x in &v` | `&T` | Borrows; `v` still usable |
| `for x in v.iter()` | `&T` | Same as `&v` |
| `for x in &mut v` | `&mut T` | Mutates in place; `v` must be `mut` |
| `for x in v.into_iter()` | `T` | Moves each item; `v` is unusable afterwards |

```rust
let names = vec![String::from("a"), String::from("b")];
for name in &names {
    println!("{name}");              // borrow
}
println!("still have {} names", names.len());

let mut names = names;               // move the Vec into a mutable binding
for name in &mut names {
    name.push('!');                  // mutate through &mut String
}
for name in names {
    println!("{name}");              // move each String out
}
// println!("{names:?}");            // would fail: names was consumed
```

Expected output:

```text
a
b
still have 2 names
a!
b!
```

If a loop needs to keep the collection, borrow it. If it needs ownership of the items — for
example, moving them into a new structure — use `into_iter()` and accept that the old vector
is gone. This is the same ownership rule from `02-ownership-and-borrowing.md`, now in a loop.

## 3. `String` versus `&str`

`String` is an owned, growable UTF-8 string (it owns a heap buffer). `&str` is a borrowed,
fixed-length view into UTF-8 bytes. A **lifetime** on a reference describes how long the
borrow is valid; most of the time Rust infers it.

| Need | Write |
|---|---|
| Own a new string from a literal | `String::from("hello")` or `"hello".to_string()` |
| Borrow a `String` as text | `s.as_str()` or just `&s` where a `&str` is expected |
| Full-range slice of a `String` | `&s[..]` (valid, but `as_str()` is clearer) |
| Copy an owned string | `s.clone()` (allocates a second buffer) |
| Accept either kind in an argument | `&str` (a `&String` coerces to it) |

```rust
let owned = String::from("hello");
let also: String = "hello".to_string();
let borrowed: &str = owned.as_str();
let via_index: &str = &owned[..];
println!("{owned} {also} {borrowed} {via_index}");
```

Expected output:

```text
hello hello hello hello
```

Prefer `&str` in function parameters when you only read text: it accepts both literals and
owned strings. Return `String` when the result must outlive the input.

### UTF-8 caveat: strings are bytes

`String` and `&str` store UTF-8. `len()` counts **bytes**, and byte indexing must stop on a
**character boundary** (the start of a UTF-8 encoded `char`).

```rust
let word = "é";                     // two bytes in UTF-8
println!("{} bytes", word.len());
// let first = &word[0..1];         // panics: 1 is not a char boundary
println!("{:?}", word.chars().next());
```

Expected output:

```text
2 bytes
Some('é')
```

Use `chars()` to iterate characters, `char_indices()` when you need byte offsets, or
`s.get(0..1)` which returns `Option<&str>` instead of panicking. Your JSON reader will work
byte by byte; it must only cut strings at valid boundaries. See `06-json-without-crates.md`.

## 4. `HashMap` and `BTreeMap`

A map associates keys with values. Both types are in `std::collections` (no external crate
needed).

```rust
use std::collections::{BTreeMap, HashMap};

let mut counts: HashMap<&str, i32> = HashMap::new();
counts.insert("planned", 2);
counts.insert("running", 1);
println!("{:?}", counts.get("planned"));

let mut ordered: BTreeMap<&str, i32> = BTreeMap::new();
ordered.insert("planned", 2);
ordered.insert("running", 1);
for (key, value) in &ordered {
    println!("{key}: {value}");
}
```

Expected output (the `HashMap` is *not* iterated here on purpose):

```text
Some(2)
planned: 2
running: 1
```

| Type | Lookup | Iteration order | Use when |
|---|---|---|---|
| `HashMap<K, V>` | Average O(1) | Unspecified and randomized per process | Pure lookup, order irrelevant |
| `BTreeMap<K, V>` | O(log n) | Sorted by key | Output order is part of the contract |

`HashMap` requires `K: Eq + Hash`; `BTreeMap` requires `K: Ord`. If you need a *different*
order than key order (say, by an id or a timestamp field), collect into a `Vec` and sort it —
do not rely on map iteration.

## 5. Iterator adapters

An **iterator** is a value that produces items one at a time. An **adapter** is a method that
returns another iterator; nothing runs until a consumer such as `collect`, `find`, `any`, or a
`for` loop pulls values through. This is called **lazy evaluation**.

```rust
let values = vec![3, 1, 2, 8];
let doubled: Vec<i32> = values.iter().map(|v| *v * 2).collect();
let evens: Vec<&i32> = values.iter().filter(|v| **v % 2 == 0).collect();
let first_big = values.iter().find(|v| **v > 5);
let any_big = values.iter().any(|v| *v > 5);
let all_positive = values.iter().all(|v| *v > 0);
let indexed: Vec<(usize, &i32)> = values.iter().enumerate().collect();

println!("{doubled:?} {evens:?} {first_big:?} {any_big} {all_positive} {indexed:?}");
```

Expected output:

```text
[6, 2, 4, 16] [2, 8] Some(8) true true [(0, 3), (1, 1), (2, 2), (3, 8)]
```

| Adapter | Produces |
|---|---|
| `map(f)` | One transformed item per input item |
| `filter(pred)` | Only items where `pred` is true |
| `find(pred)` | `Option<Item>` for the first match |
| `any(pred)` / `all(pred)` | `bool` |
| `enumerate()` | `(index, item)` pairs |
| `collect()` | A collection, inferred from the target type |

`filter` and `find` pass a **reference to the item** to the predicate. Since `.iter()` already
yields `&i32`, the closure receives `&&i32`; hence `**v`. `.any()` and `.all()` take `&i32`
directly, hence `*v`. Reading the compiler's "expected `&&i32`, found `&i32`" message is part
of learning this.

## 6. `sort_by_key` for stable list output

`sort_by_key` sorts in place using a key function. It is **stable**: items with equal keys keep
their original relative order. `sort_unstable_by_key` may be faster but may reorder ties.

```rust
let mut rows = vec![("b", 2), ("a", 1)];
rows.sort_by_key(|(label, _)| *label);
println!("{rows:?}");
```

Expected output:

```text
[("a", 1), ("b", 2)]
```

For deterministic output, choose a key with no ties — or add a secondary key — so stability
is not doing silent work for you. Sorting a `Vec` is a normal, explicit way to make list output
repeatable.

## 7. Slices: `&[T]`

A **slice** is a borrowed view of a contiguous sequence, with a length known at runtime. It is
written `&[T]`. A `&Vec<T>` coerces to `&[T]`, and so does `&[T; N]` (an array).

```rust
fn total(values: &[i32]) -> i32 {
    values.iter().sum()
}

let v = vec![1, 2, 3];
let a = [4, 5];
println!("{} {}", total(&v), total(&a));
```

Expected output:

```text
6 9
```

Taking `&[T]` instead of `&Vec<T>` makes a helper accept vectors, arrays, and subranges such as
`&v[1..3]` without changing its signature. It also tells the reader "I only borrow this; I do
not own it."

## 8. Building formatted output

`format!` creates a new `String` each call. `write!` and `writeln!` append to an existing
writer. For text output, the writer can be a `String` when you import `std::fmt::Write`.

```rust
use std::fmt::Write as _;

let mut out = String::new();
writeln!(out, "id: {}", 7).expect("writing to a String cannot fail");
write!(out, "status: {}", "planned").expect("writing to a String cannot fail");
println!("{out}");
```

Expected output:

```text
id: 7
status: planned
```

`writeln!` adds a newline; `write!` does not. They return `fmt::Result`; writing into a
`String` cannot fail, so an `expect` with that reason is one of the rare acceptable uses
(described in `04-errors-and-validation.md`). `use std::fmt::Write as _;` imports the trait
without introducing the name, which avoids a clash with `std::io::Write`. If both traits are
in scope by name, method syntax such as `out.write_str(...)` becomes ambiguous.

For plain concatenation without formatting, `out.push_str("text")` and `out.push('\n')` are
simpler and avoid a throwaway allocation.

## 9. Parsing integers: `parse::<i32>()` and `ParseIntError`

`str::parse` converts text into a type that implements `FromStr`. It **rejects surrounding
whitespace**, so trim first; it accepts an optional leading `+` or `-`.

```rust
for text in [" 42 ", "42", "x", "999999999999999999999999"] {
    println!("{text:?} -> {:?}", text.trim().parse::<i32>());
}
```

Expected output (the `Debug` text of the error is stable enough to recognize, but do not
parse it programmatically):

```text
" 42 " -> Ok(42)
"42" -> Ok(42)
"x" -> Err(ParseIntError { kind: InvalidDigit })
"999999999999999999999999" -> Err(ParseIntError { kind: PosOverflow })
```

`ParseIntError` distinguishes `InvalidDigit`, `Empty`, `PosOverflow`, and `NegOverflow`.
`04-errors-and-validation.md` shows how to turn it into a friendly message and how `?`
converts it into your own error type.

## 10. String escaping: a preview of JSON

JSON strings must be wrapped in double quotes, and any character with special meaning inside
them must be written as an escape sequence. A simple writer needs to handle at least:

| Source character | Written in a JSON string |
|---|---|
| `"` | `\"` |
| `\` | `\\` |
| newline (U+000A) | `\n` |
| tab (U+0009) | `\t` |
| other control characters below U+0020 | `\u00XX` |

Non-ASCII text may be emitted as-is in UTF-8. The reverse direction (reading escapes back into
characters) is the parser's job. That is a full design problem — grammar, byte-by-byte
scanning, and error positions — so it belongs in `06-json-without-crates.md`. Here you only
need the concept: **the bytes in memory are not necessarily the bytes in the file**.

---

## Common mistakes

- **Byte-slicing text at an arbitrary index.** `&s[0..1]` can panic on non-ASCII input. Use
  `chars()`, `get(..)`, or `char_indices()`.
- **Using `v[i]` when absence is normal.** `get(i)` returns `Option<&T>` and lets you report a
  clean error.
- **Consuming a vector and then reusing it.** `for x in v` moves it; `for x in &v` does not.
- **Assuming `HashMap` iteration order.** Use `BTreeMap` or sort a `Vec` when order matters.
- **Forgetting the extra reference in `filter`/`find` closures** (see §5).
- **Building output with repeated `+` in a loop.** Prefer `writeln!` into one `String`.
- **Importing both `std::fmt::Write` and `std::io::Write` by name.** Import one as `_`.
- **Assuming `len()` counts characters.** It counts bytes for strings, items for `Vec`.

---

> **Professional habit.** Write helpers against the weakest type that works: `&[T]` instead of
> `&Vec<T>`, `&str` instead of `&String`. Callers then keep their ownership and your function
> says exactly what it needs.

---

> **Professional habit.** If a human will read the output twice, make the order explicit.
> Sort with a named key and document it; never let a hash map decide your listing order.

---

## Apply it to Task Tracker

Questions only — no solution code here.

1. Which collection will hold your records while a command runs? What order does it need to
   preserve, and why?
2. What is the required order of `list` output? Which key will you sort by, and how will you
   break ties?
3. Which of your helper functions should take `&[Record]` rather than `&Vec<Record>`?
4. Where do you create owned `String`s from arguments, and where can you borrow `&str` from
   them instead?
5. How will you decide whether a byte range in a JSON string is safe to slice? Who performs
   unescaping before your record fields are filled?
6. Which JSON keys differ from your `snake_case` Rust field names, and which module owns the
   rename?
7. Where is the one place you parse an id from text? What happens to the parsed integer after
   that point — is it ever parsed twice?
8. How will you build a multi-line listing without repeated throwaway allocations, and which
   stream receives it?

## Checkpoint

- [ ] I can explain `len` versus `capacity`, and why `get` is safer than indexing.
- [ ] I can state what each of `.iter()`, `.iter_mut()`, and `.into_iter()` yields.
- [ ] I can convert between `String` and `&str` and explain why byte indexing needs care.
- [ ] I can choose `HashMap` or `BTreeMap` for a given requirement and justify it.
- [ ] I can chain `map`/`filter`/`find`/`any`/`all`/`enumerate` and collect the result.
- [ ] I can sort a list with `sort_by_key` and explain stable versus unstable sorting.
- [ ] I can write a function taking `&[T]` and pass it both a `Vec` and an array.
- [ ] I can build output with `writeln!` into a `String` and explain the `fmt::Write` import.
- [ ] I can parse text with `parse::<i32>()` and name what `ParseIntError` reports.

Next: `06-json-without-crates.md`
