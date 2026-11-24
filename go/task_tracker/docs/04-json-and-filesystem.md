# 04 — JSON and Filesystem

> **Jargon first.** **JSON** (JavaScript Object Notation) is a text format for structured
> data: objects, arrays, strings, numbers, booleans, and `null`. **Marshalling** means turning
> a Go value into JSON bytes; **unmarshalling** means turning JSON bytes back into a Go value.
> An **atomic** operation is one that appears to happen all at once — observers see either
> the old state or the new state, never a half-written state.

## Why this matters for Task Tracker

Task Tracker has one job beyond printing text: keep a list of tasks between runs. That list
lives in `tasks.json` in the current directory, created on first use. Get this layer wrong and
every feature above it is unreliable: permissions fail, IDs disappear, a crashed write leaves
half a file, or a field named `Description` silently becomes a JSON key called
`"Description"` instead of `"description"`.

This chapter gives you the whole standard-library toolkit: `encoding/json` for the format and
`os` plus `path/filepath` for the file. You will also learn the temp-file-and-rename pattern
that keeps data safe when a write goes wrong. Error-handling conventions come from
`03-errors-and-validation.md`; timestamps come from `06-time-and-timestamps.md`.

## What you will learn

- How Go types map to JSON types; struct tags for keys, `omitempty`, and hidden fields.
- Missing fields versus zero values, unknown keys, and `float64` inside `interface{}`.
- How `time.Time` encodes, and what deterministic output means.
- How to locate a file in the current working directory with `os.Getwd` and `path/filepath`.
- Reading and writing with `os.ReadFile`, `os.WriteFile`, `os.Stat`, `os.OpenFile`, and `0644`.
- Detecting a missing file with `errors.Is(err, os.ErrNotExist)`.
- The atomic write pattern, the single-process assumption, and corruption recovery.

## 1. JSON in one minute

A JSON file is a text document. Here is a small, generic settings document:

```json
{
  "theme": "dark",
  "retries": 3,
  "tags": ["work", "personal"],
  "lastSeen": "2026-11-24T09:15:00Z",
  "disabled": false,
  "notes": null
}
```

Rules worth memorizing:

- Keys and strings use **double quotes**; booleans are `true`/`false`; absence is `null`.
- Trailing commas are invalid; whitespace outside strings is insignificant.
- A document has one top-level value. For tasks, that is an array of objects, often `[]`.

Your records need `id`, `description`, `status`, `createdAt`, and `updatedAt`. You will decide
the concrete Go representation later; this chapter is about the machinery.

Offline reference: run `go doc encoding/json` before reading on.

## 2. `Marshal`, `Unmarshal`, and friends

| Function | Direction | Result / notes |
|---|---|---|
| `json.Marshal(v)` | Go → JSON | `[]byte`, compact (no extra whitespace) |
| `json.Unmarshal(data, &v)` | JSON → Go | Fills `v`; **pass a pointer** |
| `json.MarshalIndent(v, prefix, indent)` | Go → JSON | `[]byte`, human-readable indentation |
| `json.Indent(dst, src, prefix, indent)` | JSON bytes → pretty JSON bytes | For when you already have raw JSON |
| `json.Valid(data)` | Check only | Reports whether bytes are syntactically valid JSON |

```go
type Settings struct {
	Theme string
}

s := Settings{Theme: "dark"}
data, err := json.Marshal(s)
// data == []byte(`{"Theme":"dark"}`) — field name used as-is!
```

Two facts jump out of that output:

1. Only **exported** fields (capitalized) are visible to `encoding/json`. A lowercase field
   is silently skipped; this is a very common "why is my data empty?" bug.
2. Without tags, the JSON key is exactly the Go field name. Tags let you choose lowercase
   keys like `"description"`.

There is no `json.UnmarshalIndent` function. If you need pretty output from raw JSON bytes,
use `json.Indent` with a `bytes.Buffer`; when you already have a Go value, use
`json.MarshalIndent`.

> **Detail.** `json.MarshalIndent` does **not** append a trailing newline. If your editor
> complains, that is normal; append `'\n'` yourself only if you want it.

## 3. Struct tags: rename, omit, hide

A **struct tag** is a string literal attached to a field that gives libraries extra
instructions. `encoding/json` reads the `json` key from it.

```go
// Illustrative only: a generic settings struct, not Task Tracker code.
type Settings struct {
	Theme   string   `json:"theme"`
	Retries int      `json:"retries,omitempty"`
	Secret  string   `json:"-"`
	Tags    []string `json:"tags"`
}

s := Settings{Theme: "dark", Tags: []string{"work"}}
data, _ := json.MarshalIndent(s, "", "  ")
fmt.Println(string(data))
```

Output (whitespace added by `MarshalIndent` is about readability, not meaning):
`{"theme":"dark","tags":["work"]}`

| Tag | Effect on marshalling | Effect on unmarshalling |
|---|---|---|
| `json:"id"` | Key becomes `"id"` | Reads key `"id"` into that field |
| `json:"retries,omitempty"` | Skips the field when it holds a zero value | Still reads `"retries"` if present |
| `json:"-"` | Never written | Never read |
| no tag | Uses the Go field name verbatim | Matches the Go field name (case-insensitively) |

`omitempty` treats these as empty: `0`, `false`, `""`, `nil`, and zero-length collections.
`omitempty` on an `id` field would hide a legitimate `0`; do not use it on required fields.

> **Professional habit.** Use tag names identical to the requirements (`id`, `description`,
> `status`, `createdAt`, `updatedAt`). Changing a key later breaks existing data files.

## 4. Zero values, missing fields, and unknown fields

Unmarshalling never guesses. If a key is absent, the corresponding Go field simply keeps its
**zero value** (`0`, `""`, `false`, `nil`).

```go
var s Settings
err := json.Unmarshal([]byte(`{"theme":"light"}`), &s) // err == nil
// s.Theme == "light"; s.Retries == 0; s.Tags == nil
```

| Input JSON | Result for `Tags []string` | Why |
|---|---|---|
| `"tags": ["a", "b"]` | two-element slice | Normal array mapping |
| `"tags": []` | non-nil, length 0 slice | Empty array is not `null` |
| `"tags": null` | `nil` slice | JSON `null` maps to nil |
| key absent | `nil` slice | Zero value remains |

Unknown keys are **ignored by default**:

```go
var s Settings
_ = json.Unmarshal([]byte(`{"theme":"light","surprise":42}`), &s)
// No error; "surprise" is discarded.
```

That is convenient for forward compatibility, but it hides typos; `DisallowUnknownFields()`
on a `json.Decoder` is an optional stricter mode, not required here.

A subtle consequence: you cannot tell "set to 0" from "missing". If that matters, use a
pointer field (`*int`); for Task Tracker, validate after unmarshalling instead.

## 5. Type mapping, numbers, and time

| JSON | Go target examples | Notes |
|---|---|---|
| string | `string`, `time.Time` (RFC3339 only) | `time.Time` needs a quoted timestamp |
| number | `int`, `int64`, `float64`, `uint` | Fails if it does not fit or is fractional for ints |
| `true` / `false` | `bool` | Only these spellings are valid |
| `null` | pointer, slice, map, interface | Sets them to `nil`; scalar values keep their zero value |
| array | slice or `[N]T` | Length must match for a fixed-size array |
| object | struct or `map[string]T` | Struct fields matched by tag/name; maps by key |

Numbers decoded into `interface{}` always become `float64`:

```go
var v any // alias for interface{} (Go 1.18+)
_ = json.Unmarshal([]byte(`{"id":1}`), &v)
m := v.(map[string]any)
fmt.Printf("%T %v\n", m["id"], m["id"]) // float64 1
```

One float type covers a wide range. Prefer structs with `int` fields when you know the shape;
type-assert carefully when you do not.

`time.Time` implements the JSON marshalling interfaces, so it encodes as an RFC3339 string
and decodes from one:

```go
t := time.Date(2026, 11, 24, 9, 15, 0, 0, time.UTC)
data, _ := json.Marshal(t)
fmt.Println(string(data)) // "2026-11-24T09:15:00Z"
```

The quotes are part of the string. A timestamp needs a timezone offset to be valid RFC3339
(`06-time-and-timestamps.md`); the zero `time.Time` marshals as `"0001-01-01T00:00:00Z"`.

### Slices: `nil` versus empty

```go
var missing []string // nil
empty := []string{}  // non-nil, length 0
a, _ := json.Marshal(missing) // null
b, _ := json.Marshal(empty)   // []
```

Choose one representation: `[]` is friendlier to humans and to code that ranges over it;
`null` works too but reads as "no list at all". Be consistent.

### Deterministic output

"Deterministic" means the same input produces the same bytes: struct fields follow declaration
order, map keys are sorted lexicographically, and slice order is preserved. Do not build JSON
by string concatenation, and do not refresh timestamps during a plain read-modify-write.

### Pretty vs compact storage

| Style | How | Pros | Cons |
|---|---|---|---|
| Compact | `json.Marshal` | Smaller file, fewer bytes written | Hard to read in an editor or diff |
| Indented | `json.MarshalIndent(v, "", "  ")` | Readable, easy to inspect by hand | Larger file |

For a learning project, **indented wins**. Compare *parsed values* in tests, not raw bytes.

## 6. Files and paths: where is `tasks.json`?

The requirement says "in the current directory". The **current working directory** (cwd) is
the directory your shell is in when you launch the program — not the directory containing
the binary and not the repository root necessarily.

```bash
cd /home/me/project-a && task-cli add "one"   # writes /home/me/project-a/tasks.json
cd /home/me/project-b && task-cli list        # reads  /home/me/project-b/tasks.json
```

| Tool | Purpose | Watch out for |
|---|---|---|
| `os.Getwd()` | Returns the absolute cwd path and an error | Can fail if the directory was removed |
| `filepath.Join(dir, name)` | Builds a path with the OS separator | Also cleans redundant parts |
| `filepath.Abs(path)` | Converts a relative path to absolute using cwd | Returns an error; check it |
| `filepath.Clean(path)` | Removes `.`, `..`, and duplicate separators | Purely string-level, no disk access |
| `filepath.Dir(path)` | Parent directory of a path | Useful for temp files beside the target |

```go
wd, err := os.Getwd()
if err != nil { return err }
path := filepath.Join(wd, "tasks.json") // absolute, portable
```

Relative paths also work. `filepath.Join` avoids hand-built strings that break on Windows;
do not hard-code a home directory or use `os.Executable()` — the spec ties the data file to
where the user runs the command.

## 7. Reading and creating the file

```go
// Illustrative only: generic settings loader, not Task Tracker code.
func loadSettings(path string) (Settings, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return Settings{}, nil // fresh start; caller may create defaults
		}
		return Settings{}, fmt.Errorf("read %q: %w", path, err)
	}
	var s Settings
	if err := json.Unmarshal(data, &s); err != nil {
		return Settings{}, fmt.Errorf("parse %q: %w", path, err)
	}
	return s, nil
}
```

| Function | Behavior | Notes |
|---|---|---|
| `os.ReadFile(path)` | Reads whole file into `[]byte` | Missing file → error wrapping `os.ErrNotExist` |
| `os.Stat(path)` | Returns `FileInfo` and error | Use to distinguish missing, directory, and regular file |
| `os.WriteFile(path, data, 0644)` | Creates if missing, **truncates** if present | Permission bits apply only when creating; subject to umask |
| `os.OpenFile(path, flags, perm)` | Fine-grained control | Requires one access mode plus optional modifiers |

`0644` is an octal literal: owner reads/writes, group and others read. The OS **umask** may
remove bits, so final permissions can be stricter.

Common `os.OpenFile` flags (combine with `|`):

| Flag | Meaning |
|---|---|
| `os.O_RDONLY`, `os.O_WRONLY`, `os.O_RDWR` | Exactly one access mode |
| `os.O_CREATE` | Create the file if absent |
| `os.O_TRUNC` | Empty the file when opening for writing |
| `os.O_APPEND` | Write at the end |
| `os.O_EXCL` | Fail if the file already exists (with `O_CREATE`) |

Detect absence with `errors.Is`: the error is a `*PathError` wrapping a lower-level error, and
your layers may wrap it again. The old `os.IsNotExist` helper does not understand wrapping;
see `03-errors-and-validation.md`.

> **Professional habit.** Decide first-run behavior explicitly: create an empty store on first
> launch, or treat a missing file as an empty list and write it on first mutation. Never
> silently treat a *corrupt* file as empty.

## 8. The atomic write pattern (teach the pattern, not a task store)

`os.WriteFile` truncates the target before writing new bytes. If the process crashes or the
disk fills midway, the file can be left empty or half-written — a **torn write**. The fix is
to never modify the target in place:

1. Marshal all new data first; if that fails, nothing on disk has changed.
2. Create a temp file **in the same directory** (so rename stays on one filesystem).
3. Write the bytes; set permissions (`CreateTemp` uses `0600`); `Sync`, then `Close`, checking
   errors.
4. `os.Rename(temp, target)`: readers see the old file or the new file, never a partial one.
5. On failure, a deferred `os.Remove` deletes the temp file.

```go
// Illustrative pattern only — saving a generic settings file, not Task Tracker code.
func saveSettings(path string, s Settings) error {
	data, err := json.MarshalIndent(s, "", "  ")
	if err != nil {
		return err
	}
	tmp, err := os.CreateTemp(filepath.Dir(path), ".settings-*.tmp")
	if err != nil {
		return fmt.Errorf("create temp file: %w", err)
	}
	tmpName := tmp.Name()
	defer os.Remove(tmpName) // deliberate cleanup; ignored if rename succeeded

	if _, err := tmp.Write(data); err != nil {
		tmp.Close()
		return fmt.Errorf("write temp file: %w", err)
	}
	if err := tmp.Chmod(0644); err != nil { // CreateTemp starts at 0600
		tmp.Close()
		return fmt.Errorf("set permissions: %w", err)
	}
	if err := tmp.Sync(); err != nil {
		tmp.Close()
		return fmt.Errorf("sync temp file: %w", err)
	}
	if err := tmp.Close(); err != nil {
		return fmt.Errorf("close temp file: %w", err)
	}
	if err := os.Rename(tmpName, path); err != nil {
		return fmt.Errorf("replace %q: %w", path, err)
	}
	return nil
}
```

Points to notice:

- A leading dot and distinctive suffix keep the temp file from looking like real data.
- `Sync` buys crash safety; `Close` alone does not promise data is on disk. We check `Close`
  explicitly because its error must be known before renaming.
- The deferred removal is harmless after a successful rename; check `go doc os.Rename` for
  platform caveats and test on your own OS.

You may start with plain `os.WriteFile`, then upgrade to this pattern as a deliberate second
step, as `08-feature-by-feature.md` suggests.

## 9. Concurrency and corruption mindset

**Single-process assumption.** This CLI assumes one Task Tracker process at a time. Atomic
rename prevents *torn* files, but two processes can still read the same old list and overwrite
each other ("last writer wins"). Locking (e.g. `syscall.Flock` on Unix) is out of scope and
non-portable; document the assumption.

**Corruption mindset.** One day `tasks.json` may contain invalid JSON. Your program should:

- [ ] Report the parse error with the file path and the JSON error detail.
- [ ] Exit non-zero; never print a success message.
- [ ] Never silently overwrite a file it could not parse.
- [ ] Optionally keep a backup copy (for example `tasks.json.bak`) before replacing suspicious
      data, so recovery is possible.
- [ ] Make this case testable by writing deliberately bad JSON into a temporary directory;
      see `09-testing-and-debugging.md` for `t.TempDir`.

## Common mistakes

- Forgetting to export struct fields, then wondering why the JSON file is `{}`.
- Using `Description` as a key when the spec asks for `description`.
- Passing a value instead of a pointer to `json.Unmarshal`.
- Expecting `json.UnmarshalIndent` to exist, or using `omitempty` on required fields.
- Assuming a decoded number in `interface{}` is an `int`; it is a `float64`.
- Forgetting that `time.Time` needs RFC3339 with a timezone, or that a nil slice is `null`.
- Building paths by string concatenation instead of `filepath.Join`.
- Checking `err == os.ErrNotExist` instead of `errors.Is(err, os.ErrNotExist)`.
- Assuming a crash during `os.WriteFile` is harmless, or renaming a temp file from `/tmp`
  across filesystems.
- Deferring `Close` and renaming before data is flushed; silently resetting corrupt data.

> **Professional habit.** Before writing a byte, ask: "If the power failed right now, would
> the file still be valid?" Marshalling first and replacing atomically turns that question
> into a design choice you can defend.

> **Professional habit.** Inspect the real file. After each storage change, open `tasks.json`
> in an editor and read it. Tests confirm behavior; your eyes confirm that the data shape
> matches what another human expects to see.

## Apply it to Task Tracker

No solution code here. Answer these in your notes, then implement one decision at a time.

1. **Choose the container shape.** Will the file hold a bare array of task objects, or an
   object with a `tasks` key? What does an empty store look like: `[]` or an object with an
   empty array? Write the exact sample text you expect to see on first run.
2. **Field names.** Which JSON keys must appear for every task? Which of them may never use
   `omitempty`? How will you make sure `createdAt` and `updatedAt` are strings rather than
   Unix integers?
3. **Missing vs corrupt.** How will your loader distinguish "no file yet" from "file exists
   but invalid JSON"? Which of those is allowed to continue, and which must stop with a
   non-zero exit?
4. **Write policy.** Will you use `os.WriteFile` first and upgrade to atomic rename later, or
   start with the safe pattern? Where does `filepath.Dir` come from, and why must the temp
   file live there?
5. **Cleanup.** If marshalling fails, has anything been written? If `Sync` fails, what removes
   the temp file? If `Rename` succeeds, is the deferred removal harmless?
6. **Human readability.** Do you want indented storage? What exact indentation string will you
   use, and will you append a trailing newline?
7. **Robustness test plan.** List three files you will manually create in a scratch directory
   to test: empty file, malformed JSON, wrong JSON type (for example an object where an array
   is expected). What should the CLI print for each?
8. **Ownership.** Who in your package layout owns path construction, who owns marshalling,
   and who owns printing? Hint: the CLI layer should not know about temp files; see
   `07-architecture-and-project-layout.md`.

Checklist before you call this layer done:

- [ ] A fresh directory produces a valid JSON file on the first mutation.
- [ ] A second run reads the same file back without changing data unexpectedly.
- [ ] Unknown IDs, bad JSON, and a directory named `tasks.json` produce clear errors.
- [ ] No operation can leave a truncated or half-written file.
- [ ] `go doc encoding/json` and `go doc os.Rename` entries make sense to you.

## Checkpoint

Answer without scrolling back:

1. Which struct fields does `encoding/json` see, and how do you rename a JSON key?
2. What does `omitempty` do, and why is it risky on an `id` field?
3. What is the value of a field whose JSON key is missing? What about an unknown key?
4. What Go type does the number `1` become when unmarshalled into `interface{}`?
5. What does a `nil` slice marshal to? What does an empty non-nil slice marshal to?
6. In what order are struct fields and map keys written?
7. Why does `tasks.json` belong in the current working directory rather than next to the
   binary?
8. Write the expression that correctly detects a missing file after two layers of wrapping.
9. List the five steps of the atomic write pattern in order.
10. What does `Sync` add that `Close` alone does not promise?
11. What does the single-process assumption mean, and what failure does it leave possible?
12. What should your CLI do when it finds corrupt JSON?

Next: `05-cli-arguments-and-ux.md`
