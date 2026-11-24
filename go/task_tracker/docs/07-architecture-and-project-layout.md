# 07 — Architecture and project layout

> **Jargon first.** A *package* is a directory of Go files compiled and imported as one
> unit. An *exported* identifier starts with a capital letter and is visible from other
> packages; an *unexported* one starts lowercase and is visible only inside its own
> package. An *interface* is a named set of method signatures — a description of behavior,
> not an implementation. *Dependency direction* is the direction of "this code imports
> that code"; it should point from detailed, changeable concerns toward stable abstractions.

## Why this matters for Task Tracker

A 7-command CLI can be written in one `main.go`. It usually becomes a single 600-line file
where CLI parsing, business rules, and file I/O are tangled together. Then a tiny change —
"reject empty descriptions", "switch from JSON to SQLite", "test `add` without touching
disk" — forces you to read and understand all 600 lines.

The repository you were given already has a sensible skeleton: `cmd/cli`, `internal/task`,
`internal/storage/jsonstore`, and `internal/transport/cli`. This chapter explains why those
directories exist, what belongs in each empty file, and how to keep the design honest
without building a framework for a weekend project.

## What you will learn

- What structure buys you even in a small program, and what it costs.
- Go package and visibility rules, including the `internal/` restriction.
- How `go.mod` determines import paths, and why relative imports are wrong.
- Why `cmd/<name>` is the conventional home of a binary.
- The transport → service → repository/storage layering and its dependency direction.
- Why the consumer of an interface should define it, and why small interfaces win.
- What an import cycle is, how to recognize one, and how to avoid it.
- File naming, package naming, and when to split a file.
- What each empty file in this skeleton is for (responsibilities only, no code).
- How injecting a store and a clock makes the design testable.
- Where simplicity ends and over-engineering begins.
- How the current JSON store can later be replaced without touching the CLI.

---

## 1. Why structure matters at this size

| If you... | Then... |
|---|---|
| Put everything in `main.go` | You cannot test rules without running the whole program; every change touches everything |
| Separate parsing, rules, and storage | Each part has one reason to change, so you can reason about it alone |
| Add interfaces everywhere, factories, and a DI container | You spend more time on plumbing than on the CLI |
| Follow the skeleton's three layers | You get most of the benefit for a small, explicit cost |

The goal is *legibility*, not architecture points. You should be able to answer "where do I
put a new status-validation rule?" without hesitation. Structure gives you that answer.

A useful test: if changing the storage format would require editing the CLI parser, the
layers have leaked into each other. If adding a new command requires touching five files
that each do a little bit of everything, the boundaries are in the wrong place.

## 2. Packages and visibility

Each directory is a package. All `.go` files in one directory must declare the same package
name (except `_test.go` files may use the `_test` suffix). `internal/task/model.go` and
`internal/task/service.go` both say `package task`; they are one package, so an unexported
name in one file is visible in the other. **Package scope, not file scope, is what matters.**

Visibility rules:

| Identifier | Visible where |
|---|---|
| `Task`, `Status`, `NewStore` (capitalized) | Any package that imports this one |
| `task`, `loadAll`, `nextID` (lowercase) | Only inside the declaring package |

Keep the exported surface small. A good default: export what other layers genuinely need,
keep helpers lowercase, and let tests in the same package exercise unexported code. Tests
using `package task_test` can only see exported names, which is a useful way to check that
your public API is sufficient. The `go doc` output for a package shows only exported names,
which makes it a quick API review tool.

## 3. Import paths come from `go.mod`

The first line of `go.mod` is:

```text
module task_tracker
```

That module path is the prefix of every import in this repository. The package in
`internal/task` is imported as `task_tracker/internal/task`, and the JSON store as
`task_tracker/internal/storage/jsonstore`. There are no relative imports in Go: you never
write `../task` or `./internal/task`. If an import path looks wrong, check it against the
module line rather than guessing.

Useful offline commands:

```text
go list ./...                 # all packages in this module
go list ./internal/task       # confirm a package's import path
go doc task_tracker/internal/task
```

`go doc <pkg>` is your local API browser. No internet required.

## 4. The `internal/` rule

Go enforces a visibility boundary for directories named `internal`: code **outside** the
parent of `internal` cannot import anything beneath it. Here the parent is the module root,
so every package in `task_tracker` may import `task_tracker/internal/task`, but some other
module on the same machine may not. That is deliberate: these packages are implementation
details of this program, not a public library.

Two consequences for you:

- Empty files under `internal/` are not dead weight; they are reserved implementation slots.
- `internal/storage/sqlitestore` and `internal/transport/api` are placeholders for a future
  that is out of scope. Leave them alone. Importing them into the CLI now would add
  complexity with no behavior to show for it.

## 5. `cmd/` holds binaries

The `cmd/` convention is: one subdirectory per executable, named after the binary.
`cmd/cli/main.go` is the entry point of our CLI; `go build -o task-cli ./cmd/cli` produces
a binary named `task-cli`. `cmd/api/main.go` is a placeholder to ignore.

Keep `main.go` thin. Its job is to collect process-level inputs (`os.Args`), hand them to the
CLI transport layer, and turn an error into a printed message plus a non-zero exit code.
Argument parsing details live in `05-cli-arguments-and-ux.md`. Business rules do not live
in `main.go`.

## 6. The three-layer picture

```text
                       ┌──────────────────────────────┐
 user types a command  │ cmd/cli/main.go              │  process entry
                       │ (args, exit code, stderr)    │
                       └──────────────┬───────────────┘
                                      │ calls
                       ┌──────────────▼───────────────┐
                       │ internal/transport/cli       │  TRANSPORT
                       │ parse args, format output    │  "what did the user ask?"
                       └──────────────┬───────────────┘
                                      │ calls
                       ┌──────────────▼───────────────┐
                       │ internal/task (service.go)   │  SERVICE
                       │ validate, assign IDs, rules  │  "what should happen?"
                       └──────────────┬───────────────┘
                                      │ calls an interface it declares
                       ┌──────────────▼───────────────┐
                       │ internal/task/repository.go  │  CONTRACT
                       │ small interface: load/save   │  "what storage can I use?"
                       └──────────────▲───────────────┘
                                      │ implements
                       ┌──────────────┴───────────────┐
                       │ internal/storage/jsonstore   │  REPOSITORY / STORAGE
                       │ read/write tasks.json        │  "where are the bytes?"
                       └──────────────────────────────┘
```

`internal/task/model.go` is used by all three layers: the type that describes a task is the
shared vocabulary.

The arrow from service to repository is a *call*: the service asks something to load and
save tasks. The arrow from `jsonstore` up to the interface is an *implementation*: the
concrete store satisfies the contract the service declared. The service does **not** import
`jsonstore`; `main.go` wires them together once at startup. That one act of wiring is the
whole dependency-injection "framework" you need.

### Why the consumer defines the interface

A common instinct is to put an interface next to its implementation. In Go the more useful
rule is: **define the interface in the package that consumes it**, described in terms of
what that consumer needs. Benefits:

- The interface stays small because only one consumer's needs shape it.
- The consumer no longer imports the concrete package, so changing storage does not
  recompile or rewrite the service.
- Alternative implementations (a fake in tests, SQLite later) can be written without
  changing the consumer at all.

Interfaces should be *small and at the point of use*. If the service only needs "read all
tasks" and "write all tasks", that is the interface. Do not include delete-by-id, search,
transactions, or pagination "just in case"; every extra method is another thing a test fake
must implement and another promise the storage layer must keep.

### Import cycles

An import cycle is `A imports B imports A`. Go rejects it at compile time with a clear
error, but the fix is conceptual: move the shared contract into the consumer package or a
small third package. For example, if `task` imports `jsonstore` for a store type, and
`jsonstore` imports `task` for the `Task` type, you have a cycle. Break it by letting
`jsonstore` import `task` for the data type, while `task` defines a small interface and
never imports `jsonstore`. The concrete wiring lives in `main.go`, which imports both.

## 7. Mapping the skeleton files to responsibilities

No code here — these are job descriptions for files you will fill in.

| File | Responsibility | Should NOT do |
|---|---|---|
| `cmd/cli/main.go` | Read `os.Args`, call the CLI handler, print errors, choose exit code | Validate task rules or touch `tasks.json` |
| `internal/transport/cli/handler.go` | Interpret positional commands and arguments, call the service, format user-facing output | Decide ID policy or know how JSON is encoded |
| `internal/task/model.go` | Define the task type, status constants/type, JSON field names, and tiny helpers about the data | Read files or print to stdout |
| `internal/task/service.go` | Business rules: validate descriptions and IDs, assign IDs, set both timestamps, find a task by ID, dispatch to the store | Parse `os.Args` or format CLI messages |
| `internal/task/repository.go` | Declare the minimal storage contract the service consumes | Implement file I/O |
| `internal/storage/jsonstore/store.go` | Implement that contract against `tasks.json` in the current directory: load, decode, encode, atomic replace, bootstrap if missing | Decide what a valid description is |
| `internal/storage/sqlitestore/store.go` | Placeholder — ignore | Anything in this project |
| `internal/transport/api/*` | Placeholder — ignore | Anything in this project |

If you find a responsibility in the wrong column, move it before adding more features. The
cost of a wrong boundary grows with every command you add.

## 8. File and package naming

- **Package names:** short, lowercase, singular, no underscores. `task`, `jsonstore`,
  `cli`. A directory like `jsonstore` naturally yields package `jsonstore`; avoid
  `json_store` and `jsonStore`.
- **File names:** lowercase with underscores, descriptive of content: `model.go`,
  `service.go`, `store.go`, `store_test.go`. Test files end in `_test.go`.
- **Test package choice:** use `package task` for tests that need internal helpers, or
  `package task_test` to test only the exported API. Both are legitimate; be deliberate.
- **When to split a file:** when it has two distinct reasons to change (for example, model
  definitions and validation rules), when it grows past roughly 200 lines, or when a
  helper is used by many files in the package. Splitting early is cheap; splitting after
  duplication is not.
- **When NOT to split:** do not create one file per function or an `interfaces.go` with a
  single interface. That is noise, not structure.

`gofmt` and `go vet` (see `10-professional-habits.md`) do not enforce layout, so these
choices are yours to keep honest.

## 9. Designing for testability

Two dependencies make service logic hard to test: the clock and the store. Inject both.

Conceptually, the service should be able to receive:

| Dependency | Production value | Test value |
|---|---|---|
| Store | The JSON store instance built in `main.go` | A tiny in-memory fake that implements the same small interface |
| Clock | A function returning current UTC time | A function returning one fixed instant |

Generic illustration in a different domain:

```go
package note

import "time"

type store interface {
	Save(Note) error
}

type Service struct {
	store store
	now   func() time.Time
}
```

`note.Service` depends on a two-method-world it defines itself. A test can supply a fake
`store` and a fixed `now` without creating any files or sleeping. Production wires the real
store and `time.Now`. That is the entire pattern; no external libraries in the CLI track, no
reflection, no global mutable variables.

Two cautions:

- Do not inject every standard-library call. Inject things that are nondeterministic or
  have side effects (time, filesystem, network).
- If a constructor receives `nil` for a dependency, decide deliberately whether to default
  it or reject it. Silent nils cause panics far from their cause. `03-errors-and-validation.md`
  covers error handling patterns.

## 10. Simplicity versus over-engineering

For this project, **keep it simple**:

- One concrete store (`jsonstore`) and one small interface it satisfies.
- One service package holding rules for all seven commands.
- One handler translating positional arguments into service calls.
- Plain functions and structs. No generics unless they remove real duplication, no builder
  pattern, no event bus, no config package, no logging framework.

Signs you have gone too far: you cannot explain why a file exists; you have more interfaces
than commands; adding a command means editing four layers that all repeat the command name;
tests need a framework to construct the service.

Signs you have gone too shallow: a function cannot be tested without creating a real file;
the handler contains business rules; `main.go` knows the JSON layout; changing output format
would require editing validation code.

When in doubt, prefer the version you can read top to bottom in one sitting.

## 11. Evolution path: JSON now, SQLite later

The placeholders hint at a future where storage changes. Here is why the design survives it:

1. Today: `main.go` creates a `jsonstore` and passes it to the service through the small
   interface. Timestamps are UTC RFC3339 strings; the schema lives in `model.go`.
2. Later: write a `sqlitestore` that implements the same two or three methods. It may store
   times as text or integers internally, but it must present the same task type to the
   service.
3. Change one line of wiring in `main.go`. The service, the model, and the CLI handler do
   not change because they never depended on the concrete store.

The same reasoning applies to the API placeholder: an HTTP transport could call the same
service methods and format JSON responses, with no duplication of business rules. You are
not asked to build it. You are asked to avoid making it impossible.

## 12. Common mistakes

| Mistake | Why it hurts | Better habit |
|---|---|---|
| Importing `jsonstore` from `service.go` | Service is tied to file I/O and hard to test | Define a small interface in `task`, wire in `main.go` |
| Global mutable state for the task list | Tests leak into each other; order matters | Pass the store and clock explicitly |
| `interface {}` everywhere | No type safety, unclear contract | Name a small interface with the exact methods needed |
| One giant `main.go` | Cannot test rules independently | Move rules to `service.go`, parsing to the handler |
| Interface with ten speculative methods | Every fake becomes ten methods | Add methods when a real consumer needs them |
| Capitalizing everything "for tests" | Public API becomes noise | Export only what other packages need |
| Relative imports like `../task` | Does not compile | Use `task_tracker/internal/task` |
| Editing placeholder packages "to be thorough" | Unbounded scope | Leave them empty until the project asks for them |

### Professional habit

Before each phase, write one sentence: "This change belongs in ___ because ___." If the
sentence is hard to write, the boundary is wrong. Fix the boundary before writing code.

### Professional habit

Run `go list ./...` after adding a package. It is a fast way to see whether the module sees
your new directory and whether you accidentally introduced an import cycle.

## 13. Apply it to Task Tracker

Guidance and questions — no implementation code.

- Sketch the dependency diagram above on paper, then draw an arrow from each empty file to
  the files it is allowed to import. Does any arrow point "up" into the CLI?
- Decide which package owns the rule "an ID must exist before it can be updated or
  deleted". Why is that a service rule rather than a CLI rule?
- Decide which package owns "print `Task added successfully (ID: 1)`". Why is that a
  transport concern? Where is that exact wording specified?
- Look at `internal/task/model.go`. What is the minimum information the model must carry
  to satisfy the five required properties? What behavior (if any) belongs with it?
- Look at `internal/task/repository.go`. What are the fewest operations that can support
  all seven commands? Remember that `list` with filters needs only "read everything" if
  filtering is a service rule.
- Look at `internal/storage/jsonstore/store.go`. Which operations involve file creation,
  decoding, encoding, and atomic replacement? Where would you put a test double?
- Decide how `main.go` will hand dependencies to the handler and service. Who constructs
  the store? Who constructs the service? Who owns the clock?
- Ask yourself: if you replaced `tasks.json` with SQLite tomorrow, which files would you
  actually edit? Write the list down; it should be short.
- Read `08-feature-by-feature.md` once now, before coding. Notice how each phase touches a
  predictable subset of these files. That predictability is the payoff of this chapter.

## 14. Offline reference

```text
go help modules
go help packages
go list ./...
go doc task_tracker/internal/task
go doc time.Time
```

The Go language spec sections on packages and imports are also installed locally; search
your GOROOT documentation if you want the formal wording of the `internal/` rule.

## Checkpoint

Answer without scrolling:

1. Which import path refers to the `task` package, and where does it come from?
2. What does the `internal/` directory prevent, and who can still import these packages?
3. Why should `service.go` not import `jsonstore`?
4. What is an import cycle and how would you break one between `task` and `jsonstore`?
5. Name one responsibility each for `main.go`, `handler.go`, `service.go`,
   `repository.go`, `model.go`, and `store.go`.
6. Which two dependencies would you inject to make the service testable, and what would
   the test values be?
7. What is the smallest interface that can support all seven commands?
8. What would you need to change to move from JSON to SQLite under this design?

**Next:** `08-feature-by-feature.md`
