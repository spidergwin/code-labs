# 02 — Collections and Data Modeling

> **Why this matters for Task Tracker**
>
> Storing several tasks means holding many records, finding one by ID, changing it, and writing
> the result to JSON. Those needs map to slices, maps, structs, pointers, methods, interfaces,
> and named types. This chapter builds the mental model for each and shows how to *reason about*
> a data shape — without giving you the task model. You design that yourself.

## What you will learn

- Arrays and slices: `len`, `cap`, `append`, `make`, `copy`, slice expressions, aliasing
- `nil` versus empty slices; `range` pitfalls
- Maps: comma-ok lookup, `delete`, unspecified iteration order
- Structs, literals, embedding, JSON tags (concept preview)
- Pointers (`&`, `*`, `nil`), escape-to-heap intuition
- Methods with value versus pointer receivers; minimal interfaces
- Named types versus aliases; string constants as status enums
- `time.Time` preview; how to decide what shape your data should have

## 1. Arrays: fixed-size sequences

An **array** has a fixed length that is part of its type: `[3]string` and `[4]string` differ.

```go
var labels [3]string       // ["", "", ""]
prices := [3]int{4, 5}     // [4 5 0]
weeks := [...]int{1, 2, 3} // length inferred
fmt.Println(len(prices))   // 3
```

Arrays are **values**: assignment and function calls copy every element. Use them for known
fixed sizes (a 12-month table); use **slices** for changing lists of records.

## 2. Slices: the workhorse collection

A **slice** is a descriptor — pointer into an underlying array, length, and capacity. `len(s)` is
how many elements are visible; `cap(s)` is how many fit before the array must be replaced.

```go
s := make([]string, 2, 5) // length 2, capacity 5
fmt.Println(len(s), cap(s)) // 2 5
fmt.Printf("%q\n", s)       // ["", ""]
s[0] = "notebook"
s = append(s, "pen")        // 3 5
```

### Creating and growing

| Form | Result |
|---|---|
| `var s []int` | `nil` slice; length and capacity 0 |
| `s := []int{}` | non-nil empty slice |
| `s := []int{1, 2}` | literal with two elements |
| `make([]int, 3)` | length 3, capacity 3, zeros |
| `make([]int, 0, 10)` | length 0, capacity 10 (preallocation) |

`append` may replace the underlying array, so **always assign its result**:

```go
s = append(s, "folder")
s = append(s, "clip", "stapler")
s = append(s, other...) // spread another slice
```

`copy(dst, src)` copies up to the smaller length and returns the number copied:

```go
dst := make([]int, len(src))
n := copy(dst, src)
```

### Slice expressions and aliasing

```go
s := []int{0, 1, 2, 3, 4}
fmt.Println(s[1:3]) // [1 2]: start inclusive, end exclusive
fmt.Println(s[:2])  // [0 1]
fmt.Println(s[3:])  // [3 4]
part := s[1:3:3]    // full slice expression: len 2, cap 2 (limits later append)
```

Two slices can share storage, so mutating one can change the other:

```go
a := []int{1, 2, 3}
b := a[:2]
b[0] = 99
fmt.Println(a) // [99 2 3]
```

For independent data use `make` + `copy` (or `slices.Clone` in the standard-library `slices`
package). After `append` reallocates, `b` may still reference the old array: never rely on
aliasing by accident.

### `nil` versus empty

```go
var missing []int  // nil
empty := []int{}   // not nil
fmt.Println(missing == nil, empty == nil) // true false
```

Both have length 0 and range zero times, but `encoding/json` writes a `nil` slice as `null` and
an empty slice as `[]` — relevant when writing a store file (`04-json-and-filesystem.md`).

### `range` pitfalls

`range` copies each element into the loop variable, and its iteration count is fixed at start:

```go
type LibraryBook struct {
    Title  string
    Copies int
}

books := []LibraryBook{{Title: "A", Copies: 1}, {Title: "B", Copies: 2}}
for _, book := range books {
    book.Copies++ // modifies the copy
}
fmt.Println(books[0].Copies) // 1
for i := range books {
    books[i].Copies++ // correct: address the element
}
fmt.Println(books[0].Copies) // 2
```

Since Go 1.22 loop variables are per-iteration, so `&book` no longer points to one reused
variable — but it still points to the copy, not `books[i]`. Appending during a `range` will not
visit new elements; deleting by index while ranging can skip elements. Build a new slice or
iterate backwards instead.

## 3. Maps: key → value lookup

A **map** is an unordered collection of pairs. Keys must be **comparable** (strings, integers,
pointers, structs of comparable fields); slices and maps cannot be keys.

```go
prices := make(map[string]int)
prices["notebook"] = 4
fmt.Println(prices["notebook"]) // 4
cost, ok := prices["missing"]   // comma-ok lookup
fmt.Println(cost, ok)           // 0 false
delete(prices, "notebook")
fmt.Println(len(prices))        // 0
```

A missing key returns the value type's zero value; comma-ok tells you whether the key existed
(crucial when zero is legitimate). Literals work: `map[string]int{"pen": 2}`. `make(map[string]int,
100)` preallocates. A `nil` map can be read but **writing to it panics**. **Iteration order is
unspecified and varies between runs**: sort keys when order matters. Map values are not
addressable — `m[k].Field = x` fails; read into a variable, change it, assign it back.

Use a slice to preserve order and a map for key lookup; a program may need both.

## 4. Structs: defining a record

A **struct** is a named group of typed fields — Go's main data-modeling tool.

```go
type LibraryBook struct {
    ID     int64
    Title  string
    Copies int
}

book := LibraryBook{ID: 7, Title: "Notes", Copies: 3} // keyed literal: preferred
blank := LibraryBook{}                               // all zero values
fmt.Printf("%+v\n", book) // {ID:7 Title:Notes Copies:3}
fmt.Println(book.Title)   // Notes
```

Keyed literals survive field reordering; positional ones are fragile. Structs are values, so
assignment copies them. `==` works only if every field is comparable; compare slices and maps
field by field. Exported fields (uppercase) are visible to other packages.

### Embedding (briefly)

A field written as a type with no name is **embedded**; its fields and methods are promoted:

```go
type Label struct { Text string }
type Note struct {
    Label  // embedded
    Pinned bool
}
n := Note{Label: Label{Text: "Remember"}}
fmt.Println(n.Text) // Remember
```

Embedding shares behavior but hides origins; ordinary named fields are usually clearer.

### JSON tags (concept preview)

A **struct tag** is metadata read by convention; `encoding/json` looks for the `json` key:

```go
type Measurement struct {
    ID    int64   `json:"id"`
    Value float64 `json:"value"`
    Note  string  `json:"note,omitempty"`
}
```

Only exported fields are encoded. The tag sets the JSON key (`ID` → `"id"`); `omitempty` skips
zero values. Without a tag, the field name is used verbatim. `04-json-and-filesystem.md` covers
marshaling fully.

## 5. Pointers

A **pointer** holds an address; `*T` means "pointer to T".

```go
count := 3
p := &count      // & takes the address
*p = 9           // * dereferences: value at that address
fmt.Println(count) // 9
var missing *int // nil: points to nothing
```

Dereferencing `nil` panics, so check pointers that may be absent. For structs, `p.Field` is
shorthand for `(*p).Field`.

```go
func bump(n int)    { n = n + 1 }      // caller unchanged
func bumpAt(n *int) { *n = *n + 1 }    // caller changed
```

Pass a pointer when the function must modify the caller's value, when the value is large, or
when absence needs a representation (`nil`); otherwise pass by value. Go's compiler runs
**escape analysis**: if a local's address must outlive its function, it is allocated on the
**heap** and reclaimed by the garbage collector. You do not choose this with syntax; returning
`&note` is safe by design.

## 6. Methods

A **method** is a function with a **receiver**, a value of the type it is attached to.

```go
type LibraryBook struct {
    Title  string
    Copies int
}

func (b LibraryBook) Summary() string {
    return fmt.Sprintf("%s: %d", b.Title, b.Copies)
}

func (b *LibraryBook) Loan() {
    if b.Copies > 0 {
        b.Copies--
    }
}

book := LibraryBook{Title: "Notes", Copies: 2}
fmt.Println(book.Summary()) // Notes: 2
book.Loan()                 // Go takes the address automatically
fmt.Println(book.Copies)    // 1
```

| Receiver | Gets | Use when |
|---|---|---|
| `func (b LibraryBook)` | a copy | The method only reads; the struct is small |
| `func (b *LibraryBook)` | a pointer | The method mutates, the struct is large, or `nil` is meaningful |

Be consistent: don't mix receiver kinds on one type without a reason. Receiver types determine
a type's **method set**, which decides which interfaces it satisfies. A `nil` pointer receiver
can be called if the method does not dereference it.

## 7. Interfaces: only what you need

An **interface** is a named set of method signatures; a type satisfies it implicitly by having
those methods. No `implements` keyword exists.

```go
type error interface { Error() string }              // built into the language
type Reader interface { Read(p []byte) (n int, err error) } // from io, simplified
```

- `error` is how functions report failure; anything with `Error() string` qualifies (see
  `03-errors-and-validation.md`).
- `io.Reader` means "anything bytes can be read from": files, buffers, connections. Because
  `json.NewDecoder` accepts an `io.Reader`, the same code works with files or memory.
- `fmt.Stringer` (`String() string`) customizes how `fmt` prints a value.

Declare small interfaces **where they are used**, capturing only what the caller needs:

```go
type NoteSource interface {
    Notes() ([]string, error)
}

func countNotes(src NoteSource) (int, error) {
    notes, err := src.Notes()
    if err != nil {
        return 0, err
    }
    return len(notes), nil
}
```

Any type with that method works now or later. Interfaces describe **behavior**, not stored data;
keep them to one or two methods and put data shape in structs.

## 8. Named types, aliases, and status enums

```go
type Stage string      // NEW type with underlying type string
type Label = string    // ALIAS: just another name for string
```

A named type has its own methods and is distinct in assignment, though conversion is easy:
`Stage("draft")` or `string(StageDraft)`. Aliases create no new type.

```go
const (
    StageDraft     Stage = "draft"
    StageReview    Stage = "review"
    StagePublished Stage = "published"
)

func validStage(s Stage) bool {
    switch s {
    case StageDraft, StageReview, StagePublished:
        return true
    default:
        return false
    }
}
```

One place holds the allowed words; the type documents intent; the strings remain readable in
JSON. `iota` is better for small integers; strings are better when the exact file spelling
matters. Validate at the boundary where untrusted input enters.

## 9. `time.Time` preview

`time.Time` represents an instant plus location information.

```go
now := time.Now()
stamp := now.Format(time.RFC3339)              // e.g. 2026-11-24T10:15:30Z
parsed, err := time.Parse(time.RFC3339, stamp) // back to time.Time
```

The zero value `time.Time{}` is year 1, not "now". RFC3339 is the standard JSON/log format;
`encoding/json` handles `time.Time` automatically. `time.Now()` may carry a monotonic reading
that formatting drops, so prefer `time.Now().Equal(parsed)` over `==` for instants. See
`06-time-and-timestamps.md`.

## 10. How to reason about "what shape should my task data have?"

Start with questions, not a struct:

1. **What must persist?** `Requirements.txt` names five properties. Which are identity, content,
   status, and timestamps?
2. **What type fits each value?** Whole number, free text, one of a fixed set of words, or an
   instant? What would an invalid value look like?
3. **How is a record found?** Which field is the key? Who assigns it? What happens after a
   delete and then an add?
4. **What changes, and when?** Which fields are editable? Which must change on *every*
   modification, including status changes? Which layer sets it?
5. **What is the JSON contract?** Choose key names with tags. Decide what missing, `null`, or
   wrong-typed fields mean.
6. **Where does validation live?** At boundaries: before bad data enters the model and before
   trusting data read from disk.
7. **How do many records fit?** A slice preserves print order; a map gives key lookup. If you
   use both, which is the source of truth?
8. **Who may change it?** Which packages need each field? Exported fields are simple but
   unrestricted; unexported fields plus methods enforce invariants.

Here is a **generic** illustration of the kind of shape these answers produce — a measurement,
not a task:

```go
type Measurement struct {
    ID        int64     `json:"id"`
    Label     string    `json:"label"`
    Value     float64   `json:"value"`
    CreatedAt time.Time `json:"createdAt"`
    UpdatedAt time.Time `json:"updatedAt"`
}
```

Notice: fixed-width integer identity, plain text label, numeric value, real timestamps, explicit
JSON names. **This is not the Task definition.** Answer the questions yourself, then write your
model into `internal/task/model.go` when `08-feature-by-feature.md` asks for it.

## Common mistakes

| Mistake | Result | Fix |
|---|---|---|
| Forgetting `s = append(s, x)` | Element disappears | Assign the result |
| Mutating `b := a[:2]` | `a` changes too | `make` + `copy` for independence |
| `for _, v := range s { v.Field = x }` | Slice unchanged | `for i := range s { s[i].Field = x }` |
| Appending or deleting during `range` | Skipped/stale elements | New slice or backwards index loop |
| Assuming map order | Flaky tests and output | Sort keys |
| Writing to a `nil` map | Panic | `make` first |
| `m[k].Field = x` | Compile error | Read, modify, assign back |
| Dereferencing `nil` | Panic | Check; prefer useful zero values |
| Value receiver that mutates | Changes a copy | Pointer receiver |
| Timestamps as hand-built strings | Formatting/comparison bugs | `time.Time` + RFC3339 |

## Professional habit

> **Model the invariant, not just the fields.** For each field ask: what values are legal, who
> sets it, and when does it change? Write the answer before adding the field. Use keyed literals,
> keep interfaces small and local, and never rely on map order. When unsure about a collection
> operation, run a ten-line scratch program and print the result.

## Apply it to Task Tracker

Questions only; no solution code.

1. Copy the five required properties into a table: name, candidate type, zero value, who sets
   it, when it changes. Fill types using `go doc` evidence.
2. Decide how to represent `todo`, `in-progress`, `done`. What exact strings must JSON contain?
   Give one argument for and against plain strings, named string types, and integers.
3. `list` needs insertion order; `update`/`delete`/status changes need lookup by ID. Which
   collection would you use, and would you add an index? What happens to the index after delete?
4. Describe a test that would catch aliasing bugs when filtering tasks.
5. Why can't a map keyed by ID be printed directly for `list`? What would you add for stable output?
6. Which task operations should be methods? For each, choose a receiver kind and justify it.
7. Where might `nil` signal absence in your design, and where would it be a bug? How do you turn
   a missing task into a friendly error instead of a panic?
8. Mark each command in `Requirements.txt` yes/no for "must update `updatedAt`".
9. Choose JSON key spellings and decide whether `omitempty` fits any field. What should the file
   hold before the first add: `null`, `[]`, or something else?
10. Assign responsibilities to `internal/task`, `internal/storage/jsonstore`, and
    `internal/transport/cli` in prose — no code.

## Checkpoint

1. What is the difference between `len(s)` and `cap(s)`?
2. Why must you assign `append`'s return value?
3. What does a full slice expression `s[1:3:3]` control?
4. What does comma-ok lookup tell you that a plain map read does not?
5. Why is map iteration order unspecified, and what do you do when order matters?
6. Why doesn't `for _, book := range books { book.Copies++ }` update `books`?
7. What is the zero value of a pointer, and what happens when you dereference it?
8. When should a method use a pointer receiver? How does a type satisfy an interface?
9. What is the difference between `type Stage string` and `type Stage = string`?
10. Why use `time.Time` instead of a string for timestamps, and what did you decide for task data?

Next: `03-errors-and-validation.md`
