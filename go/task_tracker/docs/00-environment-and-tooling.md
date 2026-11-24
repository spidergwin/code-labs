# 00 — Environment and Tooling

> **Why this matters for Task Tracker**
>
> Before modeling a task you must be able to run Go code, know where your program is *running
> from*, and find answers without the internet. Task Tracker stores `tasks.json` **in the
> current working directory**, not next to the source code. Nearly every "where did my file
> go?" bug comes from confusing the directory you launch a program from with the directory the
> binary lives in. This chapter builds the toolkit the rest of the path assumes.

## What you will learn

- What Go is and what the `go` command does
- Modules, `go.mod`, module paths, and import paths
- How `go run`, `go build`, and `go install` differ
- `package main` and `func main`
- `gofmt`, `go vet`, `go doc`, and `go help` as daily tools
- `GOROOT` and `GOPATH` in modern module mode
- Working directory versus binary directory (critical for `tasks.json`)
- Terminal, editor, and Git basics; this repository's layout

---

## 1. Go and the toolchain

Go is a compiled, statically typed language. **Compiled** means source is translated to a native
executable **binary** before it runs; there is no interpreter in the loop. **Statically typed**
means every value has a type known at compile time, so the compiler catches mistakes early.
The language is small, but its **standard library** (bundled packages like `fmt`, `os`,
`encoding/json`, `time`) covers file I/O, JSON, and testing. Task Tracker uses **only** the
standard library. The `go` command also supplies formatting, building, testing, and docs: no
build system to assemble. Goroutines and channels exist but are irrelevant to this project.

The **toolchain** is that set of programs. Verify it:

```bash
go version
# go version go1.27.3 linux/amd64

go env GOOS GOARCH    # target operating system and CPU
go env GOROOT         # where Go itself is installed
go env GOPATH         # where modules and installed binaries go by default
```

`GOOS`/`GOARCH` explain suffixes like `task-cli-linux-amd64`. Go detects these automatically;
do not set them by hand. In modern **module mode**, your project does not need to live inside
`$GOPATH`: that directory mainly holds the module cache and installed binaries.

---

## 2. Modules, module paths, and import paths

A **module** is a directory tree with a `go.mod` at its root. Ours says:

```go
module task_tracker

go 1.27.3
```

`module task_tracker` declares the **module path**: the canonical name of this module and the
prefix of every import path in it. `go 1.27.3` is the minimum expected Go version, a
compatibility statement rather than a pin to edit.

An **import path** is the string you write in an `import (...)` block to name another package.
Here, code in the `internal/task` directory is imported as `task_tracker/internal/task` — never
as a filesystem path like `./internal/task`. In short: the module path is the root prefix; an
import path is that prefix plus a directory path.

The module path is a plain word, not a domain, so no network lookup is needed. During the required
CLI track there is no `go.sum` (checksums of downloaded modules) because **we have no external
dependencies**. If `go` attempts a network fetch at that stage, stop: you probably imported
something outside the standard library. In the optional API track, `go get github.com/go-chi/chi/v5`
intentionally adds a dependency and a `go.sum`; that is a separate, documented phase.

```bash
go help modules        # the module system, explained by Go itself
go list ./...          # list all packages in this module
go list -m             # print the current module path
```

`./...` means "this directory and all subdirectories"; you will see it in `go vet ./...` and
`go test ./...` constantly.

---

## 3. `package main`, `func main`, and build commands

Every Go file begins with a `package` clause. A **package** is a set of `.go` files compiled
together and sharing one namespace. A package named `main` builds an executable; execution
starts at `func main()` (no parameters, no result, exactly one per program):

```go
package main

import "fmt"

func main() {
    fmt.Println("Hello from Go")
}
```

| Command | Effect | Leaves a binary? |
|---|---|---|
| `go run .` | Compiles to a temporary binary and executes it now | No |
| `go build .` | Writes a binary named after the directory into the current directory | Yes |
| `go build -o /tmp/go-scratch/hello .` | Writes the binary where you specify | Yes |
| `go install .` | Builds and copies the binary to `$GOBIN` or `$GOPATH/bin` | Yes |
| `go test ./...` | Builds and runs tests in every package | No |

For this project you will use `go run ./cmd/cli list`, `go build -o /tmp/go-scratch/task-cli
./cmd/cli`, `go vet ./...`, and `go test ./...`. Everything after the package path belongs to
your program: `go run ./cmd/cli add "Buy groceries"` passes two positional arguments, which
arrive in `os.Args` (see `05-cli-arguments-and-ux.md`).

> **Professional habit — build before you debug.** Run `go build ./...` when a runtime error is
> confusing. A clean build proves the compiler is happy and narrows the problem to logic,
> files, or the environment.

---

## 4. Working directory versus binary directory (critical)

The **working directory** is where your shell — and therefore your program — is "standing";
`pwd` prints it. A **relative path** like `tasks.json` is resolved against that directory, not
against the source file and not against the binary's folder.

- `cd /tmp/go-scratch && go run <path>/main.go add "Buy milk"` creates
  `/tmp/go-scratch/tasks.json` if the code uses that relative name.
- Build to `/tmp/go-scratch/task-cli`, then run it from your home directory: it reads or
  creates `tasks.json` in your **home**. The binary's own location is irrelevant.
- Tests must not depend on the developer's directory; `09-testing-and-debugging.md` introduces
  `t.TempDir()` for that.

Inspect the behavior from Go:

```go
package main

import (
    "fmt"
    "os"
)

func main() {
    wd, err := os.Getwd()
    if err != nil {
        fmt.Fprintln(os.Stderr, "cannot read working directory:", err)
        os.Exit(1)
    }
    fmt.Println("working directory:", wd)
    fmt.Printf("arguments: %q\n", os.Args)
}
```

Run it from two directories. `os.Exit(1)` sets a non-zero **exit status**, how a CLI tells the
shell "this failed".

> **Professional habit — decide the data location explicitly.** Write down: "the store file is
> `tasks.json` relative to the process working directory". Paths, tests, and README examples
> then follow from one decision.

---

## 5. Formatting, vetting, and offline documentation

`gofmt` is the official formatter: indentation, spacing, and import order, with no change in
meaning. Enable format-on-save in your editor.

```bash
gofmt -l .        # list unformatted files; prints nothing when clean
gofmt -w .        # format files in place, recursively
gofmt -d file.go  # show a diff without writing
```

`go vet` reports code that compiles but is probably wrong (bad `fmt` verbs, unreachable code).
Run `go vet ./...` before every commit. It is a fast sanity check, not a replacement for tests.

`go doc` prints package documentation; `go help` explains the `go` command. Both are offline:

```bash
go doc fmt                 # package overview
go doc fmt.Printf          # one function and its signature
go doc os.WriteFile
go doc encoding/json
go doc time.Time           # a type and its methods
go help run
go help build
```

When you meet an unfamiliar function: read its signature with `go doc`, note its return values
(in Go, errors are returned, not thrown), write a ten-line scratch program, then read
`$(go env GOROOT)/src/<package>` if a question remains. The standard-library source is a
legitimate primary reference already on disk.

---

## 6. Terminal and editor basics

| Action | Command | Note |
|---|---|---|
| Show where you are | `pwd` | Do this whenever paths feel strange |
| Change directory | `cd path`, `cd ..` | Relative paths follow you |
| List files | `ls -la` | `-a` shows dotfiles such as `.git` |
| Run from another directory | `cd /tmp && ...` | `&&` means "then, if that succeeded" |
| Check exit status | `echo $?` | `0` = success, non-zero = failure |
| Quote an argument with spaces | `add "Buy groceries"` | Without quotes the shell splits it |

Set up your editor once: install Go syntax support, enable `gofmt` on save, open the **folder**
(not a lone file) so imports resolve in context, and keep a terminal beside the editor. Edit,
save, run, observe — repeat. When a command fails, read the *first* error line: Go compiler
errors give a file, a line, and often the fix.

---

## 7. Git basics for this repository

Git is a **version control system**: it stores snapshots so you can review, undo, and explain
changes. The repository containing this project is rooted at the workspace above it; `git
rev-parse --show-toplevel` prints the root. Commands work from any directory inside it.

```bash
git status --short           # one line per change
git diff                     # unstaged changes, line by line
git add docs/00-environment-and-tooling.md
git commit -m "Add environment chapter"
git log --oneline -5
```

The **working tree** is your current files; the **staging area** holds what `git add` selected;
a **commit** is a named snapshot; **untracked** means Git has never seen the file. Commit after
each passing checkpoint. Write messages that say *why* (`Use relative path for task store`
beats `stuff`). Do not commit binaries or a local `tasks.json`: add them to `.gitignore` and
read `git status` before using `git add .`.

---

## 8. Layout of this repository

```text
task_tracker/
├── go.mod                         module definition (already written)
├── cmd/
│   ├── cli/main.go                CLI entry point — you will grow this
│   └── api/main.go                future HTTP API — ignore
├── internal/
│   ├── task/{model,repository,service}.go   empty slots for your code
│   ├── storage/jsonstore/store.go           empty slot for JSON file access
│   ├── storage/sqlitestore/                 future work — ignore
│   ├── transport/cli/handler.go             empty slot for argument handling
│   └── transport/api/                       future work — ignore
└── docs/
    ├── Requirements.txt           the project brief
    ├── LEARNING_PATH.md           master index and ground rules
    └── 00–11 chapters             your tutor and reference
```

`cmd/` holds executable `main` packages. `internal/` restricts imports to this module (covered
in `07-architecture-and-project-layout.md`). Empty files are intentional slots, not mistakes;
fill them gradually following `08-feature-by-feature.md`. Use only `storage/jsonstore`.

---

## 9. Offline reference habits

Keep this cheat sheet in your notes; it is the answer to "what if I get stuck?":

```bash
go doc <package>           # package overview
go doc <package>.<Symbol>  # function, type, constant, or method
go doc -all <package>      # everything, including methods
go help <topic>            # go command help
go env GOROOT              # root of the local installation
ls "$(go env GOROOT)/src"  # standard-library source, offline
```

Prefer `go doc` over memory: knowing how to look something up beats reciting it. Run `go doc`
for each new function a chapter mentions. Never install a third-party package for something
the standard library already does. Keep a personal scrap note of "function → returns → example"
and review it before each session.

---

## 10. First 20 minutes: hands-on sequence

Use scratch files under `/tmp/go-scratch` so mistakes are free. Do not edit project files yet.

1. `mkdir -p /tmp/go-scratch && cd /tmp/go-scratch`. Create `hello.go` with the five-line
   program from section 3, then run `go run hello.go`. Expected: `Hello from Go`.
2. Break it: change `fmt.Println` to `fmt.Printline`, run again, read the compiler error, fix
   it. Compiler errors are instructions, not judgments.
3. Run `go mod init scratch`, read the generated `go.mod`, compare it with the project's, and
   try `go doc fmt.Println` and `go help run`. No network involved.
4. Retype the `os.Getwd()` program into `where.go`. Run `go run where.go one "two words"` from
   `/tmp/go-scratch`, then from your home directory with an absolute path. Compare output.
5. `go build -o /tmp/go-scratch/where where.go`, then `cd /tmp && /tmp/go-scratch/where alpha`.
   It prints `/tmp` as the working directory, not the binary's folder. This is the key
   observation of the chapter.
6. Add messy spacing to `hello.go`; run `gofmt -d hello.go`, `gofmt -w hello.go`, `gofmt -l .`,
   and `go vet ./...` in both directories. Clean output means no findings.
7. Make a program that calls `os.Exit(3)`; run it, then `echo $?`. Expected: `3`.

---

## Common mistakes

| Mistake | Symptom | Fix |
|---|---|---|
| Importing `"./internal/task"` | Bad import path error | Use `task_tracker/internal/task` |
| Assuming `tasks.json` lands beside `main.go` | File appears elsewhere, or tests see an old one | Relative paths follow the working directory |
| `go build .` from the wrong directory | "no Go files in ..." | Check `pwd`; pass `./cmd/cli` |
| Installing a library for JSON or files | Network errors, `go.sum` appears | Use `encoding/json` and `os` |
| Editing formatting by hand | Whitespace-heavy diffs | `gofmt -w .`, enable format-on-save |
| Committing binaries or `tasks.json` | Noisy Git status | Add to `.gitignore` |

---

## Professional habit

> **Look it up locally, verify mechanically, commit narrowly.**
> Before code: `go doc` the function. After code: `gofmt -l .`, `go vet ./...`, run it.
> When it works: one focused commit explaining the change. Seconds spent here save hours.

---

## Apply it to Task Tracker

Answer in notes; verify with commands. Do **not** write implementation code yet.

1. What exact command runs the CLI from `task_tracker/` with the argument `list`? How do you
   pass `add` and `Buy groceries` as one description argument?
2. Predict where `tasks.json` appears for: `go run ./cmd/cli add "Buy milk"` from the project
   root; the same from another directory; a built binary in `/tmp/go-scratch` launched from
   `/tmp`. Verify with a throwaway `os.Getwd()` program.
3. From `go.mod` and the layout, write the import path of `internal/storage/jsonstore`. Check
   with `go list ./...`.
4. Should `tasks.json` be committed or ignored? What does `git status --short` show after you
   create it?
5. Write your per-session checklist: at least `go doc`, `go help`, `gofmt -l .`, `go vet ./...`,
   `go run`, and `git status --short`.
6. Explain in one sentence why a third-party JSON library breaks this project's constraints.

---

## Checkpoint

1. What does the `go 1.27.3` line in `go.mod` mean?
2. What is the difference between a module path and an import path here?
3. Which command compiles and runs immediately, and which installs onto `PATH`?
4. Why must a runnable package be named `main` and define `func main()`?
5. What does `gofmt -l .` print when everything is formatted?
6. Which command shows `os.WriteFile`'s signature offline?
7. A binary in `/tmp/go-scratch` writes `tasks.json` when launched from your home directory:
   where does the file go, and why?
8. What does `echo $?` report after success and after `os.Exit(1)`? What does `./...` mean?

---

Next: `01-go-basics.md`
