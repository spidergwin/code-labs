# 09 — Testing and Debugging

> **Scope note.** This chapter uses only the Go standard library. Every command and API
> mentioned here ships with Go 1.27. When in doubt, check it with `go doc` — no internet needed.

## Why this matters for Task Tracker

A command-line program that reads and writes a JSON file has four unstable seams:

1. **Arguments** come from the outside world and arrive as raw strings.
2. **Files** live outside your program: they can be missing, empty, corrupt, or read-only.
3. **Time** changes every time you run the program, which makes output hard to compare.
4. **State** (the next ID, the current directory) can be left over from an earlier run.

Tests freeze those seams so you can change code without fear. Debugging is the matching
skill: when something breaks, you reduce the problem until the cause is obvious. You will use
both constantly in `08-feature-by-feature.md`.

## What you will learn

- How `go test` discovers tests, and which flags to reach for.
- How to write table-driven tests with plain `if got != want` checks.
- How to test filesystem behavior safely with `t.TempDir`, `t.Chdir`, and `t.Setenv`.
- How to inject a clock and a store so time and disk become controllable.
- How to assert error paths with `errors.Is`.
- How to write a repeatable manual smoke test for the real CLI.
- A debugging workflow that works without a graphical debugger.

---

## 1. The `go test` harness in five minutes

Go needs no test framework. A **test file** is any file ending in `_test.go`; it is ignored by
`go build` and compiled only by `go test`. A **test function** has this exact shape:

```go
func TestSomething(t *testing.T) {
    // arrange, act, assert
}
```

Rules: the name starts with `Test` followed by an uppercase letter (or digit), it takes one
`*testing.T`, and it returns nothing. `go test` scans the package's directory, so test files
live beside the code they exercise.

Choose a package declaration deliberately:

| Test package | Looks like | Use it when |
|---|---|---|
| `package foo` | same package as the code | you need to test unexported helpers |
| `package foo_test` | separate package in the same directory | you want to test only the public API, as a user would |

Both styles can coexist in one directory. For a small CLI, start with `package foo` for the
internal logic and use `package foo_test` for a few end-to-end checks of exported behavior.

A minimal example using an isolated `Counter` (not part of Task Tracker):

```go
package counter

import "testing"

func TestIncrementOnce(t *testing.T) {
    var c Counter
    c.Increment()

    got := c.Value()
    if got != 1 {
        t.Errorf("Value() = %d, want 1", got)
    }
}
```

### Commands you will actually type

| Command | Meaning | When |
|---|---|---|
| `go test ./...` | run every test in every package of the module | after each feature, before commit |
| `go test -v ./...` | show each test name and result | when you want to see what ran |
| `go test -run TestName ./...` | run tests whose names match a regular expression | focusing on one failure |
| `go test -count=1 ./...` | disable the test result cache and really rerun | after edits, or when a test smells flaky |
| `go test -race ./...` | enable the race detector | when code uses goroutines or shared state |

**Jargon:** a *test fixture* is the setup a test needs (a temp directory, a sample value, a
fake store). Go's cache means an unchanged passing package may print `(cached)`; `-count=1`
forces real execution.

---

## 2. Assertions without a third-party library

There is no `assert.Equal` in the standard library, and you do not need one:

```go
if got != want {
    t.Errorf("classify(%d) = %q, want %q", input, got, want)
}
```

`Errorf` marks the test failed and keeps going; `Fatalf` marks it failed and stops that test
immediately. Use `Fatalf` when continuing would make later lines meaningless — for example,
after a setup error or a failed file read. Prefer messages of the form `got X, want Y`.

For repeated checks, write a helper and call `t.Helper()` so failures point at the caller, not
at the helper:

```go
func assertEqual[T comparable](t *testing.T, got, want T) {
    t.Helper()
    if got != want {
        t.Errorf("got %v, want %v", got, want)
    }
}
```

The `[T comparable]` part is a type parameter: the helper works for any comparable type
(strings, ints, structs made of comparable fields). If generics feel like too much right now,
write a concrete helper for `int` or `string` instead; the pattern is the same.

### Table-driven tests

A **table-driven test** puts many cases in one slice of structs and loops over them. This is the
workhorse of Go testing: one behavior, many inputs, one place to add a new case.

```go
// classify is a stand-in for any small pure function worth testing.
func classify(n int) string {
    switch {
    case n < 0:
        return "negative"
    case n == 0:
        return "zero"
    default:
        return "positive"
    }
}

func TestClassify(t *testing.T) {
    tests := []struct {
        name  string
        input int
        want  string
    }{
        {name: "negative", input: -3, want: "negative"},
        {name: "zero", input: 0, want: "zero"},
        {name: "positive", input: 7, want: "positive"},
    }

    for _, tt := range tests {
        t.Run(tt.name, func(t *testing.T) {
            if got := classify(tt.input); got != tt.want {
                t.Errorf("classify(%d) = %q, want %q", tt.input, got, tt.want)
            }
        })
    }
}
```

`t.Run` creates a **subtest**: a named child with its own `*testing.T`. Failures are reported as
`TestClassify/zero`, and you can run just that case with
`go test -run 'TestClassify/zero' ./...`. In Go 1.22 and later (this project uses 1.27), each
loop iteration gets its own `tt`, so there is no need for the old `tt := tt` workaround.

---

## 3. Filesystem tests with `t.TempDir`

Never let a test write into the repository. `t.TempDir()` creates a unique directory, returns
its absolute path, and removes it automatically when the test and its subtests finish.

Generic example: a `saveNote` function that writes a `Note` to a path you give it.

```go
func TestSaveNoteWritesText(t *testing.T) {
    dir := t.TempDir()
    path := filepath.Join(dir, "notes.json")

    if err := saveNote(path, Note{ID: 1, Text: "Buy milk"}); err != nil {
        t.Fatalf("saveNote: %v", err)
    }

    data, err := os.ReadFile(path)
    if err != nil {
        t.Fatalf("ReadFile(%q): %v", path, err)
    }
    if !bytes.Contains(data, []byte("Buy milk")) {
        t.Errorf("file does not contain the note text; content = %s", data)
    }
}
```

Key habits:

- Build paths with `filepath.Join`, not string concatenation with `/`.
- Assert on the *file contents*, not only on the returned error.
- Give each test its own `t.TempDir()`; never assume test A ran before test B.
- For a "file missing" case, simply use a path inside the temp dir that you never create.

### Changing the current directory

Because Task Tracker stores `tasks.json` **in the current directory**, a test may need to change
that directory. Go 1.27 provides `testing.T.Chdir` (added in Go 1.24; confirm with
`go doc testing.T.Chdir`): it
calls `os.Chdir` and registers a cleanup that restores the original directory; on Unix it also
updates `PWD`. Both `t.Chdir` and `t.Setenv` affect the whole process, so **never combine them
with `t.Parallel()`** or with tests that have parallel ancestors.

If you ever need the manual form (for example, on an older toolchain), make the restoration
explicit and unconditional:

```go
old, err := os.Getwd()
if err != nil {
    t.Fatalf("Getwd: %v", err)
}
if err := os.Chdir(dir); err != nil {
    t.Fatalf("Chdir(%q): %v", dir, err)
}
t.Cleanup(func() {
    if err := os.Chdir(old); err != nil {
        t.Errorf("restore cwd: %v", err)
    }
})
```

`t.Cleanup` runs even when the test fails, which is exactly why it exists.

---

## 4. Injecting dependencies: clock and store

A function that calls `time.Now()` or opens a hard-coded file directly is hard to test. Pass
those capabilities in instead. This is **dependency injection**: the caller supplies the
collaborator, so a test can supply a predictable one.

Generic seams, using a `Note` model:

```go
type Clock func() time.Time

type NoteStore interface {
    Load() ([]Note, error)
    Save([]Note) error
}
```

- A **fake clock** is a `Clock` that always returns one fixed `time.Time`.
- A **fake store** is a small in-memory implementation of `NoteStore` (a slice plus an error
  field for simulating failures). You write only as much as the test needs; a fake is not a
  production feature.
- Define small interfaces where they are *used*, not where they are implemented. Two methods
  are enough here.

Injected time also solves the "updatedAt must change" problem: with a fixed clock you can
advance it deliberately instead of sleeping. Injected storage lets you test "disk failure"
without breaking your actual disk.

**Do not** reach for goroutines to make tests faster. This CLI is sequential; concurrency adds
race conditions, not value.

---

## 5. Testing error paths

Errors are behavior too. In `03-errors-and-validation.md` you learned sentinel errors and
wrapping; here is how to assert them. `errors.Is` walks the wrapped chain, so it still finds the
original error after `fmt.Errorf("...: %w", err)`.

```go
if err := svc.Find(99); !errors.Is(err, ErrNotFound) {
    t.Fatalf("Find(99) error = %v, want ErrNotFound", err)
}
```

Useful cases to cover for this project:

| Situation | What the test should assert |
|---|---|
| File does not exist | an error is returned (and `errors.Is(err, os.ErrNotExist)` if that is your chosen behavior) |
| File contains `{` | an error is returned; no panic, no silent empty list |
| ID does not exist | a sentinel such as `ErrNotFound` is detectable |
| Invalid status word | validation rejects it and the message names the allowed words |
| Write fails | the error propagates from the store with added context |

Also check the *success* side of the same boundary: a missing file may legitimately become an
empty list when your design says so. Decide the behavior first, then encode it in a test.

---

## 6. Keeping tests deterministic

A flaky test is worse than no test: you stop trusting it. Avoid these sources of randomness:

- **Wall clock.** Never assert "timestamp is close to now". Inject a fixed clock and compare
  exact values.
- **Order.** Slice order from `range` is stable, but map iteration order is deliberately
  random. Sort before comparing collections.
- **Leftover files.** Use a fresh `t.TempDir()` per test; do not rely on the previous test's
  `tasks.json`.
- **Sleeps.** If you need "later", move the fake clock forward.
- **Hidden global state.** Process-wide cwd and environment variables leak between tests;
  restore them with `t.Cleanup` (or use `t.Chdir` / `t.Setenv` without parallelism).

Run `go test -count=2 ./...` occasionally. If a test passes alone but fails on the second run,
you have hidden state.

### Golden files (optional)

A *golden file* is a stored expected output in a `testdata/` directory. The test compares actual
bytes with the file and fails on any difference. The Go tool ignores directories named
`testdata`, so they are safe to keep in the repo. This is useful for long, stable output such as
a help screen. For a small CLI it is usually overkill: plain string comparisons are clearer.
If you use golden files, update them intentionally and read the diff — never regenerate blindly.

---

## 7. Manual CLI smoke tests in a scratch directory

Unit tests check pieces; a **smoke test** checks that the assembled binary works from a real
shell. Always run it outside the repository so `tasks.json` is not created next to your source.

```bash
# From the repository root, build once:
mkdir -p /tmp/go-scratch
go build -o /tmp/go-scratch/tasktracker-cli ./cmd/cli

# Then work in a scratch directory that is NOT the repo:
mkdir -p /tmp/go-scratch/smoke
cd /tmp/go-scratch/smoke
rm -f tasks.json

/tmp/go-scratch/tasktracker-cli add "Buy milk"
cat tasks.json
/tmp/go-scratch/tasktracker-cli list
/tmp/go-scratch/tasktracker-cli mark-done 1
/tmp/go-scratch/tasktracker-cli list done

/tmp/go-scratch/tasktracker-cli nonsense; echo "exit=$?"
```

Check manually:

- [ ] `tasks.json` appears in the scratch directory, not in the repository.
- [ ] The JSON contains all five required fields with sensible values.
- [ ] `mark-done` changes both `status` and `updatedAt`.
- [ ] `list done` shows only done tasks.
- [ ] Bad commands print a helpful message and `exit=1` (or another non-zero value).

`echo "exit=$?"` must come immediately after the command whose status you want; any command in
between overwrites `$?`. Repeat with a corrupt `tasks.json` (write a single `{` into it) and
verify you get a clear error rather than a stack trace.

---

## 8. A debugging workflow that works

When something breaks, follow this order instead of changing random lines:

1. **Read the first compiler error literally.** It gives file, line, column, and a short message.
   Fix that one first; later errors are often consequences. See `11-troubleshooting-and-cheatsheet.md`.
2. **Reduce to a minimal reproduction.** Copy the smallest failing case into
   `/tmp/go-scratch`. If the bug disappears, the interaction with your larger program is the
   clue.
3. **Predict, then run.** Say out loud what you expect. A wrong prediction is information.
4. **Instrument at boundaries.** A temporary
   `fmt.Fprintln(os.Stderr, "after load:", len(items), err)` shows real values without
   corrupting stdout. Remove these lines when done.
5. **Run `go vet ./...`.** It catches suspicious formatting, unreachable code, and shadowed
   variables that compile fine.
6. **Run `go test -race ./...`** if goroutines or shared state are involved.
7. **Read the docs offline.** `go doc encoding/json`, `go doc os.Rename`, `go doc time.Parse`,
   or the source under `$(go env GOROOT)/src`. The docs describe edge cases you will not guess.
8. **Optional, not required:** a debugger such as Delve can set breakpoints, but it is an
   external tool and adds nothing essential to this project. Print statements plus the steps
   above are enough.

Keep a build journal entry for every bug: symptom, root cause, fix, and the question you should
have asked sooner.

---

## 9. Common bug classes in this project

| Symptom | Likely cause | First check |
|---|---|---|
| "`tasks.json` was created in the wrong place" | Wrong current directory; the test or shell ran from another folder | `pwd`, `os.Getwd()`, and use a scratch dir for smoke tests |
| "Unexpected end of JSON input" | Empty or truncated file, often after a crash mid-write | Inspect bytes; move to an atomic write (see `10-professional-habits.md`) |
| "json: cannot unmarshal ..." | JSON shape does not match the struct; wrong tags or field types | Print the raw JSON, then check struct tags and types |
| "ID 3 updated task 2" | Stale ID after a delete; IDs computed from `len(slice)` | Compute the next ID from existing IDs, not slice length |
| "nil pointer dereference" | Pointer never initialized, or `err` checked after dereferencing a result | Check errors before use; print the pointer's value |
| "method has no effect" | Wrong receiver: value receiver when mutation was intended | Look at `func (t T)` versus `func (t *T)` |
| "Timestamp is 0001-01-01" or parse errors | Wrong time layout (e.g. `2006-01-02` instead of RFC3339) | `go doc time.Layout`, `go doc time.RFC3339` |
| "Test passes alone, fails in the suite" | Shared cwd, env, file, or time state | Fresh temp dir, injected clock, no `t.Parallel()` |

## Common mistakes

- Testing only the happy path; corrupt data and unknown IDs are where bugs hide.
- Asserting on an error message's exact wording instead of `errors.Is` when a sentinel exists.
- Writing tests that call `time.Now()` and compare with a tolerance — they fail on slow machines.
- Using relative paths in tests; the test's cwd is not guaranteed to be the repo root.
- Calling `t.Parallel()` in a test that uses `t.Chdir`, `t.Setenv`, or a shared file.
- Leaving debug prints in `stdout`, which breaks machine-readable output. Use `os.Stderr`.
- Trusting a cached test result after editing only test data. Use `-count=1`.

---

## Apply it to Task Tracker

No implementation is given here on purpose. Work through these prompts against your own code:

1. **Pick three seams to test first.** Good candidates: parsing arguments into a command,
   loading an empty/missing file, and stamping timestamps with an injected clock. Write down
   which one you will do today.
2. **List direct calls to `time.Now()`, `os.Getwd()`, and `os.Open`/`os.ReadFile`.** For each,
   decide: pass it in, or keep it at the outermost edge? Why?
3. **Write a table of error cases** for `add`, `update`, and `mark-*`: missing args, extra args,
   non-numeric ID, unknown ID, bad status, corrupt JSON, unwritable file. Which are unit tests
   and which belong in the manual smoke test?
4. **Choose your folder contract.** Where will `t.Chdir` be used so `tasks.json` lands in a temp
   dir? How will you prove the file went there?
5. **Define determinism.** What order should `list` use? What exact text should errors have?
   How will you compare output without depending on the clock?
6. **After each new test, break it on purpose** (change a `want`, remove a field, corrupt the
   file) and confirm it fails. A test that cannot fail teaches nothing.
7. **Repeat the smoke test from a new scratch directory** after every feature. Record the
   commands you ran in `docs/MY_NOTES.md` so you can rerun them later.

---

## Checkpoint

Answer without scrolling back:

1. What must a file be named and a function look like for `go test` to discover it?
2. When would you choose `package foo_test` over `package foo`?
3. What do `-v`, `-run`, `-count=1`, and `-race` each change?
4. Why does a table-driven test use both a `name` field and `t.Run`?
5. What does `t.TempDir()` guarantee, and why must you never call `t.Parallel()` with
   `t.Chdir` or `t.Setenv`?
6. How does injecting a `Clock func() time.Time` make an `updatedAt` assertion exact?
7. Write the one-line pattern for checking a wrapped sentinel error.
8. Name three bug classes specific to this project and the first thing you would inspect for
   each.
9. Why should smoke tests run outside the repository?

## Next

`10-professional-habits.md` — turn code that works into code you can keep.
