# 06 — Time and timestamps

> **Jargon first.** An *instant* is a specific point on the timeline. A *presentation* is how
> that instant is written for a human or machine. A *time zone* is a rule for turning an
> instant into local clock fields (year, month, hour, ...). An *offset* is a fixed difference
> from UTC at one instant; it is not a zone, because zones change offset during the year.

## Why this matters for Task Tracker

`Requirements.txt` requires `createdAt` and `updatedAt` on every task. These are timestamps:
machine-readable records of *when something happened*. Get them wrong and the CLI still
"works" until you sort tasks, compare entries across machines, or explain why a task created
at 09:00 shows up as 08:00 on a colleague's laptop.

This chapter gives you all the `time` knowledge this project needs — and nothing you do not.
Everything is standard library; your offline reference is `go doc time` and the source in
`$(go env GOROOT)/src/time`.

## What you will learn

- Wall clock versus monotonic clock, and what survives serialization.
- `time.Now()`, UTC versus local time, and why storage should use UTC.
- The zero value of `time.Time` and `IsZero`.
- Why `time.Time` is comparable but `==` is usually wrong; use `Equal`, `Before`, `After`.
- Layout strings and Go's reference time `Mon Jan 2 15:04:05 MST 2006`.
- `time.RFC3339` and `time.RFC3339Nano`.
- `time.Parse`, `time.ParseInLocation`, and `*time.ParseError`.
- `time.Location`, `time.UTC`, `time.Local`, and `time.LoadLocation`.
- Unix seconds/nanoseconds — when appropriate, when dangerous.
- `time.Duration`, `Add`, `Sub`, and elapsed time.
- How `encoding/json` marshals `time.Time`.
- Value semantics and defensive copying.
- Deterministic tests via an injected `now` function.

---

## 1. Wall clock and monotonic clock

A computer has two notions of "now", and `time.Time` normally holds both.

| Kind | What it is | Good for | Problem |
|---|---|---|---|
| **Wall clock** | OS clock date-and-time reading | Display, storing `createdAt`, agreement across machines | Can jump backwards/forwards (NTP sync, manual change, DST) |
| **Monotonic clock** | Counter that only increases | Measuring elapsed time, ordering events on one machine | Means nothing as a calendar date; cannot be serialized |

```go
start := time.Now()
work()
elapsed := time.Since(start) // uses the monotonic reading when available
```

If the wall clock is adjusted while `work()` runs, `elapsed` stays correct. Formatting or
JSON-marshalling drops the monotonic part because a file cannot store it meaningfully.

**Key idea:** wall clock answers *what time is it?* Monotonic clock answers *how long has it
been?* Task Tracker needs the wall clock; keep the monotonic idea for durations and tests.
Read the monotonic-clock paragraph in `go doc time.Time` — it is genuinely useful.

## 2. `time.Now`, UTC, and local time

```go
now := time.Now()        // wall clock + monotonic, in time.Local
fmt.Println(now)         // prints with the local offset, e.g. +0200
fmt.Println(now.UTC())   // same instant, written in UTC
fmt.Println(now.Local()) // same instant, in the machine's local zone
```

`UTC()` and `Local()` do not change the instant: they change the *location* — the lens used
to display clock fields. `time.Local` is decided by the machine's environment (`TZ`, system
settings), so the same program on two laptops can print different strings for one instant.
Hence the rule:

> **Store UTC. Display UTC, or explicitly document that you display local time.**

For this project, store UTC strings and print those same strings. Consistency beats
prettiness.

## 3. The zero value

```go
var t time.Time
fmt.Println(t.IsZero()) // true
fmt.Println(t)          // 0001-01-01 00:00:00 +0000 UTC
```

The zero time is January 1, year 1, 00:00:00 UTC. It is not "now". If it appears in a stored
task, a timestamp was probably never set. `IsZero()` asks that question readably.

Why it matters: JSON missing a `createdAt` key decodes to the zero time, not an error.
Detecting it lets you report a corrupt record instead of showing year 1 to a user.

## 4. Comparable, but prefer `Equal`

`time.Time` can be compared with `==`, but that is usually a trap:

1. The same instant in different locations compares unequal (location pointers differ).
2. A value with a monotonic reading compares unequal to one without it, though
   `t.Equal(t.Round(0))` is true.
3. Comparing formatted strings compares presentations: `09:00+02:00` and `07:00Z` are
   different strings for the same moment.

| Method | Question |
|---|---|
| `a.Equal(b)` | Same instant? (ignores location and monotonic differences) |
| `a.Before(b)` / `a.After(b)` | Strictly earlier / later? |
| `a.IsZero()` | Is this the zero time? |

```go
a := time.Date(2026, 3, 14, 9, 0, 0, 0, time.FixedZone("CET", 2*3600))
b := a.UTC()
fmt.Println(a == b)     // false: different locations
fmt.Println(a.Equal(b)) // true: same instant
```

Use `==` only when you deliberately want full-representation equality, which is rare.

## 5. Formatting and layout strings

Go does not use `YYYY-MM-DD` codes. It uses a literal **reference time**:

```text
Mon Jan 2 15:04:05 MST 2006
```

That sentence *is* the layout. A compact mnemonic: `01/02 03:04:05PM '06 -0700`.
Recognize those numbers and you can rebuild any token.

| Meaning | Token | From the reference |
|---|---|---|
| 4-digit / 2-digit year | `2006` / `06` | `2006` |
| Month numeric / short / full | `01` / `Jan` / `January` | `01`, `Jan` |
| Day padded / space-padded | `02` / `_2` | `2` |
| Weekday short / full | `Mon` / `Monday` | `Mon` |
| Hour 24h / 12h, AM/PM | `15` / `03`, `PM` | `15`, `3PM` |
| Minute / second | `04` / `05` | `04`, `05` |
| Numeric offset | `-07:00` or `-0700` | `-0700` |
| `Z` for UTC, offset otherwise | `Z07:00` | `Z` |
| Zone abbreviation | `MST` | `MST` |

```go
t := time.Date(2026, 3, 14, 9, 26, 53, 0, time.UTC)
fmt.Println(t.Format("2006-01-02"))        // 2026-03-14
fmt.Println(t.Format("02 Jan 2006 15:04")) // 14 Mar 2026 09:26
fmt.Println(t.Format(time.RFC3339))        // 2026-03-14T09:26:53Z
```

`Format` returns a string; it never changes `t`. Writing `Format("YYYY-MM-DD")` yields the
literal text `YYYY-MM-DD`, because those letters are not tokens.

### RFC3339 and RFC3339Nano

| Constant | Shape | Use |
|---|---|---|
| `time.RFC3339` | `2006-01-02T15:04:05Z07:00` | Whole seconds, machine-readable — ideal here |
| `time.RFC3339Nano` | same with fractional seconds, trailing zeros removed | When sub-second precision must survive a round trip |

Pick one, use it everywhere, and name it in the README.

## 6. Parsing and `*time.ParseError`

```go
raw := "2026-03-14T09:26:53Z"
when, err := time.Parse(time.RFC3339, raw)
if err != nil {
	var pe *time.ParseError
	if errors.As(err, &pe) {
		fmt.Println("bad value:", pe.ValueElem, "wanted:", pe.LayoutElem)
	}
	return
}
fmt.Println(when.UTC().Format(time.RFC3339))
```

`ParseError` carries `Layout`, `Value`, `LayoutElem`, `ValueElem`, and `Message` — much more
helpful than a bare "parsing time ..." error. `errors.As` is explained in
`03-errors-and-validation.md`.

Two surprises:

- `time.Parse` assumes **UTC** when the layout has no zone element. For local wall time use
  `time.ParseInLocation(layout, value, loc)`.
- While parsing, Go accepts a fractional second even if the layout omits it. Convenience,
  not permission to be careless.

Writing and reading with the same layout and zone rule makes round trips stable. Verify that
belief in a scratch program.

## 7. Locations

| Expression | Meaning |
|---|---|
| `time.UTC` | UTC zone, offset always zero |
| `time.Local` | Machine's local zone, from the environment |
| `time.LoadLocation("Europe/Berlin")` | Named zone from the system zone database; returns an error |

```go
loc, err := time.LoadLocation("Europe/Berlin")
if err != nil {
	return // no database on this machine, or a misspelled name
}
t := time.Date(2026, 3, 14, 9, 0, 0, 0, loc)
```

The stdlib `time/tzdata` package can be blank-imported to embed zone data, but this project
does not need it: store UTC and named zones disappear from the problem. Named zones also
bring daylight-saving transitions where local times occur twice or not at all — complexity
a 7-command CLI should avoid.

## 8. Unix seconds and nanoseconds

```go
t := time.Now()
sec := t.Unix()            // int64 seconds since 1970-01-01 UTC
ns := t.UnixNano()         // int64 nanoseconds
back := time.Unix(sec, 0)  // returns a Time in time.Local
```

Compact and sortable, but: `UnixNano` overflows outside roughly 1678–2262 (undefined on the
zero time), `time.Unix` returns local time (call `.UTC()` if needed), and a bare integer does
not declare its unit. Use RFC3339 strings in `tasks.json`; if you must store an integer, put
the unit in the field name (`createdAtUnixSeconds`).

## 9. `Duration`, `Add`, `Sub`

A `time.Duration` is an `int64` nanosecond count with constants like `30 * time.Second`
and `24 * time.Hour`.

| Operation | Meaning |
|---|---|
| `t.Add(d)` | New instant `d` after `t`; `d` may be negative |
| `t.Sub(u)` | Duration `t - u`, saturating instead of overflowing |
| `time.Since(t)` / `time.Until(t)` | `time.Now().Sub(t)` / `t.Sub(time.Now())` |

```go
type Measurement struct {
	TakenAt time.Time
	Expires time.Time
}
func (m Measurement) Remaining() time.Duration { return m.Expires.Sub(time.Now()) }
```

`Add`, `UTC`, and `Round` return new values. `Sub`, `Before`, `After`, and `Equal` use
monotonic readings when both operands have them; after a serialize/parse round trip they
fall back to the wall clock.

## 10. `time.Time` and `encoding/json`

`time.Time` implements `json.Marshaler`: encoding writes a **quoted RFC3339Nano string**;
decoding expects a quoted RFC3339 string.

```go
type Note struct {
	Title     string    `json:"title"`
	CreatedAt time.Time `json:"createdAt"`
}
```

A time of `2026-03-14T09:26:53Z` encodes as `"2026-03-14T09:26:53Z"`. A missing key decodes
to the zero time. A non-string or badly formatted string makes `json.Unmarshal` fail.
So `time.Time` fields with correct tags give you the required JSON representation for free
(tags: `02-collections-and-data-modeling.md`; files: `04-json-and-filesystem.md`). Set both
fields explicitly to one `now.UTC()` value on add; never rely on the zero value.

## 11. Mutability and defensive copying

`time.Time` is a small immutable value: its fields are unexported and methods return new
values. Copying it by assignment or argument is cheap and safe.

"Defensive copying" means not letting outside code hold a reference to a value you depend
on. Prefer `time.Time` values over `*time.Time`; a pointer lets someone replace the value.
If your own struct exposes a pointer to a timestamp, return a copy of the pointed-to value.
If you hand out a slice of times, pass a copy if mutation would matter. The mindset matters
more than the mechanics — value semantics make corruption by accident unlikely.

## 12. Deterministic testing: inject a `now` function

Code that calls `time.Now()` directly cannot be tested with an exact timestamp. Pass in the
function that supplies time instead. Generic illustration (not Task Tracker code):

```go
type NoteStore struct {
	now func() time.Time
}
func NewNoteStore(now func() time.Time) *NoteStore {
	if now == nil {
		now = time.Now // production default
	}
	return &NoteStore{now: now}
}
```

A test supplies a fixed function returning `2026-03-14T09:00:00Z`, so every timestamp is
predictable. The same idea applies to storage: pass in a store so tests need no real file
(`07-architecture-and-project-layout.md`, `09-testing-and-debugging.md`).

For the CLI, calling `time.Now()` once per command and passing that value down is a sensible
middle ground: both timestamps in one operation agree without a framework.

## 13. Common mistakes

| Mistake | Symptom | Fix |
|---|---|---|
| `Format("YYYY-MM-DD")` | Literal `YYYY` in output | Use `2006-01-02` |
| Parsing local time with `time.Parse` | Timestamps shift by your offset | `ParseInLocation` or include `Z07:00` |
| Comparing formatted strings | Ordering breaks across offsets | Use `Before`/`After`/`Equal` |
| Using `==` on times | Same instant reported unequal | Use `Equal` |
| Storing local time without a zone | Ambiguous on another machine | Store UTC |
| Forgetting `updatedAt` on add | Year 1 appears in JSON | Set both fields from one `now` |
| Calling `time.Now()` twice | Timestamps differ by milliseconds | Capture `now` once |
| `UnixNano` on zero time | Overflow or nonsense | Check `IsZero()`; avoid Unix nanos |

### Professional habit

Whenever you touch a timestamp, write down three things: what instant you mean, what zone
you store it in, and what format you use on the wire. Most time bugs are disagreement about
one of those three.

### Professional habit

Have one helper that answers "what is the current UTC time for this operation?", called once
per command. Scattered `time.Now()` calls make tests flaky and timestamps inconsistent.

## 14. Apply it to Task Tracker

Guidance only — write the code yourself.

- Confirm the exact field names in `Requirements.txt`: `id`, `description`, `status`,
  `createdAt`, `updatedAt`. Decide and record: "I store UTC RFC3339 strings with whole
  seconds." Put it in the README.
- In `internal/task/model.go`, which field type produces RFC3339 JSON automatically?
- On `add`, should `createdAt` and `updatedAt` be exactly equal? What argument supports it?
- On `update` and status changes, which field changes and which must stay untouched? What
  happens if a description is "updated" to identical text?
- Parse one timestamp from `tasks.json` in a scratch program with
  `time.Parse(time.RFC3339, ...)` and reformat it. Does the instant survive?
- How would you inject a fixed clock so a test can assert an exact `createdAt`?
- What should happen if a stored record has a missing or invalid `createdAt`? Justify it.

## 15. Offline reference

```text
go doc time
go doc time.Time
go doc time.Now
go doc time.Time.Format
go doc time.Parse
go doc time.ParseError
go doc time.LoadLocation
go doc time.Duration
go doc time.Unix
go doc encoding/json.Marshaler
```

Retype both snippets into `/tmp/go-scratch`, run them, and change one layout. Predicting the
output before running it is the fastest way to learn layouts.

## Checkpoint

1. What two clocks can a `time.Now()` value hold, and which survives JSON encoding?
2. Why is `a.Equal(b)` better than `a == b`?
3. What is Go's reference layout, and why does `15` appear in it?
4. How do `RFC3339` and `RFC3339Nano` differ?
5. What zone does `time.Parse` assume when the layout has none?
6. What JSON does a `time.Time` field produce, and what appears if the key is missing?
7. Why inject a clock function instead of calling `time.Now()` in service code?
8. State your storage rule — zone, format, precision — and where you will document it.

**Next:** `07-architecture-and-project-layout.md`
