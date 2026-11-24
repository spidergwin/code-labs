# 01 — Go Basics

> **Why this matters for Task Tracker**
>
> Parsing `add "Buy groceries"`, finding a task by ID, and printing a filtered list are all made
> of variables, functions, conditions, loops, strings, and formatted output. This chapter is the
> grammar and vocabulary. Retype each snippet into `/tmp/go-scratch`, run it, change one value,
> and predict the new result before running again.

## What you will learn

- Program structure; packages, imports, exported versus unexported names, import cycles
- Variables (`var`, `:=`), zero values, inference, scope; constants and `iota`
- Basic types, conversions, and operators
- Functions: multiple returns, named returns, variadic parameters, `defer`, recursion
- `if`, `switch`, the three forms of `for`, `range`, `break`, `continue`, labels
- Strings, runes, bytes; printing with `fmt`
- Comments versus documentation comments

## 1. Program structure

A Go file has, in order: a `package` clause, an optional `import` declaration, declarations
(`var`, `const`, `type`, `func`), and statements inside functions.

```go
package main

import "fmt"

var greeting = "Hello"

func main() {
    fmt.Println(greeting)
}
```

A **declaration** introduces a package-level name; a **statement** is an instruction inside a
function (bare statements at package level are illegal). A **package** is a directory of `.go`
files compiled together; `go run file.go` compiles one file, `go run .` the whole package.

## 2. Packages and imports

Visibility is set by the first letter of an identifier:

| Name | Visibility | Example |
|---|---|---|
| Uppercase | **Exported** — usable from other packages | `Description`, `Save` |
| Lowercase | **Unexported** — only inside its own package | `description`, `save` |

This replaces `public`/`private` and applies to types, functions, fields, constants, and methods.

```go
import (
    "fmt"
    "os"
)
```

- Import **packages**, not files; use the declared package name as qualifier (`fmt.Println`).
- Every import must be used, or compilation fails. Group standard-library imports first,
  separated by a blank line from module imports.
- **Import cycles are illegal:** if A imports B and B imports A, neither can initialize. Move
  shared code into a third package or define a small interface at the point of use (see
  `02-collections-and-data-modeling.md`). The `internal/` directory further restricts imports
  to this module (`07-architecture-and-project-layout.md`).

> **Professional habit — start unexported.** Make names lowercase until another package needs
> them. Every exported name is a promise you must keep.

## 3. `package main` and `func main`

A `main` package builds an executable rather than a reusable library. Execution begins at:

```go
func main() {
    // program entry point
}
```

No parameters, no result, exactly one per program; there is no global script body. Keep your
CLI's `main` thin, delegating to packages whose behavior can be tested independently.

## 4. Variables

```go
var count int          // zero value 0
var name = "Notebook"  // type inferred: string
var price float64 = 4.5
count = 3
label := "draft"       // short declaration: declare AND initialize
total := count + 1
a, b := 1, 2
a, b = b, a            // swap
```

`:=` needs at least one new variable on the left and normally appears only inside functions.
Use `var` when there is no initial value, at package level, or to state the type explicitly.

### Zero values

| Type | Zero | Type | Zero |
|---|---|---|---|
| `int`, `int64`, `uint` | `0` | `float64` | `0.0` |
| `string` | `""` | `bool` | `false` |
| pointer, slice, map, interface | `nil` | struct | each field zeroed |

Zero values being useful is a core Go idea: an empty slice or blank string often needs no setup.

### Scope and shadowing

A variable's **scope** is the block where its name is visible. An inner declaration shadows
an outer one:

```go
func main() {
    shelf := "top"
    fmt.Println(shelf) // top
    {
        shelf := "bottom" // different variable
        fmt.Println(shelf) // bottom
    }
    fmt.Println(shelf) // top
}
```

Shadowing `err` with `:=` inside an error branch can make an error "disappear". Use `=` when you
mean to assign an existing variable.

## 5. Constants

A `const` is fixed at compile time and may be untyped until used:

```go
const MaxCopies = 5

const (
    Draft = iota // 0
    Review       // 1
    Published    // 2
)
```

An untyped constant adopts a type from context, so `Limit = 10` works with both `int` and
`float64`. `iota` generates successive integer values, one per line in a `const` block. Use
string constants when the exact word must survive in JSON (see `02-collections-and-data-modeling.md`).

## 6. Basic types and conversions

| Type | Meaning / note |
|---|---|
| `int` | Signed, architecture-sized; default for counts and IDs |
| `int64` | Signed 64-bit; predictable across machines, good for large IDs |
| `uint`, `uint64` | Unsigned; rarely needed here |
| `float64` | Default float; measurements, not money |
| `string` | Immutable bytes, usually UTF-8; comparable with `==`, `<` |
| `bool` | `true` / `false` |
| `byte` | Alias of `uint8`; a raw byte |
| `rune` | Alias of `int32`; one Unicode code point |

Conversions use `Type(value)`; there are no implicit numeric conversions:

```go
var wide int64 = int64(7)
var ratio float64 = float64(7) / 2 // 3.5
var truncated int = int(3.9)      // 3: toward zero
```

Two traps: `string(65)` is `"A"` (integer → rune), while `strconv.Itoa(65)` is `"65"`; and
integer division truncates. `go doc strconv` covers number/text conversion. Converting to a
smaller type can wrap silently — validate untrusted input first.

## 7. Operators

| Category | Operators |
|---|---|
| Arithmetic | `+` `-` `*` `/` `%` |
| Assignment | `=` `+=` `-=` `*=` `/=` `%=` etc. |
| Increment | `++` `--` (statements only) |
| Comparison | `==` `!=` `<` `<=` `>` `>=` |
| Logical / bitwise | `&&` `||` `!` / `&` `|` `^` `&^` `<<` `>>` |

`i++` cannot appear inside an expression. `7 / 2` is `3`, `7 % 2` is `1`. `&&` and `||`
short-circuit, which matters in nil and error checks. Strings compare lexicographically by
bytes; case folding needs `strings.ToLower`/`ToUpper`.

## 8. Functions

### Parameters and multiple returns

```go
func describe(book string, copies int) (string, error) {
    if copies < 0 {
        return "", fmt.Errorf("copies cannot be negative: %d", copies)
    }
    return fmt.Sprintf("%s (%d copies)", book, copies), nil
}

text, err := describe("Go Notes", 2)
if err != nil {
    fmt.Println("problem:", err)
    return
}
fmt.Println(text) // Go Notes (2 copies)
```

Returning a value plus an `error` last is idiomatic. Parameters of the same type may be grouped:
`func f(a, b int)`. `_` discards a result you deliberately ignore.

### Named returns and variadic parameters

```go
func split(total, count int) (whole, remainder int) {
    whole = total / count       // assumes count > 0
    remainder = total % count
    return                      // naked return: returns both named variables
}

func sum(values ...int) int {
    total := 0
    for _, v := range values {
        total += v
    }
    return total
}

sum(1, 2, 3)           // 6; inside sum, values is []int
sum([]int{1, 2, 3}...) // 6; spread a slice
```

Named results are documented and zero-initialized; prefer explicit returns in longer functions.

### `defer` and recursion

```go
func useNote(label string) {
    fmt.Println("open", label)
    defer fmt.Println("close", label)
    fmt.Println("write", label)
}
// open draft / write draft / close draft
```

Deferred calls run when the function returns, last in first out, with arguments evaluated at
the `defer` line. Use them for cleanup, not mid-function logic.

```go
func factorial(n int) int {
    if n <= 1 {
        return 1 // base case
    }
    return n * factorial(n-1)
}
```

Recursion needs a base case; Go does not handle unbounded depth well, so prefer loops for
simple iteration.

## 9. `if` and `switch`

```go
if copies == 0 {
    fmt.Println("out of stock")
} else if copies < 0 {
    fmt.Println("invalid count")
} else {
    fmt.Println("available")
}
```

An `if` can begin with a short statement scoped to its branches — the standard error pattern:

```go
var copies int
if n, err := fmt.Sscan("42", &copies); err != nil {
    fmt.Println("bad number:", err, "scanned:", n)
}
```

`fmt.Sscan` appears only to show the shape; argument parsing is covered in
`05-cli-arguments-and-ux.md`.

```go
switch status {
case "todo", "in-progress":
    fmt.Println("still open")
case "done":
    fmt.Println("closed")
default:
    fmt.Println("unknown status")
}
```

Cases do **not** fall through; use `fallthrough` explicitly or group values with commas. An
**expressionless switch** (`switch { case count == 0: ... }`) reads like an `if` chain. A **type
switch** (`switch v := value.(type) { case string: ... }`) branches on an interface's dynamic
type; interfaces arrive in `02-collections-and-data-modeling.md`.

## 10. Loops: the forms of `for`

```go
for i := 0; i < 5; i++ { fmt.Println(i) } // classic
for len(stack) > 0 { stack = stack[:len(stack)-1] } // condition only ("while")
for { if done { break } }                  // infinite, exited with break
```

### `range`

`range` iterates slices, arrays, maps, strings, channels, and — since Go 1.22 — integers:

```go
for index, title := range []string{"One", "Two"} {
    fmt.Println(index, title)
}
for i := range 3 { fmt.Println(i) } // 0, 1, 2
```

Use `_` to ignore a value. `break` leaves the innermost loop/switch; `continue` skips an
iteration; a **label** targets an outer loop:

```go
Search:
for _, row := range grid {
    for _, cell := range row {
        if cell == needle {
            break Search // leave both loops
        }
    }
}
```

Prefer a helper function with an early `return` over labels when clarity suffers.

## 11. Strings, runes, and bytes

A `string` is an immutable byte sequence, conventionally UTF-8. `len(s)` counts **bytes**;
`s[i]` yields a **byte**; `range` yields the starting byte index and a **rune** (Unicode code
point); `[]rune(s)` indexes by character.

```go
word := "café"
fmt.Println(len(word)) // 5 bytes: é takes two
for i, r := range word {
    fmt.Printf("%d:%c ", i, r)
}
fmt.Println()
// 0:c 1:a 2:f 3:é
```

ASCII-only input (commands, IDs, statuses) is safe to index by byte. For user-written text,
prefer `range` and `go doc strings` (`TrimSpace`, `Contains`, `HasPrefix`, `Split`, `Join`).

## 12. Printing and formatting

| Function | Behavior |
|---|---|
| `fmt.Println(a, b)` | Spaces between operands, then newline |
| `fmt.Print(a, b)` | No added spaces or newline |
| `fmt.Printf(format, args...)` | Explicit formatting; no automatic newline |
| `fmt.Sprintf(format, args...)` | Returns a formatted string |
| `fmt.Fprintln(w, ...)` | Writes to an `io.Writer` such as `os.Stderr` |

Useful verbs: `%s` string, `%d` decimal, `%t` bool, `%.2f` float, `%v` default, `%+v` struct
with field names, `%q` quoted string, `%T` type, `%x` hex.

```go
fmt.Printf("description=%q length=%d type=%T\n", d, len(d), d)
// description="Buy milk" length=8 type=string
```

Send normal results to stdout and errors to stderr so callers can redirect them. End
user-facing error output with a newline; `fmt.Errorf` messages conventionally do not include one.

## 13. Comments and documentation comments

```go
// A regular comment explains why a tricky line exists.
/* Block comments are mostly used to disable code temporarily. */

// Cookbook counts the recipes on a shelf.
// An empty shelf has zero recipes.
func Cookbook(shelf []string) int {
    return len(shelf) // len is O(1): a slice stores its length
}
```

A **documentation comment** sits immediately before an exported declaration and starts with
that name; `go doc` displays it. Explain *why*, not what the code already says. Delete
commented-out code before committing — Git remembers old versions.

## Common mistakes

| Mistake | Result | Fix |
|---|---|---|
| `:=` where `=` was meant | Shadowed `err` seems to vanish | Check every `:=` in error branches |
| Unused import | Compile error | Remove it or use it |
| Treating `int` as `int64` | Compile error | Convert explicitly |
| `string(65)` expecting `"65"` | Gets `"A"` | Use `strconv.Itoa` |
| `len(s)` as character count | Wrong for non-ASCII | `len([]rune(s))` or `range` |
| `i++` in an expression | Compile error | Standalone statement only |
| Expecting `switch` fall-through | Only first match runs | Comma-separated cases or `fallthrough` |

## Professional habit

> **Small functions, explicit errors, early returns.** Handle the failure case and return, then
> leave the happy path straight-line. Name booleans as conditions (`found`, `isValid`) and
> collections as plurals (`titles`). Predict outputs before running scratch programs; being
> wrong is how the mental model gets corrected.

## Apply it to Task Tracker

Questions only; no solution code.

1. List the variables your CLI needs from `add "Buy groceries"`. Which can be `const`? How would
   you detect a missing or whitespace-only description?
2. Using `Requirements.txt`, list the five task properties and their Go zero values. Which zero
   values are acceptable, and which should validation reject?
3. You need exactly `todo`, `in-progress`, `done` in JSON. Compare plain `string` constants, a
   custom named type, and `iota` integers. What does each option buy and cost?
4. Design the exact output for add success, delete success, and unknown ID. Which stream and
   which `fmt` verbs would you use for each?
5. Sketch a *flowchart* (boxes and arrows, not code) for `list [status]`: arguments → validate →
   load → filter → print → errors. Where do `switch` and `range` fit? Where should an early
   return stop work?
6. Name three small functions you expect to write (for example, one validating a raw status
   word). For each, describe inputs, results, and error case in prose only.
7. Write the rule you will follow for `err`: when `:=`, when `=`, and how you will spot shadowing
   during review.

## Checkpoint

1. Which is visible from another package, `Task` or `task`? Why?
2. Why does an unused import stop compilation?
3. What are the zero values of `string`, `bool`, and a pointer?
4. When must you use `var` instead of `:=`?
5. What does `iota` assign to the second constant in a block?
6. How do you convert `int` to `int64`, and a number to text?
7. What is `7 / 2` and why? Why is `i++` not an expression?
8. What two values does `range` over a slice provide? How do you ignore one?
9. Why does `len("café")` return 5, and how do you count characters?
10. Which verb shows a value's type, and what makes a comment a documentation comment?

---

Next: `02-collections-and-data-modeling.md`
