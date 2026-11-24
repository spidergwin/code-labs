# 09 — Modules and Architecture

This chapter is about drawing lines inside the crate: which code lives together, which names are
visible where, and which modules are allowed to depend on which. The goal is a skeleton that stays
pleasant to work in as the CLI grows.

## Why this matters for Task Tracker

A one-file program can implement the whole roadmap.sh spec, but it becomes hard to test and hard
to change. You will write your own JSON codec, your own UTC time conversion, file handling, and
the command logic. If all of that sits in `main.rs`, every small change recompiles and re-reads the
same giant mental model. Worse, tests can only reach whatever you made public.

Splitting into modules lets you say: "the CLI knows about arguments but not about files", "the JSON
codec knows about bytes but not about cwd", "the service knows the rules but not the storage".
Those statements are testable design decisions. This chapter gives you the vocabulary to make them.

## What you will learn

- The difference between a **crate** and a **module**, and how the module tree is formed
- How `mod` declarations map to `src/foo.rs` and `src/foo/mod.rs`
- Rust's default privacy, `pub`, `pub(crate)`, `pub(super)`, and `use`
- The meaning of `crate::`, `self::`, and `super::` paths
- How `pub use` re-exports create a stable public surface
- Why a binary crate plus a thin library crate is better than a fat `main.rs`
- Job descriptions for the modules in this project (no implementation given)
- Defining traits at the consumer, generics `S: Store` versus `&dyn Store`, and dependency injection
- How to avoid circular module dependencies
- Where `#[cfg(test)] mod tests` belongs, and how storage can be swapped later

---

## 1. Crates and modules

A **crate** is one compilation unit: either a binary (has `main`) or a library (has a public API).
Cargo builds a crate from one **crate root** file: `src/main.rs` for a binary, `src/lib.rs` for a
library. A **module** is a namespace inside a crate. Modules form a tree whose root is the crate
root.

> **Jargon.** A **namespace** is a named scope in which names like `Note` or `save` are unique.
> Two modules may each define a private helper called `save` without conflict.

A package may contain both roots:

```text
starter-project/
├── Cargo.toml
└── src/
    ├── main.rs   # binary crate root
    └── lib.rs    # library crate root (you add this when ready)
```

Then `cargo build` produces two targets: the library and the executable. The binary refers to the
library through the package name, with hyphens converted to underscores. If the package were named
`task-cli`, the binary would write `use task_cli::...`.

### `mod` declarations and file paths

A `mod` declaration both declares a module and tells Rust where to find its source.

```text
src/
├── main.rs          # `mod notes;` looks for ...
├── notes.rs         # ... this file
└── notes/
    ├── model.rs     # `mod model;` inside notes.rs
    └── format.rs    # `mod format;` inside notes.rs
```

Rules:

| Declaration | Rust looks for | Notes |
|---|---|---|
| `mod notes;` in `src/lib.rs` | `src/notes.rs` or `src/notes/mod.rs` | Preferred: `notes.rs` |
| `mod notes;` in `src/notes.rs` | `src/notes/notes.rs` or `src/notes/notes/mod.rs` | Usually not what you meant |
| `mod format;` in `src/notes.rs` | `src/notes/format.rs` or `src/notes/format/mod.rs` | Child lives in a directory named after the parent |
| `pub mod notes;` | Same paths, plus externally visible | `pub` changes visibility, not lookup |

`src/foo/mod.rs` is the older layout; `src/foo.rs` plus a `src/foo/` directory for its children is
the modern style and works in edition 2024. Do not provide both `foo.rs` and `foo/mod.rs` for the
same module — the paths are ambiguous.

A `mod` declaration must be present somewhere in an ancestor for a file to be compiled at all.
Creating `src/format.rs` without `mod format;` in the root means the file is silently ignored. That
surprises everyone once.

---

## 2. Privacy and paths

By default, an item is **private**: visible in the module where it is defined and in that module's
descendants. It is *not* visible to the parent or to siblings. This is the opposite of what many
beginners expect.

```rust
// src/notes.rs
pub struct Note {
    pub title: String,
    body: String,          // private outside `notes` and its children
}

impl Note {
    pub fn new(title: String) -> Self {
        Self { title, body: String::new() }
    }

    pub fn body(&self) -> &str {
        &self.body
    }
}

fn internal_nonce() -> u8 { 7 }   // private to `notes`

pub fn nonce() -> u8 { internal_nonce() }
```

From the binary crate root:

```rust
use task_cli::notes::Note;   // `notes` must be `pub mod` in lib.rs

fn main() {
    let note = Note::new("Remember milk".to_string());
    println!("{}", note.title);     // pub field: fine
    println!("{}", note.body());    // pub method: fine
    // println!("{}", note.body);  // error E0609: field is private
}
```

Visibility modifiers:

| Modifier | Visible to |
|---|---|
| none (private) | The defining module and its descendants |
| `pub(super)` | The parent module and its descendants |
| `pub(crate)` | Anywhere in the same crate |
| `pub` | Any module that can name the path, including other crates |

`pub` on a struct does not make its fields public; each field has its own visibility. You cannot
re-export a private type as `pub` (compiler error E0365) — make the type `pub` first.

### `crate::`, `self::`, and `super::`

- **`crate::`** starts at the current crate root. `crate::notes::Note` works from any module in
  the same crate.
- **`self::`** starts at the current module. `self::format::label` means "the `format` child of
  this module".
- **`super::`** starts at the parent module. `super::Note` means "the `Note` in my parent".

```rust
// src/notes/format.rs
use super::Note;   // parent module `notes`

pub(super) fn label(note: &Note) -> String {
    note.title.clone()
}
```

Back in `src/notes.rs`:

```rust
mod format;
use self::format::label;   // `label` is pub(super), so the parent may use it

fn example(note: &Note) -> String {
    label(note)
}
```

In edition 2018 and later, `use` paths must begin with a crate name, `crate`, `self`, or `super`
— not with an arbitrary sibling module name. This is why `use crate::notes::Note` is the default
style. `use` does not copy code; it brings an existing name into the current scope.

### `pub use` re-exports

```rust
// src/lib.rs
mod notes;
pub use notes::Note;   // callers write `task_cli::Note`
```

A **re-export** makes a private-path item available at a public path. This gives you a **façade**:
a stable front door. You can later move `Note` to `src/task/model.rs` and update one `pub use`
line instead of every caller. Re-export the small set you intend users (including your binary and
tests) to rely on.

> **Professional habit.** Keep the module tree private by default and promote only a few names
> with `pub use`. A small public surface is easier to preserve than a large one.

---

## 3. Binary plus library, and a thin `main`

Putting everything in `src/main.rs` means:

- Unit tests in the binary can reach private items, but there is no library for integration tests
  to import.
- The command loop, file I/O, JSON parsing, and formatting are tangled together.
- You cannot reason about "the rules" without reading argument handling.

With both roots, the binary becomes a **thin main**: collect arguments, call one library function,
print the error, choose the exit code.

```rust
// src/main.rs — shape only, no project logic
use std::process::ExitCode;

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().collect();

    match task_cli::run(&args) {
        Ok(message) => {
            println!("{message}");
            ExitCode::SUCCESS
        }
        Err(message) => {
            eprintln!("error: {message}");
            ExitCode::from(2)
        }
    }
}
```

`src/lib.rs` owns everything else and exposes `run` (or whatever entry point you choose). The
binary is now a **adapter** between the shell and the library: it translates argv and exit codes,
and nothing more. See `07-filesystem-and-cli.md` for the argument and exit-code details, and
`04-errors-and-validation.md` for error design.

> **Professional habit.** If `main.rs` grows past "parse, call, report", something belongs in the
> library. Keep the boundary boring.

---

## 4. Job descriptions for this skeleton

These are responsibilities, **not code**. Decide the names, signatures, and files yourself while
following `11-feature-by-feature.md`.

| Module | Job description | Must not |
|---|---|---|
| `cli` | Turn `argv` into a command value; own usage/help text and arity checks | Know about JSON, files, or IDs in storage |
| `task/model` | Define the record, its status values, and validation of user-supplied fields | Open files or call the clock |
| `service` | Own the rules: next ID, not-found errors, which fields change on update, filtering | Know whether storage is a file |
| `store/json_store` | Locate the data file, create it when missing, load bytes, replace atomically | Know the meaning of each command |
| `json` (codec) | Parse and serialize JSON text, including escapes and error positions | Open files or know about cwd |
| `time` (util) | Convert epoch seconds to UTC strings and back; provide a clock abstraction | Format command output |
| `errors` | One application error enum with `Display`, `Error`, and `From` conversions | Contain command logic |

Dependency direction (arrows mean "uses"):

```text
main ──> cli ──> service ──> model
                    │
                    ├──> errors   (used by all modules)
                    ├──> Clock trait   (defined here, implemented elsewhere)
                    └──> Store trait   (defined here, implemented elsewhere)

json_store implements Store and Clock's sibling "system clock"
       └──> json codec
```

The important rule: `service` never names `json_store`. The implementation module imports the
trait; the consumer module only knows the trait. That is **dependency inversion** — the arrow
points toward the abstraction.

---

## 5. Traits at the consumer

A **trait** is a named set of methods that types may implement. A well-worn Rust guideline is:
**define the trait in the module that uses it**, not in the module that happens to provide the first
implementation. The service decides what it needs from storage; a store implements that contract.

```rust
// In the "service" module — a generic Note example, not project code
pub trait NoteSink {
    fn save(&self, text: &str) -> Result<(), String>;
}

pub fn archive<S: NoteSink>(sink: &S, text: &str) -> Result<(), String> {
    sink.save(text)
}

// In a different module
struct MemorySink;

impl NoteSink for MemorySink {
    fn save(&self, _text: &str) -> Result<(), String> {
        Ok(())
    }
}
```

`S: NoteSink` is a **trait bound**: a constraint that the generic type `S` implements `NoteSink`.
The service can call `sink.save` without knowing the concrete type. You can implement a trait for a
type from another module as long as either the trait or the type is local to your crate.

### Generics versus trait objects

```rust
fn archive_generic<S: NoteSink>(sink: &S, text: &str) -> Result<(), String> {
    sink.save(text)
}

fn archive_dynamic(sink: &dyn NoteSink, text: &str) -> Result<(), String> {
    sink.save(text)
}
```

`&dyn NoteSink` is a **trait object**: a fat pointer carrying a data pointer and a **vtable** (a
table of function pointers resolved at runtime). Using a trait object requires the trait to be
**object-safe**: no generic methods and no methods that return `Self` by value, for example.

| Approach | Dispatch | Cost | Strength |
|---|---|---|---|
| `S: NoteSink` / `impl NoteSink` | Static (**monomorphization**: one compiled copy per concrete type) | No vtable; possibly larger binary | Keep one concrete type per call site |
| `&dyn NoteSink` | Dynamic through vtable | One pointer indirection | Mix different implementations behind one reference |

For this CLI a generic parameter is simpler and fast; reach for `dyn` only when you genuinely need
heterogeneous implementations at runtime. Both keep the service independent of the concrete store.

### Dependency injection for the store and the clock

**Dependency injection** means passing collaborators into a component instead of constructing them
inside it. If the service calls `SystemTime::now()` directly, a test cannot control time and must
sleep or accept fuzzy assertions. Instead:

```rust
pub trait Clock {
    fn now_epoch_secs(&self) -> i64;
}

pub fn stamp(clock: &impl Clock) -> i64 {
    clock.now_epoch_secs()
}
```

The real clock wraps `SystemTime::now()`; a test clock returns a fixed number. `main` is the
**composition root**: the one place that builds the real store and real clock and hands them to the
service. Everything below that point depends on abstractions. See `08-time-without-chrono.md` for
the UTC conversion and `10-testing-and-debugging.md` for deterministic test doubles.

> **Professional habit.** `main` wires; modules work. If a module constructs its own
> dependencies, you cannot test it in isolation.

---

## 6. Circular dependencies

Rust permits two modules to refer to each other, so the compiler will not necessarily stop you.
A **circular dependency** is still a design smell: you cannot understand or test either module
without the other, and changes ripple around the loop.

If `model` seems to need the service and the service needs `model`, ask:

1. Is the shared item really a plain data type? Move it to a leaf module such as `task/model`.
2. Is one side only calling a *capability*? Define a trait in the consumer and implement it in the
   other module.
3. Is formatting mixed with rules? Move formatting into `cli`.

Keep helpers at the leaves and policies in the middle. A useful test: list each module's
`use crate::...` lines; if the list has a cycle, redraw it.

---

## 7. Tests as part of the layout

`#[cfg(test)]` compiles an item only when building tests. Placing `mod tests` inside the same
file gives a **unit test** that can see private items through `use super::*`:

```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn nonce_is_exposed_through_a_public_helper() {
        assert_eq!(nonce(), 7);
    }
}
```

An **integration test** lives in `tests/` at the package root. It is a separate crate and can only
use the library's public API, which is exactly why the library root matters. Use in-file unit tests
for private helpers and `tests/` for end-to-end behavior. Details are in
`10-testing-and-debugging.md`.

> **Professional habit.** Put the test next to the code it describes. When you delete or rename a
> module, its tests move with it.

---

## 8. Common mistakes

| Mistake | Result | Fix |
|---|---|---|
| Creating `src/foo.rs` without `mod foo;` | File silently ignored | Declare the module in an ancestor |
| Both `foo.rs` and `foo/mod.rs` | Ambiguous module path | Pick one layout |
| Expecting a child's private item to be usable by the parent | E0609 / privacy error | `pub(super)` or `pub` |
| `use notes::Note;` in edition 2024 | Unresolved import | Use `crate::notes::Note` or `self::` |
| Re-exporting a private type | E0365 | Make the type `pub` first |
| Everything in `main.rs` | Untestable rules, tangled concerns | Add `lib.rs` and a thin main |
| Defining `Store` inside `json_store` | Consumer depends on implementation | Define the trait where it is consumed |
| Service constructs its own store/clock | Tests need real files and real time | Inject collaborators |
| Modules importing each other in a loop | Hard-to-follow design | Move shared types to leaves; invert dependencies |

---

## 9. Apply it to Task Tracker

Do not write project code here. Write answers, sketches, and a dependency diagram in your journal.

- [ ] Which module in the job-description table would you create **first**, and why?
- [ ] For each module, write one sentence starting "This module is allowed to know..." and one
      starting "This module must never know...".
- [ ] Where would you place the trait that the service uses for storage: next to the service or
      next to the JSON store? Explain your choice in terms of dependency direction.
- [ ] Which operations need the current time? How would you inject a clock into them?
- [ ] Which items should be private, `pub(crate)`, and re-exported from `lib.rs`?
- [ ] Draw your `use crate::...` graph and check it for cycles.
- [ ] What would change in `main.rs` if you later replaced the JSON store with a different one?
- [ ] Which private helper is worth an in-file `#[cfg(test)] mod tests` first?

---

## Checkpoint

- [ ] I can define crate, module, crate root, and module tree without looking them up.
- [ ] I can place `mod foo;` and predict whether Rust looks in `src/foo.rs` or `src/foo/mod.rs`.
- [ ] I can explain why a parent cannot see a child's private items, and name `pub(super)`.
- [ ] I can distinguish `crate::`, `self::`, and `super::` paths.
- [ ] I can explain what `pub use` buys you and what E0365 means.
- [ ] I can state why a thin binary plus a library is better than a single `main.rs`.
- [ ] I can describe the job of each proposed module without describing its code.
- [ ] I can explain trait-at-consumer, `S: Trait`, `&dyn Trait`, and dependency injection.
- [ ] I can spot a circular dependency and name two ways to break it.
- [ ] I know where unit tests and integration tests live.

**Next:** `10-testing-and-debugging.md` — verify each module with std-only tests, deliberate temp
directories, and a repeatable debugging workflow.
