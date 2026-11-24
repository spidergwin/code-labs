# Feature-by-Feature Build Plan

> **Purpose.** This is a behavioral specification for building the roadmap.sh Task Tracker CLI
> yourself. It says *what* each phase must do, *where* each responsibility belongs, and *how to
> prove* it works. It contains no finished implementation: no `Task` type, store, CLI handler,
> JSON codec, or CRUD function. Write those yourself. Prerequisites include
> `06-json-without-crates.md`, `08-time-without-chrono.md`, and `09-modules-and-architecture.md`.

## Why this matters for Task Tracker

A CLI that "mostly works" hides failures in the gaps: a file created at the wrong moment, an ID
that changes meaning after a rename, a timestamp that moves when nothing changed, a success line
printed to stderr. Rust exposes those gaps only when the boundaries are designed first. The two
std-only subsystems (JSON and time) must be tested in isolation before CRUD can use them;
otherwise every later bug could live in four different layers at once.

## What you will learn

- Turn a requirements list into ordered, independently verifiable phases.
- Decide behavioral policy (IDs, timestamps, filters, messages, exit codes) before coding.
- Separate responsibilities across modules without over-engineering a tiny crate.
- Test from a scratch directory so `tasks.json` never pollutes the repository.
- Use offline Rust documentation as a working tool, and commit in small, reviewable steps.

## 0. Rules that govern every phase

**The boundary.** Each phase names modules and their responsibilities, never their code. "Owns the
task representation" means *you* design that type using ideas from
`02-ownership-and-borrowing.md` and `03-structs-enums-and-match.md`.

**stdout, stderr, and exit codes.** Fix this convention in Phase 0 and never deviate:

| Situation | Stream | Exit |
|---|---|---|
| Success, including an empty list | stdout only | `0` |
| Usage: missing/extra args, unknown command/filter, unparsable ID | error + usage line, stderr | `2` |
| Runtime: unreadable/corrupt file, unwritable dir, clock failure | one clear error, stderr | `1` |
| Domain: unknown ID, invalid description | one clear error, stderr | `1` |

**Messages.** The spec fixes one exactly: `Task added successfully (ID: 1)`, with the assigned ID
substituted, on one stdout line. It does not fix the other wordings: choose them once (for example
`Task updated successfully (ID: 1)` and analogues for delete and both mark commands), record them
in the README, and test them. `list` prints one row per task, fields in a fixed labeled order,
sorted by ascending numeric ID; an empty result prints nothing and exits `0`.

**ID policy.** Use **max existing ID + 1**, or `1` for an empty list. A persisted counter avoids
reuse but needs an extra on-disk field and a migration story. With max+1, deleting the highest ID
means the next `add` reuses it; document that consequence. Reject out-of-range stored IDs.

**`updatedAt` rules.**

| Operation | `createdAt` | `updatedAt` | Rewrite? |
|---|---|---|---|
| `add` | capture current UTC time | the **same** value | yes |
| `update` with different description | unchanged | fresh UTC time | yes |
| `update` with identical description | unchanged | unchanged | no; success |
| status change via either mark command | unchanged | fresh UTC time | yes |
| mark to the status already held | unchanged | unchanged | no; success |
| `delete` / `list` / usage errors | n/a | n/a | delete only |

Use `YYYY-MM-DDTHH:MM:SSZ` from `08-time-without-chrono.md`; one clock read per operation makes
both `add` timestamps byte-for-byte equal.

**Status vocabulary and filters.**

| Stored status | Filter token |
|---|---|
| `todo` (default from `add`) | `list todo` |
| `in-progress` | `list in-progress` |
| `done` | `list done` |
| any | `list` (all, sorted) |

Exact lowercase matches only: `pending`, `in_progress`, `Done` are usage errors (exit `2`).

**JSON schema verification checklist.** Top level is an array (`[]` when empty); every element has
exactly `id`, `description`, `status`, `createdAt`, `updatedAt`; `id` is a positive integer without
quotes or leading zeros; `description` is a properly escaped string; `status` is one of the three
exact strings; timestamps match the format above; key order/indentation are fixed; UTF-8 has no
BOM; your parser round-trips your writer's output.

**Scratch-directory smoke test checklist.** Build once, run from a fresh directory so relative
paths resolve there (as explained in `LEARNING_PATH.md`):

```bash
SCRATCH=/tmp/opencode/task-tracker-smoke; mkdir -p "$SCRATCH" && cd "$SCRATCH"
cargo build --manifest-path /home/eliot/projects/code-labs/rust/starter-project/Cargo.toml
BIN=/home/eliot/projects/code-labs/rust/starter-project/target/debug/task-cli
"$BIN" add "Buy groceries"; cat tasks.json
```

Retest each phase in a new empty directory (or delete `tasks.json` first). Substitute your actual
binary name if you renamed the package. Cover: no file, corrupt file, unknown ID, every filter,
Unicode descriptions, and a read-only directory.

**Git commit cadence.** Commit after Phase 0, each of 1–3, each feature Phase 4–8, and Phase 9.
Messages name behavior (`Add list filters`), not mood. Before each commit run the daily baseline
in `12-professional-habits.md`. Journal three lines: what changed, worst error, clearest new idea.

**When to use offline docs.**

| Symptom | Command |
|---|---|
| Unsure of an API signature | `rustup doc --std` |
| Unsure of syntax | `rustup doc --reference` |
| Concept from an earlier chapter is fuzzy | `rustup doc --book` |
| Cargo options | `rustup doc --cargo`, `cargo help <command>` |
| Need standard-library source | `rustc --print sysroot`, then `lib/rustlib/src/rust/library` |

Read the whole compiler error before editing; Rust diagnostics are tutorials.

**Common mistakes.** Mixing errors into stdout; choosing IDs before deciding deletion behavior;
letting a display formatter invent timestamps; testing only in the crate directory where stale
state hides bootstrapping bugs.

> **Professional habit.** Put the policy tables above into your README *before* implementing them.
> Ambiguity gets resolved once, in writing.

### Apply it to Task Tracker

1. Which commands may create `tasks.json`? Which must never create it?
2. If two processes add a task in the same second, what do your timestamps guarantee?
3. Where is an error printed: where it is detected, or at the top level?

### Checkpoint

Why settle usage behavior before storage behavior?

## Phase 0 — Cargo skeleton, usage, and the bootstrap decision

**Goal.** A runnable crate that classifies arguments and reports usage correctly before any JSON,
time, or storage code exists.

**Files/modules involved.**
- `Cargo.toml`: package name, `edition = "2024"`, empty `[dependencies]`. Decide the name now.
- `src/main.rs`: entry point; turns a chosen outcome into printed output and an exit code.
- `src/cli.rs`: receives the argument vector; classifies it into a command plus raw positional args; owns usage text. No files, no ID parsing, no task knowledge.
- `src/lib.rs` (recommended): exposes modules to integration tests without launching the binary.
- **Bootstrap decision:** create `tasks.json` lazily on the first *store access* by a state-using command, never at startup and never on usage errors.

**Behavioral spec.** 1. No args: usage to stderr, exit `2`. 2. Unknown command: short error + usage to stderr, exit `2`. 3. Recognized names are exactly `add`, `update`, `delete`, `mark-in-progress`, `mark-done`, `list`. 4. `help`/`--help` if supported: usage to stdout, exit `0`. 5. Wrong argument count is a usage error from this phase on. 6. No command reads or writes files yet.

**Manual commands.**
```bash
cd /home/eliot/projects/code-labs/rust/starter-project && cargo check && cargo build
SCRATCH=/tmp/opencode/task-tracker-skeleton; mkdir -p "$SCRATCH" && cd "$SCRATCH"
BIN=/home/eliot/projects/code-labs/rust/starter-project/target/debug/task-cli
"$BIN"; echo "exit=$?"; "$BIN" frobnicate; echo "exit=$?"; "$BIN" help
ls -la                 # tasks.json must NOT appear
```

**Edge cases.** Empty command name, leading dash, surrounding whitespace, very long unknown token; match exactly, with no silent trimming or lowercasing.

**Acceptance criteria.**
- [ ] The six names are recognized; everything else is rejected.
- [ ] Usage/error routing and exit codes match section 0.
- [ ] No file appears in a fresh scratch directory.
- [ ] `cargo check`, `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings` pass.

**Checkpoint.** Why handle usage before storage?

## Phase 1 — Minimal JSON codec with tests (chapter `06`)

**Goal.** A tested std-only JSON reader/writer for exactly the shapes this project needs.

**Files/modules involved.**
- `src/json.rs` (or `src/json/mod.rs`): owns a small JSON value representation, decoder, encoder, and position-aware errors. Knows nothing about tasks, files, or time.
- In-module `#[cfg(test)]` tests: valid inputs, malformed inputs, escaping, round-trips; see `10-testing-and-debugging.md`.

**Behavioral spec.** 1. Decode/encode an empty array. 2. Decode an array of objects with the five schema value types. 3. Malformed input errors include a character offset and short reason. 4. Escape `"`, `\`, and control chars on write; unescape on read; preserve Unicode. 5. Reject duplicate keys, trailing commas, comments, single quotes, unquoted keys (document the duplicate-key rule). 6. Emit plain integers; leave ID range checks to the model boundary. 7. Fixed indentation, key order, and trailing-newline decision.

**Manual commands.**
```bash
cargo test json
cargo test json -- --nocapture
cargo test --all-targets
```

**Edge cases.** Empty file, BOM, CRLF, trailing whitespace, `null`, nested values in wrong fields, huge strings, invalid escapes like `\q`, numbers beyond your integer type.

**Acceptance criteria.**
- [ ] Each malformed case has a named passing test.
- [ ] Encode-then-decode preserves values.
- [ ] Errors carry position, not just `invalid JSON`.
- [ ] No dependency is added.

**Checkpoint.** Which JSON features are you deliberately skipping, and how will you detect them?

## Phase 2 — UTC time utility with tests (chapter `08`)

**Goal.** Convert epoch seconds to the required UTC string and back, std-only, with a fixed-clock test seam.

**Files/modules involved.**
- `src/time.rs`: epoch-second acquisition, civil-date conversion, zero-padded formatting, stored-timestamp validation, and an injectable instant for tests. No files, no tasks.
- In-module tests for known dates, leap years, and invalid strings.

**Behavioral spec.** 1. Format `YYYY-MM-DDTHH:MM:SSZ` with fixed widths. 2. UTC only; never the local timezone. 3. Reject wrong separators, missing `Z`, impossible dates, out-of-range fields. 4. Call the clock once per operation. 5. Tests use a fixed instant, never the wall clock.

**Manual commands.**
```bash
cargo test time
date -u +%Y-%m-%dT%H:%M:%SZ     # local sanity check only
```

**Edge cases.** Epoch `0`, pre-1970 (document support), leap day, 1900 vs 2000, `23:59:59` rollover, 64-bit storage to avoid 2038 issues.

**Acceptance criteria.**
- [ ] Known epochs format to expected strings in tests.
- [ ] Invalid timestamp strings are rejected clearly.
- [ ] Tests are reproducible offline.
- [ ] Only one module knows the layout.

**Checkpoint.** Why is a fixed clock better than comparing against `date -u` in a test?

## Phase 3 — Model, error type, and store bootstrap

**Goal.** Represent tasks and errors; make `tasks.json` appear with `[]` on first real store access.

**Files/modules involved.**
- `src/model.rs`: task representation, status type, ID type, field validation, mapping to exact JSON keys. No I/O.
- `src/error.rs`: one application error type with cases for I/O, JSON, arguments, ID, missing ID, status, description, clock; user-facing messages and conversions from lower errors. No printing.
- `src/store.rs`: load/save, in-memory collection, next-ID calculation, create-if-missing rule. Depends on JSON and model; does not print or choose exit codes.
- `src/main.rs`: calls dispatch and prints the error once.

**Behavioral spec.** 1. Missing file: create with `[]`, yield empty collection. 2. Existing file: parse it; corrupt content errors name the file and problem. 3. Malformed task fields (missing/extra key, bad status/timestamp) are errors, never skipped. 4. Only store-using commands trigger creation. 5. Saving writes deterministic order and exact schema. 6. Next-ID helper follows section 0.

**Manual commands.**
```bash
SCRATCH=/tmp/opencode/task-tracker-store; mkdir -p "$SCRATCH" && cd "$SCRATCH"
BIN=/home/eliot/projects/code-labs/rust/starter-project/target/debug/task-cli
"$BIN" list; cat tasks.json; echo "exit=$?"
"$BIN" frobnicate; ls                     # no new file
printf '{ broken' > tasks.json; "$BIN" list; echo "exit=$?"
```

**Edge cases.** A directory named `tasks.json`, unreadable file, empty file, object instead of array, unknown status string.

**Acceptance criteria.**
- [ ] Fresh directory gets exactly `[]`; a second `list` does not change it.
- [ ] Corrupt input gives one stderr line, exit `1`.
- [ ] Tests cover missing, empty, corrupt, and invalid-task cases.

**Checkpoint.** Why does the store, not the dispatcher, own "create if missing"?

## Phase 4 — `add <description>`

**Goal.** Create a task with a correct ID and equal creation/update timestamps.

**Files/modules involved.**
- `src/cli.rs`: recognizes `add`, requires exactly one positional argument.
- `src/model.rs`: constructs a valid task with status `todo`.
- `src/time.rs`: supplies the one instant.
- `src/store.rs`: computes next ID, appends, saves.
- `src/main.rs`: prints the exact success line.

**Behavioral spec.** 1. Store description as given; reject whitespace-only text, do not silently trim otherwise. 2. ID = max + 1 (or `1`). 3. Both timestamps come from one clock read. 4. Print `Task added successfully (ID: N)` to stdout, exit `0`. 5. Missing/extra args: usage error, exit `2`. 6. Keep a seam for atomic saves; Phase 9 completes it.

**Manual commands.**
```bash
SCRATCH=/tmp/opencode/task-tracker-add; mkdir -p "$SCRATCH" && cd "$SCRATCH"
BIN=/home/eliot/projects/code-labs/rust/starter-project/target/debug/task-cli
"$BIN" add "Buy groceries"; echo "exit=$?"
"$BIN" add 'Write "quotes" and a \\ backslash'
"$BIN" add "Über café 日本語"
cat tasks.json
"$BIN" add; "$BIN" add one two; echo "exit=$?"
```

**Edge cases.** Empty or spaces-only text, emoji, embedded newline, leading `-`, stored ID at integer limit.

**Acceptance criteria.**
- [ ] Exact message for the first task (ID `1`).
- [ ] All five properties present, status `todo`, equal timestamps.
- [ ] Second add gets ID `2`; bad arity changes nothing.

**Checkpoint.** Why read the clock once instead of once per field?

## Phase 5 — `list` and filters

**Goal.** Display all tasks or one exact status subset, without writing.

**Files/modules involved.**
- `src/cli.rs`: zero or one filter token, validated against section 0.
- `src/store.rs`: read-only view; no mutation path.
- `src/model.rs` / `src/time.rs`: already-validated display values.
- `src/main.rs`: rows in fixed order to stdout.

**Behavioral spec.** 1. `list`: all tasks by ascending ID. 2. Three filters return their exact subsets. 3. Tokens match exactly. 4. Empty result: no output, exit `0`. 5. No file change, including modification time. 6. Identical labeled field order on every row.

**Manual commands.**
```bash
SCRATCH=/tmp/opencode/task-tracker-list; mkdir -p "$SCRATCH" && cd "$SCRATCH"
BIN=/home/eliot/projects/code-labs/rust/starter-project/target/debug/task-cli
"$BIN" add "First"; "$BIN" add "Second"
"$BIN" list; "$BIN" list todo; "$BIN" list in-progress; "$BIN" list done
"$BIN" list pending; echo "exit=$?"
stat tasks.json
```

**Edge cases.** Uppercase filter, trailing space, two tokens, all tasks sharing one status.

**Acceptance criteria.**
- [ ] Each filter returns only matching tasks.
- [ ] Unknown filter gives usage text, exit `2`.
- [ ] Order is stable; listing never writes.

**Checkpoint.** Should status strings be validated in the filter parser, the model, or both?

## Phase 6 — `update <id> <description>`

**Goal.** Replace a description and change `updatedAt` only on a real change.

**Files/modules involved.**
- `src/cli.rs`: two positional args; shared ID parser.
- `src/model.rs`: description validation and "did it change?" decision.
- `src/store.rs`: find, mutate, save only on change.
- `src/main.rs`: one success message.

**Behavioral spec.** 1. Positive integer ID; malformed is usage error (exit `2`). 2. Valid unknown ID: error, exit `1`, naming the ID. 3. Different description updates text and `updatedAt`; `createdAt` untouched. 4. Identical description: successful no-op, no rewrite. 5. Empty/whitespace description: exit `1`. 6. Extra args: usage error.

**Manual commands.**
```bash
SCRATCH=/tmp/opencode/task-tracker-update; mkdir -p "$SCRATCH" && cd "$SCRATCH"
BIN=/home/eliot/projects/code-labs/rust/starter-project/target/debug/task-cli
"$BIN" add "Buy groceries"
"$BIN" update 1 "Buy groceries and milk"
"$BIN" update 1 "Buy groceries and milk"       # no-op
"$BIN" update 99 "Ghost"; "$BIN" update abc "Bad ID"; "$BIN" update 1
cat tasks.json
```

**Edge cases.** ID `0`, negative or `+1`, leading zeros, surrounding spaces, case-only changes, concurrent edits.

**Acceptance criteria.**
- [ ] `createdAt` is byte-for-byte unchanged.
- [ ] `updatedAt` changes only for a real change; no-op still exits `0`.
- [ ] Unknown and malformed IDs are distinguishable.

**Checkpoint.** Why is no-op *success* easier to defend than no-op error?

## Phase 7 — `delete <id>`

**Goal.** Remove exactly one task and report the result.

**Files/modules involved.**
- `src/cli.rs`: one argument, shared ID parser.
- `src/store.rs`: find, remove, save; owns the missing-ID error.
- `src/main.rs`: one success message.

**Behavioral spec.** 1. Existing ID: remove, save, success, exit `0`. 2. Unknown ID: error, exit `1` (never silent success). 3. Malformed/missing ID: usage error, exit `2`. 4. Highest-ID deletion may be followed by reuse (section 0); document it. 5. No other task's fields or timestamps change.

**Manual commands.**
```bash
SCRATCH=/tmp/opencode/task-tracker-delete; mkdir -p "$SCRATCH" && cd "$SCRATCH"
BIN=/home/eliot/projects/code-labs/rust/starter-project/target/debug/task-cli
"$BIN" add "One"; "$BIN" add "Two"
"$BIN" delete 1; "$BIN" list
"$BIN" delete 1; echo "exit=$?"        # already deleted
"$BIN" add "Three"; "$BIN" list; cat tasks.json
```

**Edge cases.** Empty store, only task, double delete, empty result saved as `[]` not blank content.

**Acceptance criteria.**
- [ ] Only the requested task disappears; last deletion leaves valid `[]`.
- [ ] Second delete errors with exit `1`.
- [ ] ID reuse matches the documented policy.

**Checkpoint.** What would break if delete touched remaining tasks' `updatedAt`?

## Phase 8 — `mark-in-progress <id>` and `mark-done <id>`

**Goal.** Change status with the same no-op and timestamp discipline as `update`.

**Files/modules involved.**
- `src/cli.rs`: two command names, one ID each, shared parser.
- `src/model.rs`: status type with three valid values and explicit transitions.
- `src/store.rs`: find, transition, save only on change.
- `src/main.rs`: one success message per command.

**Behavioral spec.** 1. Commands set `in-progress` and `done` respectively. 2. Neither sets `todo`; neither touches `createdAt`. 3. A real transition refreshes `updatedAt`. 4. Re-marking the current status is a no-op success. 5. Unknown ID: exit `1`; malformed: exit `2`. 6. Only the three exact lowercase strings reach disk.

**Manual commands.**
```bash
SCRATCH=/tmp/opencode/task-tracker-status; mkdir -p "$SCRATCH" && cd "$SCRATCH"
BIN=/home/eliot/projects/code-labs/rust/starter-project/target/debug/task-cli
"$BIN" add "Task A"; "$BIN" list todo
"$BIN" mark-in-progress 1; "$BIN" list in-progress
"$BIN" mark-done 1; "$BIN" list done
"$BIN" mark-done 1; "$BIN" mark-done 404; echo "exit=$?"
cat tasks.json
```

**Edge cases.** Done → in-progress (allowed unless you document otherwise), deleted ID, two marks in one second.

**Acceptance criteria.**
- [ ] Filters in `05-collections-and-strings.md` terms return expected subsets.
- [ ] `updatedAt` changes only on real transitions.
- [ ] Adding a new status later forces a compile error, not a silent fallback.

**Checkpoint.** Why model status as an enum rather than a plain string?

## Phase 9 — Polish: atomic writes, validation, README, tests, clippy

**Goal.** Make failures safe, docs complete, and the crate clean under every standard check.

**Files/modules involved.**
- `src/store.rs`: atomic replacement (temporary file in the same directory, flush, close, rename over target; clean up on failure). Decide and document fsync and permission preservation.
- `src/model.rs`, `src/cli.rs`, `src/json.rs`, `src/time.rs`: final boundary validation pass; no duplicated rules.
- `README.md`: install/build, commands, examples, JSON schema, timestamp format, errors/exit codes, development.
- `.gitignore`: build output and local `tasks.json`; never commit binaries or scratch data.
- Tests: unit tests per module plus integration tests launching the binary in a temp directory (`10-testing-and-debugging.md`).

**Behavioral spec.** 1. Interrupted writes never leave partially valid JSON. 2. Failed saves keep the previous file readable and report one clear error. 3. Every documented error case has a test checking message category and exit code. 4. Output is deterministic. 5. README matches actual behavior, including reuse and no-ops. 6. All checks below pass.

**Manual commands.**
```bash
cargo fmt && cargo fmt --check
cargo clippy --all-targets -- -D warnings
cargo check && cargo test && cargo build --release
```

**Edge cases.** Full disk, read-only directory, crash between temp creation and rename, stale temp file, permissions to preserve. Repeat section 0's scratch checklist with the release binary.

**Acceptance criteria.**
- [ ] Every schema checklist item verified against a real file.
- [ ] Every phase's commands work in a fresh scratch directory.
- [ ] All error paths exit non-zero with one clear line.
- [ ] For the CLI track, no external crates in `Cargo.toml`; `Cargo.lock` contains only your
      package. Chapter `15` changes this deliberately for the API track.
- [ ] The definition of done in `LEARNING_PATH.md` is satisfied.

**Checkpoint.** Which policy change now would force the most test and doc updates?

---

## Final checklist

- [ ] I made and documented bootstrap, ID, no-op, filter, and message decisions myself.
- [ ] Each phase passed its checks before I moved on; my journal has ten entries.
- [ ] Only state-using commands can create `tasks.json`.
- [ ] JSON schema and timestamp format were verified, not assumed.
- [ ] Success, usage, and runtime output are separated by stream and exit code.
- [ ] Commits are small and map to behaviors in this chapter.
- [ ] I can explain every module's ownership and error flow without notes.

## Next

Read `12-professional-habits.md` for the daily quality routine, and keep
`13-troubleshooting-and-cheatsheet.md` open while you work through the phases.
