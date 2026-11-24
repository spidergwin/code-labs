# Professional Habits

> **Purpose.** Rust rewards small, disciplined steps. This chapter turns "clean code" advice into
> a daily routine you can actually follow while building the Task Tracker CLI. Everything here
> uses the standard library and the tools already installed with Rust 1.99.1.

## Why this matters for Task Tracker

A tiny CLI is the best place to build habits: the entire crate fits in your head, so a lint
warning or a surprising clone is easy to investigate. If you let `unwrap()` creep into the file
store now, the same habit will hide a real bug when the file is corrupt. If output order depends on
a hash map today, your tests will fail randomly tomorrow. The habits below directly protect the
acceptance criteria in `11-feature-by-feature.md`.

## What you will learn

- Run a repeatable five-command baseline before every commit.
- Name functions, types, constants, and modules the way Rust code expects.
- Keep functions small and pull pure logic away from I/O.
- Return errors instead of panicking, add context, and print each error once.
- Borrow for reads, own for storage, and justify every clone.
- Document public items, explain *why* in comments, and produce deterministic output.
- Write a README that matches the code, plus pre-commit and self-review checklists.

## 1. The daily baseline

Run these from `starter-project/`, in this order, after every meaningful edit:

```bash
cargo fmt --check
cargo clippy --all-targets -- -D warnings
cargo check
cargo test
git status
```

| Command | What it catches | When to run |
|---|---|---|
| `cargo fmt --check` | Formatting drift, inconsistent indentation | Before every commit |
| `cargo clippy --all-targets -- -D warnings` | Idiom problems, needless clones, suspicious patterns, including in tests | Before every commit |
| `cargo check` | Type and borrow errors, faster than a full build | After each edit |
| `cargo test` | Behavioral regressions | After each edit and before merge |
| `git status` | Forgotten files, accidental `tasks.json`, compiled binaries | Before staging |

Run `cargo fmt` (without `--check`) to apply formatting, then re-run the baseline. `--all-targets`
matters: it lints test code too. `-D warnings` turns warnings into failures, so the bar stays high.

> **Professional habit.** A red baseline is a stop sign, not a suggestion. Fix the warning before
> adding the next feature; warning debt compounds quickly in Rust.

## 2. Idiomatic naming

| Item | Convention | Example shape |
|---|---|---|
| Functions, variables, modules, methods | `snake_case` | `parse_args`, `load_tasks`, `next_id` |
| Types: structs, enums, traits | `UpperCamelCase` | `Task`, `Status`, `AppError` |
| Constants and `static` items | `SCREAMING_SNAKE_CASE` | `MAX_DESCRIPTION_LEN` |
| Enum variants | `UpperCamelCase` | `InProgress`, `NotFound` |
| Test functions | descriptive `snake_case` sentence | `add_rejects_blank_description` |

Avoid **stuttering**, where the module name is repeated in the type name. `task::TaskService`
reads the name twice; prefer a module named `service` with `Service`, or a module named `task`
with `Task`. Name things by responsibility: a module that owns persistence is `store`, not
`task_store_manager`. Booleans read well with `is_`/`has_`; functions that may fail often start
with `try_`.

```rust
const MAX_DESCRIPTION_LEN: usize = 500;
fn is_valid_status(raw: &str) -> bool { /* ... */ }
```

Acronyms follow Rust style too: `Id`, not `ID`, inside type and variant names, while the JSON key
stays exactly `id` because the file format demands it. Do not rename on-disk fields to satisfy a
style rule.

## 3. Small functions and pure helpers

A good function has one job, a name that describes its result, and arguments that fit on one line.
When a function mixes *deciding* and *doing*, split it. A **pure function** reads its inputs and
returns a value without I/O, global state, or clocks; it is trivial to test. An **impure function**
reads a file, prints, or asks the time.

For this project, keep these concerns separate: argument classification (pure), ID computation
from a slice of tasks (pure), status transition on a task (pure), loading/saving files (impure),
and printing/exit codes (impure). Then "does the next ID skip deleted entries?" becomes a test of
a pure helper instead of a scratch-directory experiment.

Heuristics, not laws: a function over roughly 30 lines or with more than three nested levels is
worth reviewing. Deep `match` pyramids usually want a helper or an enum method. Prefer early
returns to indentation. Keep module boundaries aligned with the phase responsibilities in
`11-feature-by-feature.md`; do not create a file for every two-line function.

## 4. Error discipline

- No `unwrap()` or `expect()` in production paths. A bad file or argument must produce a clear error, not a panic with a backtrace.
- Tests may use them when the test itself guarantees the value, but prefer `?` in test functions returning `Result` where it reads well.
- Return `Result<T, AppError>` from fallible helpers; use `?` to propagate.
- Add **context** at each boundary that knows more: "could not read tasks.json: permission denied", not just "permission denied".
- Decide that only `main` prints. Lower layers return errors; `main` prints one line to stderr and chooses the exit code from section 0 of `11-feature-by-feature.md`.
- Never print and then return the same error: that is the classic double message.
- Distinguish usage errors (exit `2`) from runtime and domain errors (exit `1`) in your error type, not in scattered `if` statements.

```rust
let n = text.parse::<u32>().map_err(|e| format!("bad number: {e}"))?;
```

Implement `Display` for your error type so `{}` gives a human sentence, and `std::error::Error`
so it composes with standard machinery. Use `From` implementations for mechanical conversions
between low-level errors and your type, but add context manually where a bare conversion would
lose the file path or command name. See `04-errors-and-validation.md`.

## 5. Ownership ergonomics

**Borrow for reads, own for storage, clone only with a reason.** A `&str` parameter accepts both
string literals and `String` values; a `String` parameter forces the caller to give up ownership.
Returning borrowed data from a function that creates it is impossible, so return an owned `String`
in that case.

| Situation | Prefer | Why |
|---|---|---|
| Function only inspects text | `&str` | No allocation, caller keeps the value |
| Function inspects a task list | `&[Task]` | Accepts arrays, slices, and `Vec` without moving |
| Function needs to mutate one task | `&mut Task` | Explicit, single-writer access |
| Function stores text beyond the call | `String` | The data must outlive the borrow |
| Function appends to a collection it owns | `&mut Vec<_>` | Avoids returning a whole new vector |
| Two owners genuinely needed | `Rc`/`Arc` (std) | Explicit shared ownership; avoid in a tiny single-threaded CLI |

Before adding `.clone()`, ask: can I pass a reference, restructure the loop, or take the value by
value instead? The clones worth keeping are the ones where the alternative is more confusing code
or a borrow that cannot be satisfied. Rust's borrow checker is telling you about real data flow,
so listen to it before silencing it. See `02-ownership-and-borrowing.md`.

## 6. Documentation and comments

Use `//!` at the top of `src/lib.rs` (and optionally `src/main.rs`) for crate docs: what the crate
does and where the on-disk format is specified. Use `//!` at the top of each module file to state
its responsibility. Use `///` on every public item: public types, functions, and methods.

```rust
//! Task Tracker CLI: std-only task management.
//! The on-disk schema is documented in README.md.

/// Returns the next id, or `None` when there are no tasks.
```

Doc comments are more than prose: they are markdown, and fenced examples inside them are compiled
and run by `cargo test`. A short example that shows how to call a public helper is a test you get
for free. Write docs as contracts: what the function returns, what it rejects, and which errors it
can produce. Avoid restating the signature in words; say what the caller needs to know.

Comments inside functions explain **why**, not **what**. Good reasons include a subtle ordering
requirement, a format constraint imposed by the spec, or a workaround for a platform behavior:

```rust
// Rename after flush: on some platforms an open file cannot be replaced.
```

If the code needs a comment to explain what it does, first consider renaming the code. If a
comment explains a decision, keep it and include the consequence of ignoring it.

## 7. Deterministic output

Determinism means the same inputs produce the same bytes on every run and machine. For this CLI
that covers stdout, error messages, and `tasks.json`.

- Sort tasks by numeric ID before listing and before saving.
- When keys or groups must be ordered, use `BTreeMap` (sorted) rather than `HashMap` (unspecified order); see `05-collections-and-strings.md`.
- Emit JSON keys in a fixed order from your writer; never iterate a hash map to build objects.
- Use the fixed-clock seam from `08-time-without-chrono.md` in tests; never assert against wall-clock time.
- Do not print pointers, addresses, iteration order of a hash set, or locale-dependent formatting.
- Keep timestamps second-precision UTC with `Z`, so the same instant formats identically everywhere.

Random-looking test failures are usually hidden nondeterminism: a hash iteration order, a real
clock read, or a test sharing a file with another test. Give each test its own temporary directory.

## 8. Atomic writes and idempotency

An **atomic write** makes replacement all-or-nothing from the reader's perspective. The pattern:
write to a temporary file in the *same directory*, flush and close it, then rename it over the
target. Same directory matters because rename across filesystems may not be atomic. Clean up a
stale temporary file on failure and keep the original file untouched until the rename succeeds.

**Idempotency** means repeating an operation has the same effect as doing it once. Apply it to the
no-op rules in `11-feature-by-feature.md`: re-running an identical `update` or a mark command that
changes nothing leaves both the data and `updatedAt` alone, yet still reports success. `delete` is
deliberately *not* idempotent there: deleting a missing ID is an error so typos are visible.
Document whichever choice you make; surprises come from inconsistent choices, not from the choice
itself.

## 9. Validate at boundaries

Validate data when it crosses into your system, then trust internal invariants:

| Boundary | Validate |
|---|---|
| CLI arguments | command name, argument count, filter token, ID syntax |
| JSON file | structure, key set, types, status strings, timestamp format, ID range |
| Clock | that a timestamp could be obtained and formatted |
| Description input | non-empty, non-whitespace, documented length if any |

Do not re-validate the same status string in three layers. Let the JSON decoder validate JSON
syntax, let the model validate task fields, and let the CLI validate argument shapes. When a layer
accepts invalid data, the error should point at the layer that knew enough to reject it.

## 10. README structure for this project

Write the README as a contract for a user who has never seen the code:

1. **Title and one-paragraph purpose.** What the CLI does and that its track uses no external crates.
2. **Install/build.** Rust version, `cd starter-project`, `cargo build --release`, where the binary is.
3. **Commands.** A table of `add`, `update`, `delete`, `mark-in-progress`, `mark-done`, `list`, and all three filters, with argument shapes.
4. **Examples.** A short bash session showing add, list, mark, and filter, with expected output lines.
5. **JSON schema.** The five keys, their types, exact status strings, and the ID-reuse rule.
6. **Timestamp format.** `YYYY-MM-DDTHH:MM:SSZ`, UTC, second precision, and the `updatedAt` rules table.
7. **Errors and exit codes.** The stdout/stderr split and the `0`/`1`/`2` convention from `11-feature-by-feature.md`.
8. **Development.** `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings`, `cargo test`, and how to run smoke tests from a scratch directory.

Keep examples copy-pasteable and verify them after Phase 9. An inaccurate README is worse than a
short one.

## 11. Git hygiene

One behavior per commit: "Add JSON writer", "Add delete command", "Document exit codes". A commit
that mixes formatting, a refactor, and a feature is hard to review and hard to revert. Stage
explicit files instead of `git add -A`.

```gitignore
/target/
tasks.json
```

`target/` holds build artifacts and must never be committed; `tasks.json` is local runtime state
created by smoke tests. Never commit compiled binaries, temporary scratch data, or editor backups.
If you rename the package, commit the name change together with the README update.

## 12. Performance mindset for a tiny CLI

Do not optimize before measuring. A task list with a few hundred entries makes almost every
operation negligible. Clarity and correctness win. Still, know the basics:

- `Vec` stores elements contiguously and grows by allocating a larger buffer and moving elements. Growth is amortized, so repeated `push` is cheap; `Vec::with_capacity(n)` helps only when you know `n`.
- `HashMap` gives average constant-time lookup with no ordering guarantee; `BTreeMap` gives ordered lookup in logarithmic time. For deterministic JSON output, prefer `BTreeMap` or a sorted `Vec`.
- Indexing a `Vec` is O(1); scanning for an ID is O(n). With small n, scanning is fine and simpler than maintaining an index.
- Cloning a `String` allocates and copies; cloning a whole task list in a loop is a real cost. Pass references instead.
- Formatting and file I/O dominate this program. Reduce needless reads/writes—one load and one save per command—rather than micro-optimizing arithmetic.

If a change makes the code harder to read without a measured benefit, do not make it. When you do
need evidence, measure with `cargo build --release` and a repeatable workload; do not guess.

## 13. Pre-commit checklist

- [ ] `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings`, `cargo check`, `cargo test` all pass.
- [ ] A fresh scratch directory smoke test confirms the changed behavior.
- [ ] No `unwrap`/`expect` or `println!` was added to library modules.
- [ ] New behavior has a test and a README line where user-visible.
- [ ] `git status` shows only intended source, test, and doc files.
- [ ] Commit message names the behavior; build journal updated.

## 14. Self code-review checklist

- [ ] Can I state each module's single responsibility in one sentence?
- [ ] Does every public item have a doc comment, and every non-obvious choice a *why* comment?
- [ ] Are errors returned with context and printed exactly once?
- [ ] Are IDs, timestamps, filters, and no-ops handled exactly as documented?
- [ ] Are reads borrowed, stored values owned, and clones justified?
- [ ] Would two runs on the same input produce identical bytes?
- [ ] What edge case would break this if the file were hand-edited?

### Common mistakes

- Running only `cargo check` and assuming clippy and tests will pass.
- Adding a dependency "just for convenience" and quietly breaking the std-only constraint.
- Treating a warning as cosmetic because the program still runs.
- Writing README prose from memory instead of from the tested behavior.

> **Professional habit.** End each session by running the baseline and writing one journal line
> about the warning or borrow error you fixed. Six months from now, that journal is your fastest
> reference.

### Apply it to Task Tracker

1. Which functions in your crate are pure, and which tests do they make easy?
2. Where does your code currently clone, and can any of those clones become borrows?
3. Which output could vary between two runs on the same data, and how would you pin it down?
4. Does your README state exit codes and the ID-reuse rule in the user's language?

### Checkpoint

Explain, without running anything, why `HashMap` iteration order can break a JSON round-trip test
and how you would prevent it.

## Next

Keep `13-troubleshooting-and-cheatsheet.md` beside you while coding, and return to
`11-feature-by-feature.md` for the next phase's acceptance criteria.
