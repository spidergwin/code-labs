# 08 — Feature by feature

> **Jargon first.** A *behavioral spec* says what the program must do, in plain language,
> before you write code. An *acceptance criterion* is a check that must pass before you move
> on. An *edge case* is unusual but legal input that reveals bugs. An *atomic replace* writes
> a complete new file and swaps it into place, so a crash cannot leave half-written JSON.

## Why this matters for Task Tracker

You now have the vocabulary: arguments (`05-cli-arguments-and-ux.md`), errors
(`03-errors-and-validation.md`), files (`04-json-and-filesystem.md`), time
(`06-time-and-timestamps.md`), and layout (`07-architecture-and-project-layout.md`). This
chapter turns it into an ordered build plan. No implementation code appears here — you write
it. **Do not skip ahead**: a bug found in phase 1 costs minutes; the same bug found after
five features costs hours.

## What you will learn

- Eight small phases with specs, manual commands, edge cases, and checkpoints.
- An ID policy and exactly when `updatedAt` changes.
- How the three status filters map to stored statuses.
- Which messages the requirements fix and which you must design.
- How to verify the JSON schema and test safely in a scratch directory.
- A commit cadence and when to ask for help.

---

## 0. Ground rules for the build

1. **Work in a scratch directory.** Build a binary, then run *that binary* from a throwaway
   directory so `tasks.json` never lands in the repository by accident:
   ```text
   go build -o /tmp/opencode/task-cli ./cmd/cli     # from the project root
   mkdir -p /tmp/opencode/task-tracker-scratch
   cd /tmp/opencode/task-tracker-scratch
   /tmp/opencode/task-cli list
   ```
   `go run ./cmd/cli ...` from the project root creates `tasks.json` in the project root,
   because `go run` uses the current directory. Fine for one final demo, bad as a test loop.
2. **Never test against real data.** Keep real tasks in a different directory from scratch.
3. **Inspect the file after every mutation.** `cat tasks.json` and check names, types, and
   timestamps before moving on.
4. **Spec → code → run → inspect → checkpoint → commit.** Rewrite the spec in your own words.
5. **Keep the build journal:** what changed, what broke, how you fixed it, what you learned.

## 1. Command-to-phase map

| Command | First implemented | Refined in |
|---|---|---|
| usage / no command / unknown command | Phase 0 | Phase 6 |
| `add "<description>"` | Phase 1 | Phase 6 (validation) |
| `list` | Phase 2 | Phase 6 (output) |
| `list todo`, `list in-progress`, `list done` | Phase 2 | Phase 6 |
| `update <id> "<description>"` | Phase 3 | Phase 6 (validation) |
| `delete <id>` | Phase 4 | Phase 6 |
| `mark-in-progress <id>`, `mark-done <id>` | Phase 5 | Phase 6 |

Every phase must leave earlier commands working. If `add` breaks while you implement
`delete`, fix the regression before continuing.

## 2. Decisions to make before Phase 1

Write your answers down once, then follow them.

### ID generation

| Strategy | Behavior | Tradeoff |
|---|---|---|
| Max existing + 1 | Empty file → `1`; deleting the highest ID may let the next add reuse it | Simplest, no sixth field; reuse is visible |
| Persist a "next ID" counter | Never reuse an ID | Adds a file-level field beyond the five required task properties |

For this project, **max existing + 1** is recommended: monotonic while the highest-ID record
remains, no extra state, easy to explain. Document the after-delete reuse behavior. If you
choose a counter, say why. IDs are positive integers in JSON, not strings; non-numeric or
non-positive input is a clear error. One local process means no concurrency race to solve.

### `updatedAt` rules

| Operation | `createdAt` | `updatedAt` |
|---|---|---|
| `add` | Set to `now` (UTC) | The **same** `now` value |
| `update` with a changed description | Unchanged | New `now` |
| `update` to identical text | Unchanged | Decide; recommend unchanged (no mutation) |
| `mark-in-progress` / `mark-done` | Unchanged | New `now`; a status change is a mutation |
| Repeating the same status | Unchanged | Decide; recommend unchanged |
| `list` / failed operation | Unchanged | Unchanged |
| `delete` | Record removed | Record removed |

Capture `now` once per operation so the two fields cannot disagree by milliseconds.

### Status filter mapping

| Command | Selection |
|---|---|
| `list` | All tasks, predictable order (recommend ascending ID) |
| `list todo` / `list in-progress` / `list done` | Exact matching status |
| `list <anything else>` | Clear error, non-zero exit |

`Requirements.txt` also says "all tasks that are not done", but its examples define `todo`
and `in-progress` separately. Keep exactly the three lowercase statuses shown there; treat
`todo` as "not started". An extra `not-done` filter is an addition, not a replacement.

### Messages

The requirements fix one string exactly:

```text
Task added successfully (ID: 1)
```

Design the rest consistently; for example `Task updated successfully (ID: 3)`,
`Task deleted successfully (ID: 3)`, `Task 3 marked as in-progress`, `Task 3 marked as done`.
Normal messages go to stdout, errors to stderr, failures exit non-zero. Decide whether an
empty `list` prints nothing or a friendly line, and document it.

---

## Phase 0 — Skeleton, usage, and `tasks.json` bootstrap

**Goal.** The binary runs, rejects nonsense input politely, and creates empty storage on
first use. No task behavior yet.

**Files:** `cmd/cli/main.go`, `internal/transport/cli/handler.go`,
`internal/storage/jsonstore/store.go` (bootstrap only), `internal/task/repository.go`
(contract, if chosen now), `README.md` (sketch).

**Spec.** No arguments → short usage listing all commands, non-zero exit. Unknown command →
error plus usage, non-zero. Wrong argument count → clear error, non-zero. On first storage
access, if `tasks.json` is missing in the current directory, create it with `[]`; never
overwrite an existing file. Stdlib imports only.

**Run manually:**
```text
go build -o /tmp/opencode/task-cli ./cmd/cli
cd /tmp/opencode/task-tracker-scratch && rm -f tasks.json
/tmp/opencode/task-cli
/tmp/opencode/task-cli frobnicate
/tmp/opencode/task-cli list
cat tasks.json
/tmp/opencode/task-cli list    # file exists: must still work
```
**Edge cases.** No args; unknown command; extra args; empty directory; existing file;
whitespace-only file; a directory named `tasks.json`; unwritable directory.

**Acceptance.** Usage names every command; missing file is created once with `[]`; existing
file untouched; bad invocations exit non-zero; `gofmt -l .` and `go vet ./...` are clean.

**Checkpoint.** Why check "not exists" instead of blindly writing? What happens if the file
exists but is empty?

## Phase 1 — `add`

**Goal.** Create one task with all five properties and persist it.

**Files:** `internal/task/model.go`, `internal/task/service.go`,
`internal/storage/jsonstore/store.go`, `internal/transport/cli/handler.go`.

**Spec.** Exactly one description argument. New task gets your documented ID, status
`todo`, and both timestamps from one UTC `now`. Append and rewrite safely. Print the exact
required success message.

**Run manually:**
```text
/tmp/opencode/task-cli add "Buy groceries"
cat tasks.json
/tmp/opencode/task-cli add "Read chapter 6"
cat tasks.json
```
**Edge cases.** Missing, empty, or multi-word unquoted description; punctuation; very long
text; duplicate descriptions (allowed); file missing before first add.

**Acceptance.** Two adds give IDs 1 and 2; both timestamps are identical UTC RFC3339
strings; exactly five keys per task; message matches character for character; a failed add
does not change the file.

**Checkpoint.** Where is the ID chosen, the timestamp captured, and the JSON encoded? If
all three are in one function, consider splitting.

## Phase 2 — `list` and filters

**Goal.** Display tasks in predictable order with correct filtering.

**Files:** `internal/task/service.go` (selection), `internal/transport/cli/handler.go`
(formatting), possibly `internal/task/repository.go` and
`internal/storage/jsonstore/store.go` (read-only operation).

**Spec.** `list` shows all; the three filters select exact statuses; order deterministic
(ascending ID); bad filter is an error. Listing never changes the file.

**Run manually:**
```text
/tmp/opencode/task-cli list
/tmp/opencode/task-cli list todo
/tmp/opencode/task-cli list in-progress
/tmp/opencode/task-cli list done
/tmp/opencode/task-cli list finished
```
**Edge cases.** Empty store; one task per status; no matches; wrong-case filter; extra
argument; corrupt JSON.

**Acceptance.** Both added tasks appear under `list` and `todo`, neither under other
filters; bad filter exits non-zero; `tasks.json` unchanged by all lists.

**Checkpoint.** Is filtering a business rule or a display rule in your design? Why must
listing not rewrite the file?

## Phase 3 — `update`

**Goal.** Replace a description and record the mutation time.

**Files:** `internal/task/service.go`, `internal/storage/jsonstore/store.go`,
`internal/transport/cli/handler.go`.

**Spec.** Exactly two arguments; positive numeric ID that exists. Description changes,
`updatedAt` becomes a new UTC time; `id`, `createdAt`, `status` are untouched. Failures write
nothing.

**Run manually:**
```text
/tmp/opencode/task-cli update 1 "Buy groceries and cook dinner"
cat tasks.json
/tmp/opencode/task-cli update 99 "Ghost task"
/tmp/opencode/task-cli update abc "Bad ID"
```
**Edge cases.** Unknown/zero/negative/non-numeric ID; empty new description; missing or
extra argument; updating a `done` task (allowed unless you document otherwise); empty file.

**Acceptance.** Only the description and `updatedAt` of the target change; all failures exit
non-zero and leave the file untouched.

**Checkpoint.** What is your identical-description rule? Why is "ID must exist" a service
rule rather than a CLI rule?

## Phase 4 — `delete`

**Goal.** Remove one task by ID without damaging the rest.

**Files:** `internal/task/service.go`, `internal/storage/jsonstore/store.go`,
`internal/transport/cli/handler.go`.

**Spec.** One valid, existing ID argument. Remove exactly that task and rewrite safely.
Note what your max+1 policy will choose next after deleting the highest ID.

**Run manually:**
```text
/tmp/opencode/task-cli delete 2
cat tasks.json
/tmp/opencode/task-cli delete 2          # now unknown
/tmp/opencode/task-cli add "After delete"
cat tasks.json
```
**Edge cases.** Deleting the only task; unknown ID; invalid ID; empty file; empty result
array must remain valid JSON.

**Acceptance.** Remaining IDs and timestamps unchanged; unknown ID is a non-zero exit with
no write; last deletion leaves a valid empty array.

**Checkpoint.** Why is "not found" an error rather than silent success? Did the next `add`
match your documented policy?

## Phase 5 — `mark-in-progress` and `mark-done`

**Goal.** Change status with dedicated commands and update the timestamp.

**Files:** `internal/task/service.go`, `internal/transport/cli/handler.go`, and
`internal/task/model.go` if named status values arrive here.

**Spec.** One valid ID each. Set the corresponding exact lowercase status and a new UTC
`updatedAt`; leave `createdAt` and description alone. Repeats follow your documented rule.

**Run manually:**
```text
/tmp/opencode/task-cli mark-in-progress 1
cat tasks.json
/tmp/opencode/task-cli list in-progress
/tmp/opencode/task-cli mark-done 1
cat tasks.json
/tmp/opencode/task-cli list done
```
**Edge cases.** Unknown/invalid ID; extra argument; marking an already-marked task;
switching statuses repeatedly; empty file.

**Acceptance.** Filters reflect transitions; `updatedAt` changes on real status changes;
nothing else changes; invalid input exits non-zero.

**Checkpoint.** Why separate commands instead of a generic `set-status`? What prevents a
typo like `donee` from entering the file?

## Phase 6 — Polish: atomic writes, validation, README, tests

**Goal.** Make failures safe, document behavior, and lock it in with tests.

**Files:** `internal/storage/jsonstore/store.go` (atomic replace),
`internal/task/service.go` and `internal/task/model.go` (validation), `README.md`, new
`*_test.go` files, `cmd/cli/main.go` (exit-code review).

**Spec.** Encode to a temporary file in the same directory, rename over the target, clean
up on failure. Validate all descriptions with one rule (recommend: non-empty after trimming
whitespace) and all IDs in one place. README covers build, every command, the exact schema,
ID policy, timestamp format, filters, and errors. Tests cover JSON round trips, ID
assignment, filtering, invalid input, and missing-file bootstrap; use temp dirs and an
injected clock (`09-testing-and-debugging.md`).

**Run manually:**
```text
gofmt -l .
go vet ./...
go test ./...
go build -o /tmp/opencode/task-cli ./cmd/cli
```
Then repeat the full manual script in a fresh scratch directory.

**Edge cases.** Simulated encode failure; read-only directory; corrupt JSON (clear error,
never silent reset); trailing garbage; leftover temp file; whitespace-only description.

**Acceptance.** No path leaves a partially written file; all commands work after a clean
rebuild; README answers newcomer questions; `gofmt`, `go vet`, `go test` clean.

**Checkpoint.** How would you prove the atomic write works? Which test catches a future
change that writes in place?

## Phase 7 — Final review

**Goal.** Verify against `Requirements.txt` and `LEARNING_PATH.md`, not memory.

**Files:** code only if review finds a defect; README and journal otherwise.

**Spec.** Tick every requirement against observed behavior. Explain out loud why each file
in the layout exists and what it does not do.

**Run manually:** `gofmt -l .`, `go vet ./...`, `go test ./...`, `go build ./...`; then rerun
the manual script in a brand-new scratch directory.

**Edge cases.** Stale binary left in PATH; leftover `tasks.json` from an earlier phase;
README examples that no longer match the code.

**Acceptance.** Both checklists fully ticked; placeholders untouched; no scratch data
committed; you can explain timestamps, dependency direction, and ID policy without notes.

**Checkpoint.** In two sentences, why can't the CLI layer corrupt the JSON file? If you
cannot, revisit `07-architecture-and-project-layout.md`.

---

## 3. JSON schema verification

- [ ] File is `tasks.json` in the process's current directory.
- [ ] Top level is an array; empty store is `[]`, not `{}` or blank text.
- [ ] Each object has exactly `id`, `description`, `status`, `createdAt`, `updatedAt`.
- [ ] `id` is a positive JSON number; the other four are JSON strings.
- [ ] `status` is exactly `todo`, `in-progress`, or `done`.
- [ ] Timestamps are UTC RFC3339 strings (recommend ending in `Z`).
- [ ] On add both timestamps match; later `createdAt` never changes.

Verify with `cat tasks.json`, `python3 -m json.tool tasks.json` if available (verification
only, never a runtime dependency), or a scratch Go program using `json.Valid` and
`json.Unmarshal` into your model. See `04-json-and-filesystem.md` and `go doc encoding/json`.

## 4. Manual test script checklist

Run from `/tmp/opencode/task-tracker-scratch` with the built binary; reset with
`rm -f tasks.json` between full runs — **only in scratch**.

- [ ] Fresh directory: `list` creates `[]` and succeeds.
- [ ] `add "Buy groceries"` → exact message; ID 1; five fields.
- [ ] `add "Read chapter 6"` → ID 2; `list` shows both in ID order.
- [ ] `list todo` shows both; other filters show none.
- [ ] `update 1 "Buy groceries and cook dinner"` → description and `updatedAt` changed only.
- [ ] `mark-in-progress 1` → shown under `in-progress`, not `todo`.
- [ ] `mark-done 1` → shown under `done`, not `in-progress`.
- [ ] `update 1 ""` → clear error; file unchanged.
- [ ] `delete 2` → gone, `list` still works; `delete 999` → error, no change.
- [ ] `list bogus`, no args, unknown command → clear errors, non-zero exit.
- [ ] Corrupt `tasks.json` → clear error, no panic, no silent reset.
- [ ] After every step: `cat tasks.json` and confirm valid JSON.

Check failures with `echo $?`: printed text alone does not reveal process status.

## 5. Commit cadence

One commit per completed phase after its checkpoint passes; this localizes regressions.
Before each: `gofmt -l .`, `go vet ./...`, `go test ./...`, plus the phase's manual commands.
Use short messages like `Add task creation with timestamps`. Never commit scratch data,
binaries, or temporary files; build into `/tmp/opencode`. If a phase becomes a big refactor,
commit mechanical renames separately from behavior changes. Update the journal the same day.

## 6. When to ask for help

Try first, in order: `go doc <package>` and `go doc <package>.<Symbol>`; `go help <command>`
(`build`, `run`, `test`, `modules`); the stdlib source under `$(go env GOROOT)/src`; and your
own ten-line reproduction. When you ask someone, include the exact command, exact output,
expected behavior, what you tried, and the smallest repro. "It doesn't work" is
unanswerable; "this command exits 1 with this error" is usually solved in one reply.

## 7. Common mistakes

| Mistake | Consequence | Habit |
|---|---|---|
| Several commands at once | Hard to localize bugs | One phase per session |
| Testing in the repository root | Stray `tasks.json`, accidental commits | Binary in `/tmp/opencode`, scratch dir |
| Two `time.Now()` calls on add | Timestamps disagree | Capture `now` once |
| Rewriting in place | Crash can corrupt JSON | Atomic temp file + rename |
| Silent success for unknown IDs | Users think it worked | Error and non-zero exit |
| Comparing formatted strings | Filtering/order breaks | Compare status values and instants |
| Extra "just in case" fields | Schema drift | Check the five required keys |
| Editing placeholder packages | Unbounded scope | Leave them empty |

### Professional habit

Before every write to `tasks.json`, ask: "If the power failed right now, would the file
still be valid?" If the answer depends on timing, the write is not atomic yet.

### Professional habit

Treat acceptance criteria as a contract with future you. A phase is done when the edge
cases and file contents pass — not merely when the happy path printed a message.

## 8. Apply it to Task Tracker

- Write the ID policy and `updatedAt` rules at the top of your journal before Phase 1;
  re-read them before Phases 3–5.
- Per phase, fill in: "The only layer that changes is ___, because the change is about ___."
- After Phase 2, draw your real dependency arrows and compare with the diagram in
  `07-architecture-and-project-layout.md`.
- After Phase 6, list each test and the bug it would catch. A test with no failure mode can
  be deleted.
- If you crave a `config.go`, logger, or generic repository, journal the urge and revisit
  after Phase 7. Most such ideas evaporate once the CLI works.

## Checkpoint

- [ ] All seven commands work from a clean scratch directory.
- [ ] Every task has exactly the five required fields.
- [ ] Failures give clear messages, non-zero exits, and no file changes.
- [ ] `gofmt -l .`, `go vet ./...`, `go test ./...` are clean.
- [ ] You can explain ID policy, timestamp rules, filter mapping, and layer boundaries out
      loud.
- [ ] One commit per phase exists, with no scratch data or binaries committed.

If any box is unchecked, return to that phase rather than starting a new feature.

**Next:** `09-testing-and-debugging.md`
