# 11 — Troubleshooting and Cheatsheet

## Why this reference matters for Task Tracker

You do not need the internet while building this CLI. You need a fast way to answer three
questions: *what does this syntax mean?*, *why did the compiler/runtime say that?*, and *which
standard-library function do I use?* This chapter is that lookup table. Keep it open in a second
window while you work through `08-feature-by-feature.md`.

This is a **reference**, not a tutorial: the snippets are isolated syntax reminders, never a
finished Task Tracker implementation. For reasoning about tests, see
`09-testing-and-debugging.md`; for code-quality rules, see `10-professional-habits.md`.

## What you will learn

- The exact commands to run, format, vet, test, and inspect the toolchain offline.
- A compact syntax map for the Go features used in this path.
- What each common `fmt` verb does, especially `%w`.
- How to read the compiler's and runtime's most frequent complaints.
- JSON, time, argument, and conversion pitfalls specific to this project.
- Definitions for 40+ Go terms used across the path.
- A numbered debugging checklist you can follow when stuck.

---

## 1. Quick command reference

Run these from the module root (`go.mod` lives there):

| Command | Purpose |
|---|---|
| `go run ./cmd/cli add "Buy milk"` | build and run the CLI in one step |
| `go build ./...` | compile all packages without writing a binary |
| `go build -o /tmp/go-scratch/tasktracker-cli ./cmd/cli` | build a named binary for smoke tests |
| `go test ./...` | run all tests |
| `go test -v -run TestName ./...` | run named tests verbosely |
| `go vet ./...` | static checks included with Go |
| `gofmt -l .` / `gofmt -w .` | list / rewrite unformatted files |
| `go doc <package>` | package documentation, e.g. `go doc encoding/json` |
| `go doc <package>.<Symbol>` | symbol documentation, e.g. `go doc os.Rename` |
| `go help <command>` | tool documentation, e.g. `go help testflag` |
| `go mod tidy` | synchronize `go.mod`/`go.sum` with imports (CLI track has no third-party deps) |
| `go env GOROOT` | locate the local standard-library source under `src/` |

During the CLI track there is no `go.sum` because there are no external modules. If `go mod tidy`
adds one, an accidental third-party import slipped in. The optional API track is different:
chapter `12` deliberately adds chi, and `go.sum` then records its checksum.

---

## 2. Syntax cheat sheet

| Concept | Form | Remember |
|---|---|---|
| Variable | `var name T = value` or `name := value` | `:=` declares **and** initializes; it needs a new variable on the left |
| Constant | `const MaxRetries = 3` | Untyped constants adapt to context; no `:=` for constants |
| Function | `func Name(a int, b string) (int, error)` | Results are named or unnamed; return values must match the signature |
| Method | `func (s *Store) Load() ([]Task, error)` | The part in parentheses is the **receiver**; pointer means it can mutate |
| Interface | `type Closer interface { Close() error }` | Satisfied implicitly; no `implements` keyword |
| Struct | `type Note struct { ID int; Text string }` | Fields are written on separate lines in real code |
| Composite literal | `Note{ID: 1, Text: "hi"}` or `Note{}` | Missing fields get their zero value |
| Pointer | `p := &n`; `*p` dereferences | `&` takes the address; `*` reads/writes through it |
| Slice | `[]int{}`, `make([]int, 0, 10)` | Length vs capacity: `len` can grow up to `cap` without reallocating |
| Array | `[3]int{}` | Fixed length; rarely used directly in this project |
| Map | `map[string]int{}`, `make(map[string]int)` | Nil map panics on write; use `make` first |
| `range` | `for i, v := range slice` / `for k, v := range m` | Map order is random; omit a value with `_` |
| `if` | `if err != nil { ... } else { ... }` | The condition needs no parentheses; braces are mandatory |
| `for` | `for i := 0; i < n; i++` / `for condition {}` / `for {}` | Go has only one loop keyword |
| `switch` | `switch s { case "todo": ...; default: ... }` | No automatic fallthrough; use `fallthrough` explicitly |
| Type conversion | `string(b)`, `int64(n)`, `Status(raw)` | Conversion, not casting; must be allowed by the types |
| `defer` | `defer f.Close()` | Runs at function exit, LIFO; check errors inside a closure if needed |
| Import | `import ("os"; "fmt")` | Unused imports are compile errors, not warnings |
| Package | `package task` at the top of every file | `internal/` limits imports to this module |

---

## 3. Format verbs

From `go doc fmt`. Use them in `fmt.Printf`, `fmt.Sprintf`, `fmt.Fprintln`, and `fmt.Errorf`.

| Verb | Meaning | Typical use here |
|---|---|---|
| `%v` | Default representation of any value | `t.Errorf("got %v, want %v", got, want)` |
| `%+v` | Like `%v`, but structs include field names | Inspecting a decoded task |
| `%#v` | Go-syntax representation, including the type | Precise debugging of nested values |
| `%d` | Signed decimal integer | IDs and counts |
| `%s` | String or byte slice as text | Description or file contents |
| `%q` | Double-quoted string with escapes | Showing user input; empty strings stay visible |
| `%t` | `true` or `false` | Flags and boolean conditions |
| `%w` | Wrap an error so `errors.Is`/`errors.As` still work | **Only** inside `fmt.Errorf` |
| `%T` | Dynamic type of a value | "Why is this a `string` and not an `int`?" |
| `%p` | Pointer representation in hexadecimal | Checking whether two pointers are the same |

`fmt.Fprintln(os.Stderr, "value:", x)` uses `%v` implicitly. Keep normal program output on
`stdout` and diagnostics on `stderr`.

---

## 4. Common compiler errors

The compiler reports the **first** problem; fix it before reading the rest, then rebuild.

| Message (shortened) | Typical cause | Fix |
|---|---|---|
| `undefined: x` | Typo, missing import, wrong capitalization, or name declared in another package | Check spelling and visibility; add the import; use `go doc` for the real name |
| `x declared and not used` | A variable or local function was declared but never read | Use it, remove it, or assign to `_` if deliberately discarding |
| `imported and not used: "strconv"` | Import has no reference in the file | Remove the import or actually call `strconv.Atoi` |
| `cannot use x (variable of type string) as int value in argument` | Wrong type at a call or assignment | Convert (`strconv.Atoi` returns two values), or construct the right type |
| `not enough arguments in call to ...` | Missing a required parameter | Read the function signature; count the arguments |
| `missing return` | Some path through a function reaches the end without returning | Add a final `return` or handle the error branch explicitly |
| `cannot call pointer method on x` / `cannot take address of x` | Pointer receiver called on a non-addressable value (function result, map element, literal) | Assign to a local variable first, then call the method |
| `invalid operation: x.Field (variable of type T) has no field or method Field` | Typo, wrong type, or trying to access an unexported field from another package | Check the type with `%T`; read `go doc <package>.<Type>` |
| `syntax error: unexpected newline, expected }` or `expected ';'` | Unclosed brace/parenthesis, or missing comma in a multiline literal | Inspect the lines just above the reported position |
| `declared and not used: err` after a short declaration | `err` was overwritten instead of checked | Check `if err != nil` right after the call |

Example of the pointer case: `makeNote().MarkDone()` fails if `MarkDone` has a pointer receiver
and `makeNote()` returns a value; write `n := makeNote(); n.MarkDone()` instead.

---

## 5. Common runtime errors and panics

A panic means the program reached an operation Go considers unsafe. Read the top frame: it names
the exact line and operation.

| Message | Likely cause | First check |
|---|---|---|
| `invalid memory address or nil pointer dereference` | Pointer is `nil`, or a map lookup returned a nil pointer | Check the error before using results; print the pointer; initialize with `&T{}` |
| `index out of range [n] with length m` | Slice/array indexed past its end or negative | Print `len`; check `len(args)` before `os.Args[1]` |
| `assignment to entry in nil map` | Map declared but not initialized | `m := make(map[K]V)` before `m[k] = v` |
| `unexpected end of JSON input` | Empty or truncated file/string; half-written file | Inspect the raw bytes; use atomic replacement; see `10-professional-habits.md` |
| `json: cannot unmarshal ... into Go value of type ...` | JSON shape/type does not match the struct | Print the JSON; check field types, tags, and exported fields |
| `no such file or directory` | Wrong path or wrong current directory | `pwd`, `os.Getwd()`, `filepath.Join`; use a scratch dir for smoke tests |
| `permission denied` | Read-only file, directory, or insufficient file mode | Inspect with `ls -l`; check the mode used when creating `tasks.json` |

Panics from expected user errors (bad JSON, bad arguments) are bugs in *your* handling: return a
clear error instead. Runtime panics are not caught by `recover` in this CLI; do not add `recover`
as a shortcut.

---

## 6. JSON pitfalls

| Pitfall | Symptom | Reminder |
|---|---|---|
| Unexported struct fields | Fields missing from output | `encoding/json` only touches exported (uppercase) fields |
| Wrong tag spelling | JSON keys do not match `Requirements.txt` | `json:"id"`, `json:"createdAt"`, etc.; a typo is silent |
| Case differences | Surprising decode results | Matching prefers exact case but accepts case-insensitive fallback |
| `time.Time` as a string | Parse error or zero time | JSON encodes time as an RFC3339 string; use `time.Time`, not a custom layout |
| Numbers decoded into `interface{}` | Values become `float64` | Decode into your own typed struct instead |
| Trailing comma or comments | `invalid character ...` | JSON has no comments and no trailing commas |
| Empty file | `unexpected end of JSON input` | Decide whether empty means "no tasks" or "corrupt", then handle it |
| `null` input | Nil pointer/slice after decode | Decide whether to reject `null` for a list |
| `omitempty` expectations | Zero values still appear where you did not expect | `omitempty` omits zero values, not zero-valued structs by default |
| Writing straight to the file | Corrupt file after interruption | Write to a temp file in the same directory, then rename |
| Unknown fields | Silently ignored | Useful for forward compatibility; validate values you care about |

Useful functions: `json.Marshal`, `json.MarshalIndent`, `json.Unmarshal`,
`json.NewEncoder`, `json.NewDecoder`. Inspect them with `go doc encoding/json`.

---

## 7. Time layout reminder

Go layouts are **examples**, not format codes. The reference time is:

```text
Mon Jan 2 15:04:05 MST 2006
```

| Need | Use |
|---|---|
| Timestamp in JSON | `time.RFC3339` = `2006-01-02T15:04:05Z07:00` |
| Current time | `time.Now()` (or an injected `Clock` for tests) |
| Convert to text | `t.Format(time.RFC3339)` |
| Parse text | `time.Parse(time.RFC3339, s)` |
| Zero time | `time.Time{}` — prints as `0001-01-01T00:00:00Z` |
| UTC | `t.UTC()` |

Common traps: writing `01-02-2006` (that is a US-style date, not RFC3339); comparing a
`time.Time` from JSON with one from `time.Now()` without stripping the monotonic clock reading
(`t.Round(0)` is one way); assuming local time equals UTC; parsing before checking the error.
`go doc time.Layout` shows the full legend.

---

## 8. `os.Args` and `strconv` reminders

- `os.Args[0]` is the program name; real arguments start at `os.Args[1]`.
- `len(os.Args)` counts the program name too. Check it **before** indexing.
- Arguments are always `string`. IDs must be converted: `id, err := strconv.Atoi(raw)`.
- `Atoi` returns an error for empty text, letters, and overflow. Check it before using `id`.
- For explicit bit size, use `strconv.ParseInt(raw, 10, 64)`.
- To convert an ID back to text, use `strconv.Itoa(id)`.
- This project uses positional arguments; the optional `flag` package is not required.

---

## 9. Glossary (terms used in this path)

| Term | Meaning |
|---|---|
| Argument | A value passed to a function or supplied on the command line |
| Parameter | The name a function uses for an incoming argument |
| Binary | An executable produced by `go build` |
| Module | A directory tree with `go.mod` defining a module path and Go version |
| Package | A directory of Go files sharing one `package` name |
| Import path | The string used in an `import`, e.g. `encoding/json` |
| Exported | Identifier starting uppercase; visible from other packages |
| Unexported | Identifier starting lowercase; package-private |
| Zero value | The default value: `0`, `""`, `false`, `nil`, or an empty collection |
| Composite literal | Syntax constructing a struct/slice/map value, e.g. `Note{}` |
| Struct | A named collection of fields |
| Field | A named member of a struct |
| Struct tag | Metadata on a field, e.g. `json:"id"`, used by `encoding/json` |
| Method | A function with a receiver |
| Receiver | The value before the method name in `func (n Note) String()` |
| Pointer | A value holding the address of another value |
| Dereference | Accessing the value a pointer points to with `*` |
| Slice | A flexible-length view over an array; has length and capacity |
| Array | A fixed-length sequence |
| Map | An unordered collection of key/value pairs |
| Nil | The zero value of pointers, slices, maps, interfaces, channels, and funcs |
| Interface | A set of method signatures; satisfied implicitly by types |
| Sentinel error | A package-level error value compared with `errors.Is` |
| Error wrapping | Adding context with `fmt.Errorf("...: %w", err)` while preserving the cause |
| Goroutine | A lightweight concurrent function; not needed for this CLI |
| Race condition | Two goroutines touching shared state concurrently with at least one write |
| Race detector | The `-race` instrumentation that finds such conflicts |
| Table-driven test | One test looping over a slice of named input/want cases |
| Subtest | A named child test created with `t.Run` |
| Test fixture | Setup data or environment a test needs |
| Temp directory | A throwaway directory from `t.TempDir()` |
| Golden file | Stored expected output in `testdata/`, compared byte-for-byte |
| Stdlib | The standard library shipped with Go |
| gofmt | The built-in formatter; canonical Go style |
| go vet | The built-in static analyzer |
| Dependency injection | Passing collaborators in instead of creating them deep inside |
| Fake / stub | A small test-only implementation of an interface |
| CWD | Current working directory; where relative paths and `tasks.json` resolve |
| Atomicity | An operation that appears to happen all at once, never half-done |
| Idempotency | Repeating an operation leaves the same final state |
| Deterministic | Same inputs and state always produce the same output |
| JSON marshal | Converting a Go value to JSON bytes |
| JSON unmarshal | Converting JSON bytes into a Go value |
| RFC3339 | The `time` layout used for timestamps in this project |
| Monotonic clock reading | Hidden time comparison data that JSON does not preserve |
| Exit code | Integer a program returns to the shell; zero means success by convention |
| Stdout / Stderr | Standard output (results) and standard error (diagnostics) |

---

## 10. Where to look offline

Run these before guessing an API. `go doc` reads the installed documentation; the source is in
`$(go env GOROOT)/src`.

```bash
go help test          go help testflag       go help build
go help modules       go help go.mod         go env GOROOT

go doc testing        go doc testing.T       go doc testing.T.Run
go doc testing.T.TempDir                     go doc testing.T.Chdir
go doc testing.T.Setenv                      go doc testing.T.Cleanup

go doc os             go doc os.Args         go doc os.Getwd
go doc os.ReadFile    go doc os.WriteFile    go doc os.CreateTemp
go doc os.Rename      go doc os.Exit

go doc errors         go doc errors.Is       go doc errors.New
go doc fmt            go doc fmt.Errorf      go doc fmt.Fprintln

go doc encoding/json  go doc json.Marshal    go doc json.Unmarshal

go doc time           go doc time.Time       go doc time.Layout
go doc time.RFC3339   go doc time.Parse      go doc time.Format
go doc time.Time.Round

go doc strconv        go doc strconv.Atoi    go doc strconv.Itoa
go doc strings        go doc path/filepath   go doc maps
```

If `go doc <name>` says "no Go files", you probably used a symbol name where a package path is
required, or the symbol is in a different package. Check the package list with `go list std`.

---

## 11. Debugging checklist

Work top to bottom; do not skip to changing code:

1. **Copy the exact error message** into your notes. The wording is a search key in `go doc`.
2. **Find the first failure.** Fix the earliest compiler error or the first failed test; later
   messages are often consequences.
3. **Locate the precise line** from `file.go:line:column`. Open that file and read five lines
   above and below it.
4. **Reproduce minimally** in `/tmp/go-scratch`: the smallest input that still fails.
5. **State your expectation** before running: "I expect ID 2, I got 0." A wrong guess is data.
6. **Check a boundary first:** argument count, converted ID, error from the file read, decoded
   JSON shape, current directory.
7. **Print values to `stderr`** with `fmt.Fprintln(os.Stderr, "step:", value)`; remove them when
   the cause is found.
8. **Check types** with `%T` when a value is not what you assumed.
9. **Run `go vet ./...`** and `go test -count=1 ./...` to rule out stale results and suspicious
   constructs.
10. **Run `go test -race ./...`** only if goroutines or shared state exist; they do not in this
    CLI.
11. **Read the offline docs** for the exact function: `go doc os.Rename`, `go doc time.Parse`,
    and so on.
12. **Fix the cause, not the symptom.** Do not change the expected value in a test to make it
    pass unless the requirement really changed.
13. **Add a test that would have caught the bug**, then confirm it fails on the old behavior.
14. **Write three lines in your build journal**: symptom, root cause, fix.

---

## Common mistakes

- Indexing `os.Args[1]` before checking `len(os.Args)`.
- Using `%v` to wrap an error instead of `%w`, then wondering why `errors.Is` fails.
- Guessing a time layout instead of copying `time.RFC3339`.
- Treating an empty JSON file and a corrupt JSON file as the same case without deciding.
- Reading a `time.Time` from JSON and comparing it to `time.Now()` without `Round(0)`.
- Assuming a map's iteration order is stable when printing `list`.
- Editing code before reading the first error message carefully.

---

## Checkpoint / self-test

Answer these aloud without looking back:

1. Which command runs every test and disables the cache?
2. Which verb preserves an error for `errors.Is`, and where may it be used?
3. What are the two values returned by `strconv.Atoi`, and why must both be handled?
4. Why does `os.Args[1]` need a length check?
5. What does `cannot call pointer method on` usually mean, and what is the quick fix?
6. What distinguishes `unexpected end of JSON input` from `json: cannot unmarshal ...`?
7. Write the RFC3339 layout from memory and say why `01-02-2006` is wrong.
8. Explain zero value, receiver, sentinel error, and idempotency in one sentence each.
9. Give three `go doc` commands you can use without a network connection.
10. What are the first three steps of the debugging checklist?

If any answer is fuzzy, re-read that section once, then retype the relevant snippet into
`/tmp/go-scratch` and run it. Repetition is how this becomes permanent knowledge.

## Next

You have finished the CLI reference track — return to `08-feature-by-feature.md` and build the
CLI if you have not already. When that works, continue with `12-http-api-with-chi.md` for the
optional API track.
