# 10 — Professional Habits

> **Scope note.** Everything here uses only the Go standard library. `gofmt` and `go vet` ship
> with Go; you may hear about `gofumpt`, but it is an external tool and this path deliberately
> does not use or recommend it. `gofmt` is enough.

## Why this matters for Task Tracker

The CLI is small, but small programs still rot. A sloppy `Task` field, an ignored error, or a
write that truncates `tasks.json` before the new data is ready will cost you more time than
careful habits cost to build. This chapter is the set of defaults you can apply **today**, while
the project is still tiny, so that later features fit into a clean shape instead of a pile of
patches. It complements `09-testing-and-debugging.md` (verification) and
`07-architecture-and-project-layout.md` (structure).

## What you will learn

- A daily baseline of formatting, vetting, testing, and committing.
- Naming rules that make Go code read like Go.
- How to keep functions short and errors informative.
- When and how to copy slices, maps, and time values.
- Why boundary validation, atomic writes, idempotency, and deterministic output matter.
- A sane performance and memory mindset for a tiny CLI.
- How to structure the README and your Git history.

---

## 1. The daily baseline

Run these from the module root. Make them a reflex, not a ceremony:

| Command | Purpose | Expected result |
|---|---|---|
| `gofmt -l .` | list files whose formatting differs | no output |
| `gofmt -w .` | rewrite files in Go's canonical style | — |
| `go vet ./...` | report suspicious constructs that still compile | no output |
| `go test ./...` | run all tests | `ok` for each package |
| `go build ./...` | confirm every package compiles | no output |

`gofmt` is not a matter of taste: it is the one formatting standard every Go tool assumes. Do not
fight it or hand-align code. Configure your editor to run it on save, then use `gofmt -l .` as
the final check.

`go vet` is a static analyzer included with Go. It catches things like wrong `fmt` verbs,
copies of types that must not be copied, and unreachable code. It is not a linter you install;
it is part of the toolchain.

> ### Professional habit
> Never commit a change where `gofmt -l .`, `go vet ./...`, or `go test ./...` reports a
> problem. Fix first, commit second. The whole check takes seconds on this project.

---

## 2. Naming: short where obvious, precise where it counts

Go favors short names in small scopes and descriptive names at package boundaries.

| Prefer | Avoid | Why |
|---|---|---|
| `id`, `err`, `t`, `b`, `i` in a short function | `theIdentifier`, `errorValue`, `temporaryVariable` | The scope is three lines; noise hurts |
| `MarkDone`, `NextID`, `LoadTasks` | `mark_done`, `next_id`, `loadTasks` | Exported Go names use `UpperCamelCase` |
| `ID`, `URL`, `HTTP`, `JSON` in identifiers | `Id`, `Url`, `Http`, `Json` | Go treats common initialisms as one capitalized word |
| `task.Service` | `task.TaskService` | Package name already says `task`; repeating it stutters |
| `newNote` (unexported) | `NewNote` for a private helper | Capitalization is visibility, not emphasis |
| `load` / `store` as interfaces with two methods | `AbstractTaskRepositoryManager` | Name the role, not the implementation |

`task.Task` is a common and tolerable exception to the stuttering rule; long names such as
`task.TaskService` are not. A name should say *what* the thing is, not *how* it is implemented:
prefer `Store` over `JSONFileStore` unless multiple store implementations must coexist.

**Jargon:** *exported* means the identifier starts with an uppercase letter and can be imported
by another package; *unexported* starts lowercase and is visible only inside its package.

---

## 3. Function length and single responsibility

A function should have one reason to change and a name that honestly describes all of its work.
If you need "and" to name it (`validateAndSaveAndPrint`), split it.

Useful heuristics, not laws:

- Most functions fit on one screen; many are under 20 lines. Longer is allowed when the body is
  a single straightforward sequence.
- Each helper should be testable on its own. Pure functions (same input, same output, no I/O)
  are the easiest to test.
- Keep details at one level of abstraction. A function that parses, validates, writes, and
  formats is really four functions.
- Prefer a descriptive helper over a long comment block. Code that explains itself needs less
  commentary.

> ### Professional habit
> When a function grows, extract the part you most want to test — not an arbitrary chunk.
> The extraction that improves tests is usually the right boundary.

---

## 4. Happy path first: early returns

Guard clauses keep the main flow unindented and readable. Handle the error or invalid case at
the top, then return; leave the successful path as the straight line through the function.

Generic example with a `Note`:

```go
func describe(note Note) (string, error) {
    if note.Text == "" {
        return "", errors.New("note text is empty")
    }
    if n := len(note.Text); n > 80 {
        return "", fmt.Errorf("note text too long: %d characters", n)
    }
    return note.Text, nil
}
```

Contrast that with nested `if` blocks five levels deep: the reader has to hold every condition
in memory. Early returns let them forget each condition as soon as it is handled.

Error message conventions:

- Start lowercase; no trailing period. These strings are often embedded in other messages.
- Say what failed and what was expected: `unknown status "doing"; want todo, in-progress, or done`.
- Include the offending value with `%q` so spaces and empty strings are visible.

---

## 5. Errors: wrap, never ignore

A returned error should gain context as it travels outward, not lose it. Use `%w` exactly once
per layer where you add information; use `%v` when you only want to display it.

Generic example:

```go
func loadNote(path string) (Note, error) {
    data, err := os.ReadFile(path)
    if err != nil {
        return Note{}, fmt.Errorf("read note %q: %w", path, err)
    }
    // ... decoding omitted: this is an illustration, not a solution
}
```

Rules that scale from a toy CLI to production:

1. **Check every error.** A bare `_ = err` is acceptable only when you have consciously decided
   the failure is irrelevant; say why in a short comment.
2. **Return errors; print once.** Library and internal functions return errors. The outermost
   `main` decides what to show on `os.Stderr` and which exit code to use. Printing inside every
   layer produces duplicated, confusing messages.
3. **Preserve sentinels.** Wrapping with `%w` keeps `errors.Is` working; see
   `09-testing-and-debugging.md`.
4. **Do not panic for expected failures.** Bad user input and corrupt files are normal events.
5. **Never discard partial results silently.** If a write fails after a read, report it.

---

## 6. Comments that explain why

Good comments are rare and valuable. They explain a *decision* the code cannot express: why a
lock is needed, why a workaround exists, why a particular order is required.

- Do not narrate the obvious: `i++ // increment i` adds nothing.
- Do not keep commented-out code; Git remembers it.
- Every exported identifier should have a **doc comment** beginning with its name. This is a
  convention the standard library follows and `go doc` displays.

```go
// NewLibraryBook copies tags so later changes to the caller's slice
// cannot alter the stored book.
func NewLibraryBook(title string, tags []string) LibraryBook {
    return LibraryBook{Title: title, Tags: append([]string(nil), tags...)}
}
```

A **package comment** sits above the `package` clause in one file (often `doc.go`) and says what
the package is for. One or two sentences are plenty: `// Package task models a task and its
allowed status transitions.`

---

## 7. Immutability and defensive copies

Slices, maps, and pointers are references in disguise. Returning or storing one without copying
lets a caller change your internal state unexpectedly.

| Value | Habit | Reason |
|---|---|---|
| `[]T` | `append([]T(nil), s...)` when copying | Avoid sharing the backing array |
| `map[K]V` | `maps.Clone(m)` (standard library) | It is a shallow copy; nested values still share |
| `time.Time` | Pass and return by value | A `time.Time` value is small and safe to copy; avoid `*time.Time` fields unless nil means something |
| `struct` with reference fields | Copy the struct *and* its reference fields | A struct copy alone is shallow |
| Interface holding a pointer | Know who owns the pointed-to value | Otherwise two owners may mutate it |

Read back with a copy as well:

```go
func (b LibraryBook) TagsCopy() []string {
    return append([]string(nil), b.Tags...)
}
```

Note the **value receiver** here: reading through a copy is fine. If a method must modify the
receiver, use a pointer receiver (`func (b *LibraryBook) AddTag(...)`). Mixing receivers on one
type is usually a bug; choose one style per type.

Immutability where practical means: build a value completely, then share it; do not hand out
mutable internals; treat a decoded `tasks.json` as a snapshot you replace rather than a shared
scratchpad.

---

## 8. Validation at boundaries

Validate once, at the edge where untrusted data enters:

| Boundary | Typical checks |
|---|---|
| Command line (`os.Args`) | known command, correct argument count, numeric ID, allowed status word, non-empty description |
| JSON file | syntactically valid JSON, expected shape, plausible status values, non-negative IDs |
| Public package functions | parameters that would otherwise produce nonsense (empty paths, nil dependencies) |

Once a value has passed validation, internal helpers may trust it. Revalidating in every helper
wastes code and hides where the rule actually lives. Turn a raw string into a typed value as
early as you can: after parsing, the rest of the program works with a `Status`, not another
string.

Error messages from boundary validation are user-facing. They should tell the learner exactly
what to type next, not just "invalid input".

---

## 9. Atomic writes, idempotency, determinism

These three habits prevent most data-loss and "it works on my machine" bugs.

### Atomic file replacement

Writing directly to `tasks.json` can leave a half-written file if the process stops mid-write.
The safe pattern is replace-by-rename:

1. Create a temporary file **in the same directory** as `tasks.json` (`os.CreateTemp`).
2. Write all bytes and check the write error; close the file and check the close error.
3. Rename the temporary file over `tasks.json` (`os.Rename`). On one filesystem this replaces
   the old file in a single step for readers.
4. On any failure, remove the temporary file and return an error.
5. Set a sensible file mode deliberately (`0o644` for a user-owned data file).

Check `go doc os.CreateTemp` and `go doc os.Rename` for details and portability caveats: rename
semantics are OS-specific, especially across directories or on Windows.

### Idempotency

An **idempotent** operation gives the same final state whether it runs once or several times.
Decide and document the semantics:

- `mark-done 3` twice should leave the same task and the same status; decide whether the second
  call updates `updatedAt`.
- Deleting an unknown ID should produce a predictable result (clear error or explicit no-op),
  never silent corruption.
- Re-reading a missing file should not create garbage; creating an empty file once is fine.

### Deterministic output

| Source of nondeterminism | Fix |
|---|---|
| `time.Now()` in output | Format with `time.RFC3339` and inject the clock where practical |
| Map iteration | Sort keys or iterate a slice in a fixed order |
| Unspecified list order | Sort tasks by ID (or document insertion order) |
| Different error wording per layer | One place formats user-facing messages |

**Avoid global state.** Package-level variables for the store, cwd, or "current time" make tests
interfere with each other and make the program's behavior depend on call order. Pass values
instead; this is the same dependency injection idea from `09-testing-and-debugging.md`.

---

## 10. Performance and memory: a tiny CLI's mindset

Do not optimize by guesswork. A linear scan (`O(n)`) over a few hundred tasks is
microseconds; clarity wins. Measure before changing a design, using `go test -bench` only when a
real slowdown exists.

Still, know the basics:

- `append` may reallocate a slice and copy its elements. If you know the final size, use
  `make([]T, 0, n)` so growth is predictable.
- A `nil` map can be read from but not written to; initialize it with `make(map[K]V)` before
  assignment.
- `defer` runs when the *function* returns, not at the end of a loop. A `defer` inside a loop
  with 10,000 iterations holds 10,000 calls. Put the loop body in a helper function if you need
  per-iteration cleanup.
- Reading the whole JSON file into memory is fine for this project; streaming parsers add
  complexity with no benefit.
- Goroutines are not a performance button. This CLI is sequential; keep it that way.

> ### Professional habit
> Write the obvious correct version, test it, then measure. Only optimize a measured hotspot,
> and keep the simpler version in Git history so you can compare.

---

## 11. README structure for this project

A README is part of the product. No internet links are needed — local commands and examples are
enough. Suggested sections:

| Section | What it should contain |
|---|---|
| Overview | One paragraph: what the tool does, where it stores data, stdlib-only constraint |
| Install / build | `go build -o tasktracker ./cmd/cli`; note the required Go version from `go.mod` |
| Commands | Every command with its positional arguments: `add`, `update`, `delete`, `mark-in-progress`, `mark-done`, `list` plus status filters |
| Examples | A short session: add two tasks, mark one done, list done; show `tasks.json` location |
| JSON schema | The five required fields (`id`, `description`, `status`, `createdAt`, `updatedAt`), their types, allowed status words, and timestamp format |
| Error behavior | Missing file, corrupt JSON, bad arguments, unknown ID: what is printed and the exit code |
| Development | `go test ./...`, `go vet ./...`, `gofmt -l .`, scratch-directory smoke tests, pointer to `LEARNING_PATH.md` |

Do not paste your full source into the README. Show *usage*, not implementation. Update the
README in the same commit as the behavior it describes.

---

## 12. Git hygiene

- Commit small, complete changes: one command, one bug fix, one documentation section.
- Write subjects in the imperative mood: `Add delete command`, not `Added stuff` or `wip`.
  Use the body to explain *why* when the reason is not obvious.
- Add a `.gitignore` for built binaries (for example `/tasktracker-cli`, `/cli`, `/bin/`).
- **Do not commit a real `tasks.json`.** It is runtime data in the current directory, and it may
  contain the learner's actual tasks. Add it to `.gitignore`; if you want a sample, commit a
  clearly named `tasks.example.json` with synthetic data instead.
- Never `git add .` without looking at `git status` first. Check that no scratch files or
  credentials are included.
- Do not commit broken intermediate states just to "save". Use notes or a stash, and commit when
  the checkpoint passes.

### Pre-commit checklist

- [ ] `gofmt -l .` prints nothing.
- [ ] `go vet ./...` is clean.
- [ ] `go test ./...` passes.
- [ ] A scratch-directory smoke test covers the changed command.
- [ ] README and notes reflect any new behavior.
- [ ] `git status` shows only intended files; no binaries or `tasks.json`.
- [ ] Commit message says what changed and why.

### Self code-review checklist

1. Does every exported identifier have a doc comment beginning with its name?
2. Does each function do one thing, and does its name match what it does?
3. Is every error checked, wrapped with context, and eventually printed exactly once?
4. Are errors compared with `errors.Is` rather than string matching?
5. Are slices, maps, and time values copied where ownership could leak?
6. Is validation done at the boundary, not scattered through helpers?
7. Can the file be left half-written? If so, is replacement atomic?
8. Would running a command twice cause a surprising second result?
9. Is output independent of map order, wall clock, and previous runs?
10. If a test failed, did I fix the cause rather than loosen the assertion?

---

## Common mistakes

- Installing third-party linters or formatters before mastering `gofmt` and `go vet`.
- Naming every identifier with two or three words "to be clear" — clarity comes from scope and
  context too.
- Returning an error from a helper and separately printing the same error in `main`.
- Using `%v` where `%w` is needed, then discovering `errors.Is` no longer works.
- Handing out a slice field directly and wondering why a caller changed "private" state.
- Writing directly to `tasks.json` and assuming the OS will protect you from partial writes.
- Premature optimization that makes the code harder to read without making it measurably faster.

---

## Apply it to Task Tracker

Use these prompts on your own code; no solution is supplied.

1. Run `gofmt -l .`, `go vet ./...`, and `go test ./...`. Fix the first complaint before
   looking at the second.
2. Scan your identifiers for stuttering (`task.TaskSomething`) and inconsistent initialisms
   (`Id` versus `ID`). Rename consistently in one commit.
3. Pick the longest function you have written. Write down its responsibilities. Can you name one
   helper whose extraction would make it easier to test? Do that first.
4. Find one ignored error and one error printed in two places. Decide the single owner of each
   message.
5. List which of your types own slices, maps, or pointers. For each, decide: copy on input,
   copy on output, or documented shared ownership.
6. Draw the write path on paper: where is the temp file, when is it closed, what happens on
   failure, and when does the rename occur?
7. Decide the idempotency rule for `mark-done` on an already-done task and for `delete` of an
   unknown ID. Write it in the README before coding it.
8. Make one commit that follows the checklist above, then read your own diff as if a reviewer
   sent it to you.

---

## Checkpoint

Answer without scrolling back:

1. Which four commands form your pre-commit baseline, and what does each catch?
2. Why do Go identifiers use `ID` and `URL` rather than `Id` and `Url`?
3. What is a doc comment, and how must it start?
4. Why is `append([]string(nil), tags...)` used when storing or returning a slice?
5. Name the four steps of atomic replacement and the reason for each.
6. What does idempotent mean for `mark-done 1` called twice?
7. Why are map iteration and `time.Now()` problems for deterministic output?
8. When should `defer` inside a loop worry you?
9. What belongs in the README's "JSON schema" and "Error behavior" sections?
10. Why should a real `tasks.json` never be committed?

## Next

`11-troubleshooting-and-cheatsheet.md` — keep it open while you write code.
