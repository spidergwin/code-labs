# 03 — Errors and Validation

> **Jargon first.** An **error** is a value that describes something that went wrong. Go does
> not throw exceptions for ordinary failures: functions *return* an error value, and the
> caller decides what to do. A **sentinel error** is one specific, named error value you can
> compare against. **Wrapping** an error means attaching context while keeping the original
> error recoverable.

## Why this matters for Task Tracker

Task Tracker is a small program with a big obligation: it must never lie to you. If a task
cannot be saved, the CLI must not print "Task added successfully". If the user types
`update banana`, the program must say what was wrong instead of crashing with a stack trace.
If `tasks.json` is missing, that is *not* a disaster — it is an expected state the program
should handle on its own.

Every requirement in `Requirements.txt` that says "handle errors and edge cases gracefully"
lands in this chapter. You will learn how to move failures from the place they happen (deep in
a storage function) to the place that can decide what the user sees (the CLI boundary), without
losing the useful detail along the way.

## What you will learn

- What the built-in `error` interface is, and how `errors.New` and `fmt.Errorf` create errors.
- How to check errors immediately, return them upward, and add context at each layer.
- How `%w`, `errors.Is`, `errors.As`, and `errors.Unwrap` let you inspect wrapped errors.
- Why sentinel errors such as `os.ErrNotExist` are the right tool for "expected" failures.
- Why `panic`/`recover` and `log.Fatal` are poor choices for CLI input errors.
- How to design arity, ID, description, and status validation with clear messages.
- User-facing vs developer output, stdout vs stderr, and thinking about partial failure.

## 1. The `error` interface

The entire error system is one tiny interface built into the language:

```go
type error interface {
	Error() string // a human-readable description
}
```

Any type with an `Error() string` method satisfies it; two constructors cover most cases:

```go
import (
	"errors"
	"fmt"
)

var errEmpty = errors.New("description must not be empty") // fixed message, no formatting

err := fmt.Errorf("task %d not found", id)                 // formatted message
```

| Constructor | Use it when | Example message |
|---|---|---|
| `errors.New("...")` | The message never changes; often a package-level sentinel | `invalid status` |
| `fmt.Errorf("...", args)` | The message needs values from the situation | `unknown ID 42` |
| `fmt.Errorf("...: %w", err)` | You are adding context to an existing error | `read tasks.json: open ...: no such file` |

> **Professional habit.** Create sentinel errors as package-level `var` values with names
> beginning `Err`, for example `ErrNotFound`. Compare with `errors.Is`, never with `==` once
> wrapping is involved.

## 2. Check errors immediately

The golden rule in Go is: **check the error at the boundary where it is produced.**

```go
data, err := os.ReadFile("tasks.json")
if err != nil {
	return err // do NOT keep going and do NOT ignore err
}
// Only here is data guaranteed to be meaningful.
```

Ignoring an error means continuing with zero values: `nil` bytes, `0` IDs, empty strings.
That is how a missing file turns into "all tasks deleted" instead of a helpful message.

| Tempting shortcut | What actually happens |
|---|---|
| `data, _ := os.ReadFile(path)` | On failure `data` is `nil`; later code may misbehave silently |
| `n, err := strconv.Atoi(s)` then use `n` anyway | `n` is `0` on a parse error, which can look like a real ID |
| Check `err` after several more statements | You may already have used invalid data or done side effects |
| `if err != nil { panic(err) }` | Stack trace instead of a CLI-quality message |

There is one legitimate way to discard an error: when failure truly cannot matter, and you
write `_ = ...` deliberately so a reader sees the intent. In Task Tracker that is rare; the
one place you will see it is cleaning up a temporary file after a failed write (see
`04-json-and-filesystem.md`).

## 3. Return errors up the stack, adding context at each layer

A typical call chain in this project will look like this (names only — you decide the code):

```text
main -> CLI handler -> service -> JSON store -> os.ReadFile
```

The low-level layer reports *what* broke. Each higher layer adds *where it was in the
operation*. Use `%w` (the "wrap" verb) to keep the original error reachable:

```go
// Illustrative only: a generic settings loader, not Task Tracker code.
func load(path string) (Settings, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return Settings{}, fmt.Errorf("read settings %q: %w", path, err)
	}
	var s Settings
	if err := json.Unmarshal(data, &s); err != nil {
		return Settings{}, fmt.Errorf("parse settings %q: %w", path, err)
	}
	return s, nil
}
```

The resulting message might read:

```text
read settings "settings.json": open settings.json: no such file or directory
```

That is useful to a developer *and* testable with `errors.Is` because the original
`*fs.PathError` is still inside the chain.

| Layer | Good context to add | Noise to avoid |
|---|---|---|
| Storage | Path and operation: `read tasks.json`, `write tasks.json` | Repeating the whole call stack |
| Service | Domain action: `add task`, `update task 7` | Restating the OS message |
| CLI handler | User-facing summary, often *instead of* the raw error | Internal function names |
| `main` | Exit code and one final line to stderr | Duplicate messages printed twice |

### When NOT to wrap

Wrapping is a tool, not a reflex. Skip it when:

- You have nothing to add. `if err != nil { return err }` is perfectly idiomatic.
- You are at the CLI boundary and will print a polished, translated message to the user.
- You intend to **replace** a low-level error with a domain error, for example turning
  `os.ErrNotExist` into `ErrStoreMissing` for your own package. Do this only when callers
  genuinely need the domain concept; otherwise keep the original.
- The error came from your own validation and already says everything needed.

Do not emit the same error text at three layers; users read the top line and assume the rest
is noise.

> **Professional habit.** Add context only when it answers a question the lower layer could
> not: *which file?*, *which task ID?*, *which operation?* If it answers none of those, return
> the error unchanged.

## 4. Inspecting errors: `Is`, `As`, and `Unwrap`

`errors.Unwrap(err)` returns the error wrapped by a single `%w`, or `nil` if there is none.
You rarely call it directly. Instead:

- `errors.Is(err, target)` answers "is this error, anywhere in the chain, the same as
  `target`?" It also understands sentinel values.
- `errors.As(err, &target)` answers "does any error in the chain have this type?" `target`
  must be a pointer to an error type or to an interface.

```go
// Illustrative validation helper: generic, not Task Tracker code.
func parseID(s string) (int, error) {
	if s == "" {
		return 0, ErrMissingID
	}
	n, err := strconv.Atoi(s)
	if err != nil {
		return 0, fmt.Errorf("parse id %q: %w", s, err)
	}
	if n <= 0 {
		return 0, fmt.Errorf("%w: %d", ErrInvalidID, n)
	}
	return n, nil
}
```

Now the caller can make decisions without string matching:

```go
id, err := parseID(userInput)
switch {
case errors.Is(err, ErrMissingID):
	// print usage hint
case errors.Is(err, ErrInvalidID):
	// "ID must be a positive whole number"
case errors.Is(err, strconv.ErrSyntax):
	// "not a number" — strconv wraps this sentinel in *strconv.NumError
case err != nil:
	// unexpected: report generically
default:
	_ = id // safe to use
}
```

If you need the structured details, use `As`:

```go
var numErr *strconv.NumError
if errors.As(err, &numErr) {
	fmt.Println("bad value was:", numErr.Num) // NumError has Num and Err fields
}
```

Useful standard-library sentinels for this project:

| Sentinel | Package | Meaning here |
|---|---|---|
| `os.ErrNotExist` (alias of `fs.ErrNotExist`) | `os`, `io/fs` | File or directory is absent — expected on first run |
| `os.ErrPermission` | `os` | Access denied |
| `strconv.ErrSyntax` | `strconv` | Text is not a valid number |
| `strconv.ErrRange` | `strconv` | Number is too large for the target type |
| `io.EOF` | `io` | End of input (mostly in tests and scanners) |

> **Go 1.27 detail.** A single `fmt.Errorf` call can contain one `%w`, which is the pattern
> you will use almost everywhere. Since Go 1.20, multiple `%w` verbs are also permitted and
> produce an error that matches any of the wrapped targets — but avoid them until you truly
> need to branch on two causes, because a single wrap chain is easier to reason about. Run
> `go doc fmt.Errorf` and `go doc errors` to see the exact current wording.

## 5. `panic` and `recover`: not for user mistakes

A **panic** stops normal execution and unwinds the stack. `recover` can stop that unwind, but
only when called directly from a function deferred in the panicking goroutine.

```go
func safe() (err error) {
	defer func() {
		if r := recover(); r != nil {
			err = fmt.Errorf("recovered: %v", r) // convert panic to error
		}
	}()
	mightPanic()
	return nil
}
```

Use panics for *programmer bugs* (an impossible state, a violated internal invariant), not
for input a user can type. Reasons to avoid them in a CLI:

| Concern | `panic` | Returned `error` |
|---|---|---|
| Output | Runtime stack trace, file paths, line numbers | One chosen sentence |
| Exit status | Unrecovered panic exits with status 2 by runtime convention | You choose 0, 1, or 2 |
| Control flow | Skips the rest of your function | Caller stays in control |
| Defers | **Do run** during panic unwinding | Run normally on return |
| Testability | Awkward to assert | Easy: `if err == nil { t.Fatal(...) }` |

Note the defer row carefully: `panic` *does* run deferred functions; `os.Exit` does *not*.
That distinction matters in the next section. A library-style `recover` at the top of `main`
can be a last-resort net, but it should never be your normal error path — and it must not be
used to hide bugs you could fix.

## 6. Ending the program: `os.Exit` and `log.Fatal`

An **exit code** (or status code) is the integer a process returns to its shell. Check it with
`echo $?` in bash. Zero means success; anything else signals failure.

```go
func main() {
	os.Exit(run(os.Args[1:])) // only place that decides the exit code
}

func run(args []string) int {
	if err := doWork(args); err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		return 1
	}
	return 0
}
```

Because `run` returns normally, any `defer` inside it executes before `os.Exit` is called in
`main`. That is the pattern to copy.

| Mechanism | Runs deferred functions? | Writes where? | Verdict for this CLI |
|---|---|---|---|
| `return err` from a helper | Yes | Nowhere by itself | Preferred everywhere |
| `os.Exit(code)` | **No** | Nowhere | Once, in `main`, after cleanup |
| `log.Fatal("...")` / `log.Fatalf` | **No** (calls `os.Exit(1)`) | `stderr`, with date/time prefix by default | Avoid for user errors |
| Unrecovered `panic` | Yes (unwinds) | `stderr`, with stack trace | Avoid |

`log.Fatal` is especially tempting when you are learning, but it gives you no control over
the exit code, adds a timestamp that confuses users, and skips deferred cleanup such as
removing a temporary file. `log` is still useful for optional developer diagnostics — see
section 8.

Suggested exit codes (document whatever you choose in your README):

| Code | Meaning | Example |
|---|---|---|
| `0` | Success | Task added, list printed, help shown |
| `1` | Runtime or data failure | File unreadable, JSON corrupt, ID not found |
| `2` | Usage error | Unknown command, missing or extra arguments, bad status |

## 7. Validating user input

**Validation** means checking input before you act on it. In this project every argument is a
`string` until you convert it, so validate explicitly. Decide the rule and the message before
you write the code:

| Check | Rule to choose | Clear message shape |
|---|---|---|
| Missing args | Required count per command; at least one arg overall | `usage: task-cli add "description"` |
| Extra args | Exact count for commands with fixed arity | `add takes 1 argument, got 3` |
| Non-numeric ID | `strconv.Atoi` must succeed | `ID must be a whole number, got "abc"` |
| Out-of-range ID | Positive, and present in the stored data | `no task with ID 99` |
| Empty description | Reject `""` and whitespace-only | `description must not be empty` |
| Invalid status | One of `todo`, `in-progress`, `done` | `unknown status "doing"; use todo, in-progress, or done` |

Two layers of "out of range" are worth separating: **syntactic** (`-3` is a valid integer but
not a valid ID) and **semantic** (`7` is a fine ID but no such task exists). Both are user
errors, but the messages differ.

A slightly fuller generic example, showing validation kept next to parsing:

```go
// Illustrative only: generic note helpers, not Task Tracker code.
var (
	ErrMissingID   = errors.New("missing id")
	ErrInvalidID   = errors.New("id must be a positive integer")
	ErrEmptyText   = errors.New("note text must not be empty")
	allowedStatus  = map[string]bool{"todo": true, "in-progress": true, "done": true}
)

func validateText(text string) error {
	if strings.TrimSpace(text) == "" {
		return ErrEmptyText
	}
	return nil
}

func validateStatus(s string) error {
	if !allowedStatus[s] {
		return fmt.Errorf("unknown status %q: want todo, in-progress, or done", s)
	}
	return nil
}
```

Validate **before** touching storage. If the ID cannot be parsed, there is no reason to read
`tasks.json` at all. Cheap checks first; expensive or side-effecting work last.

> **Professional habit.** Make validation functions pure: they take a value and return an
> error, with no printing and no file access. Then you can unit-test them in isolation and
> reuse them from any command.

## 8. User-facing messages vs developer debug messages, and stdout vs stderr

**stdout** (standard output) is the normal data channel. **stderr** (standard error) is the
channel for diagnostics. Keeping them separate lets a user redirect one without the other:

```bash
note add "Buy milk"            # success message goes to stdout
note add                       # error goes to stderr; stdout stays empty
note list 1>/dev/null          # discard data, still see errors
note list 2>/dev/null          # discard errors, still see data
```

| Message kind | Destination | Tone | Example |
|---|---|---|---|
| Command result / list output | stdout | Terse, stable | `Task added successfully (ID: 1)` |
| Usage and validation errors | stderr | Actionable | `update needs an ID and a description` |
| Unexpected internal failure | stderr | Honest, not blaming the user | `could not save tasks.json: permission denied` |
| Developer trace, env var or flag only | stderr | Detailed | `store.save: rename temp -> tasks.json: ...` |

Write user messages in terms of *what the user typed and what to do next*, not internal
function names. Keep one detailed channel for yourself — a `TASK_TRACKER_DEBUG=1` environment
variable or an optional `--verbose` flag — so a normal user never sees implementation details
but you can still diagnose a bug.

### Partial failure and data safety

A **partial failure** is when some steps succeeded and a later step failed. For example: you
append a new task in memory, write the file, and the write fails halfway. Ask these questions
for every mutating command:

- [ ] Did I validate all input before doing any work?
- [ ] Did I keep the original data intact until the new data was safely on disk?
- [ ] Could I print success before the write was confirmed? (It must be *after*.)
- [ ] If the write failed, is the file still parseable and unchanged?
- [ ] Did any earlier step create a temporary artifact that needs cleanup?
- [ ] Is the message specific enough to tell the user whether to retry?

The technique that makes most of this painless is the atomic temp-file-and-rename pattern;
you will learn it in `04-json-and-filesystem.md`.

## Common mistakes

- Ignoring `err` with `_` "just for now" and then using the zero value.
- Comparing errors with `==` after wrapping; use `errors.Is`.
- Using `%v` where `%w` was needed, which destroys the error chain.
- Wrapping at every layer until the final message is a paragraph of duplication.
- Calling `log.Fatal` deep inside a helper and wondering why `defer` never ran.
- Printing an error to stderr *and* returning it, then printing it again in `main`.
- Treating every `err != nil` as fatal: a missing `tasks.json` on first run is expected.
- Letting `panic` handle a non-numeric ID; users should not see stack traces.
- Printing success messages before confirming the file write succeeded.
- Validating after mutating the in-memory slice, leaving inconsistent state.
- Prefixing `"error: "` onto a message that already says "error". Pick one style.

> **Professional habit.** Treat every error check as a decision: *handle it here, add context
> and return it, or translate it?* Run `go vet ./...` before every commit.

## Apply it to Task Tracker

No solution code here — make these decisions in your own design notes first, then implement
them one at a time.

1. **Map the error paths.** For each of `add`, `update`, `delete`, `mark-in-progress`,
   `mark-done`, and `list`, list every way it can fail: bad args, bad ID, unknown ID, missing
   file, unreadable file, corrupt JSON, unwritable file. Which are user errors and which are
   environment errors?
2. **Choose your sentinels.** Which errors will be package-level values? Likely candidates:
   "not found", "invalid status", "store is missing". Where should each be created, and who
   should compare against it?
3. **Decide the first-run behavior.** How will you distinguish "file does not exist" from
   "file exists but is unreadable"? See `04-json-and-filesystem.md`.
4. **Write the message table.** For every validation rule in section 7, write the exact text
   your CLI will print. Include a hint about correct usage. Keep capitalization and quoting
   consistent.
5. **Draw the call chain.** Which layer adds `read tasks.json: ...`, which layer adds
   `add task: ...`, and which layer prints to stderr? Make sure no message contains the same
   phrase twice.
6. **Pick exit codes.** Which failures return `1` and which return `2`? Write the convention
   into your README so scripts can rely on it.
7. **Safety review.** Walk through `delete 3` when the file write fails. What does the user
   see? What does `tasks.json` contain afterwards? Does any `defer` still run?

Checklist before you call this feature done:

- [ ] Every function that can fail returns an `error`, and callers check it immediately.
- [ ] Wrapping uses `%w`; no command prints success before persistence succeeds.
- [ ] A missing file is an expected case, not a crash; bad input yields clean messages.
- [ ] `go vet ./...` and a few bad-input runs produce stable output.

## Checkpoint

Answer without scrolling back:

1. What is the exact definition of the `error` interface?
2. When do you use `errors.New` versus `fmt.Errorf`?
3. What does `%w` do that `%v` does not?
4. How do `errors.Is` and `errors.As` differ? Give one use case for each.
5. Why is `os.ErrNotExist` better than matching the string `"no such file or directory"`?
6. Which runs deferred functions: `panic` unwinding or `os.Exit`? Why does that matter here?
7. Why should a bad ID be an error return rather than a panic?
8. What is a partial failure, and what order of operations reduces the risk?

Next: `04-json-and-filesystem.md`
