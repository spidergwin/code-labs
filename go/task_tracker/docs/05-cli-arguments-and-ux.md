# 05 — CLI Arguments and UX

> **Jargon first.** A **CLI** (command-line interface) is a program driven by text typed in a
> shell. A **positional argument** is an argument identified by its position, such as the
> `1` in `task-cli delete 1`, rather than by a name such as `--id=1`. **Arity** is the number
> of arguments a command expects. A **flag** is a named option like `--help` or `-v`.

## Why this matters for Task Tracker

The entire user interface of Task Tracker is a list of strings. There are no buttons, no
prompts, no menus. If you get argument handling wrong, every feature is unreachable: a
description with a space arrives split into three arguments, a typo becomes a crash, or an
unknown command silently does nothing.

The specification in `Requirements.txt` asks for positional arguments and shows the exact
command shapes. Your job is to turn those shapes into predictable behavior: correct arity
checks, clear usage text, sensible exit codes, and output that behaves well when another
program reads it. Error conventions come from `03-errors-and-validation.md`; the data file is
covered in `04-json-and-filesystem.md`.

## What you will learn

- How `os.Args` is laid out and why `os.Args[0]` is not user input.
- Why the roadmap.sh specification uses positional arguments.
- Dispatching with `switch` and checking arity per command.
- How the shell handles quotes, spaces, empty strings, and wildcards before Go sees them.
- Why every argument is a `string`, and how to convert IDs with `strconv`.
- The optional `flag` package and why flags must precede positional arguments.
- Usage text, `--help`/`-h`, stderr, exit codes, and script-friendly output.
- Idempotency, destructive actions, optional status filters, and why not to prompt.

## 1. `os.Args`: the raw material

Every Go program gets its arguments in one slice:

```go
fmt.Printf("%q\n", os.Args)
```

Run it as `./note add "Buy milk"`:

```text
["./note" "add" "Buy milk"]
```

| Element | Meaning | Treat as |
|---|---|---|
| `os.Args[0]` | The program name, spelled the way it was invoked | Diagnostic only |
| `os.Args[1]` | Usually the command (`add`, `show`, …) | User input |
| `os.Args[2]` and later | Operands: IDs, descriptions, filters | User input |
| `len(os.Args)` | Program name + number of arguments | Always check before indexing |

`os.Args[0]` might be `./note`, `/usr/local/bin/note`, or just `note`. Never branch on it and
never use it to locate your data file — the data file lives in the current working directory
by design (`04-json-and-filesystem.md`).

The most important rule: **check the length before indexing.**

```go
if len(os.Args) < 2 {
	// no command: print usage, exit with a usage code
}
cmd := os.Args[1]
```

Indexing `os.Args[1]` without checking panics with an index-out-of-range error — the exact
stack trace you are trying to avoid from `03-errors-and-validation.md`.

## 2. Why positional arguments?

**Positional** means the meaning comes from the slot: first the command, then the ID, then the
description. The specification shows commands like:

```bash
task-cli add "Buy groceries"
task-cli update 1 "Buy groceries and cook dinner"
task-cli delete 1
task-cli list in-progress
```

Reasons this style fits the project:

- It is what the spec requires, including for grading and README examples.
- It is simple: no parser library, no configuration file, no ambiguity about defaults.
- It is scriptable: a shell script can substitute values into fixed positions.
- It keeps the learner's focus on Go concepts (slices, validation, files) rather than on a
  flag-parsing framework.

Named flags are not forbidden, but they are not the core interface. If you add an optional
`--verbose` or `--help`, keep the documented positional forms working exactly as written.

## 3. Manual dispatch with a `switch`

The smallest reliable design is: normalize the args slice, handle help, switch on the command,
check arity inside each `case`, then call a function that does the work.

```go
// Illustrative only: a tiny "note" CLI, NOT the Task Tracker command set.
package main

import (
	"fmt"
	"io"
	"os"
	"strconv"
)

const usage = `usage: note <add|show> [text|id]

  note add "text"   add a note
  note show <id>    show a note by id
  note --help       show this help
`

func run(args []string, out, errOut io.Writer) int {
	if len(args) == 0 {
		fmt.Fprint(errOut, usage)
		return 2
	}
	if args[0] == "-h" || args[0] == "--help" {
		fmt.Fprint(out, usage)
		return 0
	}

	switch args[0] {
	case "add":
		if len(args) != 2 || args[1] == "" {
			fmt.Fprintln(errOut, "add needs exactly one non-empty text argument")
			fmt.Fprint(errOut, usage)
			return 2
		}
		fmt.Fprintf(out, "added note: %s\n", args[1])
		return 0

	case "show":
		if len(args) != 2 {
			fmt.Fprintln(errOut, "show needs exactly one id")
			return 2
		}
		id, err := strconv.Atoi(args[1])
		if err != nil || id <= 0 {
			fmt.Fprintf(errOut, "id must be a positive whole number, got %q\n", args[1])
			return 2
		}
		fmt.Fprintf(out, "note %d\n", id) // stand-in for real lookup
		return 0

	default:
		fmt.Fprintf(errOut, "unknown command %q\n", args[0])
		fmt.Fprint(errOut, usage)
		return 2
	}
}

func main() {
	os.Exit(run(os.Args[1:], os.Stdout, os.Stderr))
}
```

Notice the shape: `run` returns an `int`, and **only `main` calls `os.Exit`**, so `run`'s defers
execute (`03-errors-and-validation.md`). Injecting `io.Writer` values makes it easy to test;
see `09-testing-and-debugging.md`.

> **Professional habit.** Keep argument parsing separate from business logic: `run` decides
> *whether input is valid*; other functions decide *what to do with it*.

## 4. Arity checks per command

Each command has its own contract. Write it down as a table before coding, then make the code
match the table exactly. For the generic example above:

| Invocation | `len(args)` | Valid? | Action |
|---|---|---|---|
| `note` | 0 | No | Usage to stderr, exit `2` |
| `note --help` | 1 | Yes (special case) | Usage to stdout, exit `0` |
| `note add "Buy milk"` | 2 | Yes | Add, success to stdout, exit `0` |
| `note add` | 1 | No | "needs one text" + usage, exit `2` |
| `note add "a" "b"` | 3 | No | "too many arguments", exit `2` |
| `note show 3` | 2 | Yes if numeric | Show, exit `0` |
| `note show 0` | 2 | No | "positive whole number", exit `2` |
| `note frobnicate` | 1 | No | "unknown command" + usage, exit `2` |

Two distinct checks are easy to conflate:

- **Arity**: the right *number* of arguments.
- **Content**: the arguments have the right *form* (non-empty text, numeric ID, valid status).

Check arity first. It guarantees `args[i]` exists, so content checks never need index
guards. Use `!=` for fixed arity, and a range like `< 2` for commands with an optional final
argument.

For Task Tracker, build your own table from the documented commands: which take zero, one, or
two arguments, and which have an optional final one? Do not write checks yet — agree on it.

## 5. The shell is part of your program's input pipeline

Go receives a slice of strings, but the **shell** decides what those strings are.

```bash
note add "Buy groceries"     # args: ["add", "Buy groceries"]  — one argument
note add Buy groceries       # args: ["add", "Buy", "groceries"] — three arguments
note add 'Buy $milk'         # single quotes: $milk stays literal
note add "Buy $milk"         # double quotes: shell expands the variable first
note add ""                  # one argument, an empty string
note add *.txt               # shell may expand the glob into many arguments
note add "a\ b"              # one argument containing a backslash and a space
```

Key facts:

- Quotes are **removed by the shell**; your program never sees them.
- A description with spaces must be quoted; mention this in usage text.
- `""` is a real zero-length argument: it passes arity but must fail the content check.
- Wildcards, variable expansion, and history expansion happen in the shell, not Go. Bash-like
  shells are assumed here; Windows shells quote differently.

Do not rejoin extra arguments into a description on the user's behalf; that hides mistakes.
Print a usage hint instead.

## 6. Strings, numbers, and `strconv`

Every element of `os.Args` is a `string`. The digit characters in `"42"` are not the integer
`42`; you must convert them explicitly.

```go
id, err := strconv.Atoi(args[1]) // int; returns an error for bad input
```

| Need | Tool | Notes |
|---|---|---|
| Decimal string → `int` | `strconv.Atoi(s)` | Convenient shorthand for `ParseInt(s, 10, 0)` |
| Explicit base/bit size | `strconv.ParseInt(s, 10, 64)` | Use base `10` so `08` is not treated as octal |
| `int` → string | `strconv.Itoa(n)` | Or `fmt.Sprintf("%d", n)` |
| Validate only | `strconv.Atoi` then range check | `0` and negatives are usually invalid IDs |

`Atoi` returns `0` and an error on failure; check the error first. A bad parse is a **usage
error** (exit `2`), not a data failure (exit `1`). Use `errors.Is` with `strconv.ErrSyntax` or
`strconv.ErrRange` to tell "not a number" from "too large".

Validation order: `parse → range check → existence check`. Each step deserves its own message,
which beats one generic "invalid ID".

## 7. Optional flags: `flag` package (advanced)

Go ships a `flag` package, but you do **not** need it here; it is worth knowing anyway.

```go
verbose := flag.Bool("verbose", false, "print extra detail")
flag.Parse()
rest := flag.Args() // positional arguments left after flag parsing
```

The crucial behavior: **Go's `flag` package stops at the first non-flag argument.**

```bash
note -verbose show 3    # -verbose parsed; rest == ["show", "3"]
note show -verbose 3    # parsing stops at "show"; rest == ["show", "-verbose", "3"]
note -- show 3 -v       # "--" ends flag parsing explicitly
```

So flags must precede positional commands. This differs from some other tools and is a
common source of confusion. With the default `flag.CommandLine`, a bad flag prints usage and
exits with status `2`; `-h` or `-help` prints usage and exits successfully if you have not
defined those names yourself.

If you decide to add flags later, keep them optional and document them. A positional-args
CLI with manual dispatch remains the primary interface for Task Tracker; do not restructure
the documented commands around flags.

## 8. Help, errors, and exit codes

**Usage text** is a short description of valid invocations. It should fit on a terminal
screen and list every command with one line each. Put it in a constant so help and error
paths cannot drift apart.

| Situation | Destination | Exit code | Include usage? |
|---|---|---|---|
| User asked for `--help` / `-h` | stdout | `0` | Yes |
| No command given | stderr | `2` | Yes |
| Unknown command | stderr | `2` | Yes |
| Missing or extra arguments | stderr | `2` | Yes, or a one-line hint |
| Bad ID / bad status / empty description | stderr | `2` | Hint is enough |
| File unreadable, JSON corrupt, ID not found | stderr | `1` | No |
| Success | stdout | `0` | No |

```bash
$ note --help
usage: note <add|show> [text|id]
  note add "text"   add a note
  note show <id>    show a note by id
$ echo $?
0

$ note show abc
id must be a positive whole number, got "abc"
$ echo $?
2
```

Handle `-h` and `--help` **before** the command switch: they are requests for documentation,
not commands. Also decide whether the bare word `help` is accepted; either choice is fine as
long as it is documented and consistent.

Exit code conventions are a contract with scripts:

- `0`: the command did what it was asked to do.
- `2`: the user gave bad instructions (usage). Many tools use `2` here.
- `1`: the instructions were fine but the program could not complete (I/O, data, not found).

Pick one convention, write it in the README, and never change it casually. In shell scripts,
`$?` holds the last exit code; a non-zero code lets `if task-cli ...; then ...` work.

## 9. Success messages and script-friendly output

A success message should confirm the operation and include the ID when one was generated. The
specification suggests `Task added successfully (ID: 1)` — treat that exact shape as the
public contract and keep it stable.

Guidelines for output that plays nicely with people *and* scripts:

- Print one short line per event, ending with a newline.
- Keep machine-relevant values (IDs, statuses) in a consistent position and format.
- Never print prompts, spinners, colors, or emoji when output is redirected to a file.
- Do not print extra celebration text, timestamps, or internal paths on success.
- For lists, print one task per line in a fixed column order.
- Keep stdout free of diagnostics; errors belong on stderr (`03-errors-and-validation.md`).
- Print success **after** the data has been saved, never before.

```bash
$ task-cli add "Buy groceries"
Task added successfully (ID: 1)
$ task-cli list
1  [todo]         Buy groceries
$ echo $?
0
```

If you want both human and machine formats later, add an explicit opt-in such as `--format
json`; do not change the default output silently.

## 10. Optional status filters and the `list` command

`list` is the only documented command with an optional argument:

| Invocation | Meaning |
|---|---|
| `list` | All tasks |
| `list todo` | Tasks not started |
| `list in-progress` | Tasks currently being worked on |
| `list done` | Completed tasks |

Implementation guidance (no code):

- Accept either zero arguments or exactly one; reject two or more with a usage error.
- When one argument is present, validate it against the three allowed status strings before
  reading storage.
- Use the same status names in input and in the JSON file. Do not accept aliases like
  `"complete"` unless you deliberately document them.
- If the filter matches nothing, an empty list is a **successful** result, not an error. Print
  nothing (or a clear "no tasks" line) and exit `0`; decide and document which.

## 11. Idempotency and destructive actions

An operation is **idempotent** if doing it twice has the same final state as doing it once.
`delete 7` is not naturally idempotent: the first call succeeds, the second cannot find the
task.

Choose and document one policy:

| Policy | Second `delete 7` | Pros | Cons |
|---|---|---|---|
| Report "not found" | Error, exit `1` | Honest, catches typos | Scripts must tolerate it |
| Silent no-op | Success, exit `0` | Idempotent and script-friendly | Hides typos |

There is no universally correct answer. For a learning CLI, reporting "no task with ID 7" is
usually clearer. Whatever you pick, apply it consistently to `update` and the mark commands
too.

**Do not add interactive confirmation prompts.** A positional-args CLI must work when stdin
is not a terminal:

```bash
task-cli delete 7 < /dev/null   # a prompt would hang or fail here
```

If you later want a safety gate, make it explicit and optional (for example a documented
`--yes` flag) rather than blocking on input. Since the roadmap specification lists no such
flag, the simplest correct behavior is: validate the ID, delete it, print the result.

## Common mistakes

- Reading `os.Args[1]` before checking `len(os.Args)`, or using `os.Args[0]` as a command.
- Forgetting that a quoted description is one argument, then rejoining extra arguments into
  a description instead of reporting arity errors.
- Treating `""` as "no argument", or calling `strconv.Atoi` without checking its error.
- Accepting negative or zero IDs, or letting `list ""` mean "all tasks".
- Printing errors to stdout, usage to stdout for errors, or help to stderr for success.
- Calling `os.Exit` inside a helper, skipping defers and cleanup.
- Adding a prompt to a destructive command, or defining flags after positional arguments.
- Changing success message wording without noticing scripts depend on it.

> **Professional habit.** Put the command/arity/exit-code table in your README, then test with
> the shell, not just `go run`: quotes, spaces, empty strings, unknown commands, and `echo $?`.

## Apply it to Task Tracker

No solution code here. Produce these artifacts in your notes first.

1. **The contract table.** For `add`, `update`, `delete`, `mark-in-progress`, `mark-done`, and
   `list`, write: exact command shape, argument count, content rules, success output, error
   output, and exit code. Use the shapes in `Requirements.txt` verbatim.
2. **Help text.** Draft a short usage block that fits in one screen. Does it mention that
   descriptions with spaces need quotes? Does it show all three status filters?
3. **Dispatch plan.** Which layer owns the `switch` on `os.Args[1]`? Which layer converts the
   ID string to an integer? Which layer looks up the task? Use `07-architecture-and-project-layout.md`
   to decide where those functions live.
4. **Error path walkthrough.** For `update abc "new text"`, trace the exact checks in order
   and write the exact stderr line. Repeat for `update 999 "new text"` and `list doing`.
5. **Exit code policy.** Write down which failures are `1` and which are `2`, then confirm
   that every error path in your table has one code.
6. **Idempotency decision.** What happens on the second `delete 7`? What about the second
   `mark-done 7`? Justify your choice in one sentence.
7. **Script test plan.** List five shell commands to run against your finished CLI, each with
   the expected stdout, stderr, and `$?`. Include at least one quoted description and one
   redirect like `2>/dev/null`.
8. **Help, prompts, and output stability.** Decide where `--help` works, confirm no path waits
   for stdin, and mark the exact success strings you are committing to.

Checklist before you call this layer done:

- [ ] Every command enforces its arity before indexing arguments.
- [ ] `--help` and `-h` succeed; errors go to stderr with a non-zero exit code.
- [ ] Success output matches the specification and includes generated IDs.
- [ ] The CLI behaves the same when stdin is closed or output is redirected.

## Checkpoint

Answer without scrolling back:

1. What does `os.Args[0]` contain, and why should you ignore it for command dispatch?
2. Why must you check `len(os.Args)` before reading `os.Args[1]`?
3. What does "positional argument" mean, and why does the spec ask for them?
4. Describe the shell's role in `note add "Buy milk"` — what does Go actually receive?
5. Why should extra arguments not be rejoined into a description?
6. What is the difference between an arity error and a content error?
7. Why does `flag` require flags before positional arguments?
8. Which exit code would you use for `list doing`, and where would the message go?
9. What does idempotent mean, and why avoid interactive confirmation prompts?

Next: `06-time-and-timestamps.md`
