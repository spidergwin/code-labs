# 08 — Time Without Chrono

> **Scope.** Standard-library-only UTC timestamps in the fixed shape `YYYY-MM-DDTHH:MM:SSZ`, plus
> the reverse conversion. This is a general utility chapter: it teaches clock access and calendar
> arithmetic, not task commands. The commands that use it come in `11-feature-by-feature.md`; the
> JSON representation lives in `06-json-without-crates.md`.
>
> **Boundary.** Rust blocks are **study sketch — retype and complete yourself** material. The two
> calendar functions are labelled generic utility references: they are complete arithmetic sketches
> you must verify with tests. This is not a Task Tracker implementation.

## Why this matters for Task Tracker

Every task carries `createdAt` and `updatedAt`. Those strings must be stable, sortable, and written
without an external date library. Storing a fixed-width UTC timestamp gives you three benefits at
once: lexicographic string ordering matches chronological ordering, the value is unambiguous across
machines, and the file is readable by a human.

The trap is that `std::time` gives you an *instant*, not a *date*. You can ask "how many seconds
since 1970?" but not "what year is it?" This chapter closes that gap with a small, tested utility
and shows how to inject the clock so tests never depend on the real time.

---

## What you will learn

- `SystemTime`, `UNIX_EPOCH`, `duration_since`, `SystemTimeError`, `as_secs`, and `subsec_millis`.
- Why whole-second UTC is the right precision for this project, and how wall clocks differ from
  monotonic clocks.
- What `std` cannot do for you, and the exact limits of our own formatter.
- The civil-calendar algorithm: eras, day-of-year, day-of-era, and the 719468-day shift.
- Both directions: days-from-civil and civil-from-days, as generic utility sketches.
- How epoch seconds become `YYYY-MM-DDTHH:MM:SSZ`, and how a validated string becomes epoch seconds.
- How to inject a clock and capture it once per operation.
- Which tests prove the arithmetic, including leap years and known dates.

---

## 1. What `std::time` actually gives you

**Unix epoch** means 1970-01-01T00:00:00Z, the zero point used by many systems. `UNIX_EPOCH` is a
`SystemTime` constant representing that instant. **UTC** (Coordinated Universal Time) is the
time scale at zero longitude; it has no daylight-saving shifts.

| API | Meaning |
|---|---|
| `SystemTime::now()` | current wall-clock instant |
| `UNIX_EPOCH` | the instant 1970-01-01T00:00:00Z |
| `now.duration_since(UNIX_EPOCH)` | `Result<Duration, SystemTimeError>` |
| `Duration::as_secs()` | whole seconds as `u64` |
| `Duration::subsec_millis()` | remaining milliseconds as `u32` |
| `SystemTimeError` | failure when the earlier instant is *after* the later one |

```rust
// Study sketch — retype and complete yourself.
use std::time::{SystemTime, UNIX_EPOCH};

fn now_secs() -> Result<i64, TimeError> {
    let duration = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|_| TimeError::BeforeEpoch)?;
    Ok(duration.as_secs() as i64)
}
```

`TimeError` is your own error enum (see `04-errors-and-validation.md`); one variant covers a clock
before the epoch. `duration_since` fails if the system clock is set before 1970 — unusual today, but possible on a
badly configured machine or in a test. `unwrap()` there would panic on a user's clock; return an
error instead. `as_secs()` truncates: 12:00:00.999 becomes 12:00:00. If you need milliseconds,
read `subsec_millis()` before converting; for this project we deliberately drop it.

### Wall clock versus monotonic clock

`SystemTime` is a **wall clock**: it can jump forward or backward because of NTP adjustments or a
user changing the date. `std::time::Instant` is **monotonic**: it never moves backward, so it is the
right tool for measuring elapsed durations (timeouts, benchmarks). Timestamps stored in a file must
be wall-clock values — a user expects `updatedAt` to reflect the date on their clock — so we use
`SystemTime` despite its occasional jumps.

### Why whole-second UTC

- The output format requested by roadmap.sh-shaped CLIs has no fractional part or offset.
- Second precision is plenty for edit history; it keeps strings short and comparisons simple.
- UTC removes timezone ambiguity. Local time would require named zones, DST rules, and a zone
  database — none of which exist in `std`.
- Fixed-width strings sort correctly: `2026-11-24T09:15:00Z` < `2026-11-25T00:00:00Z`.

> **Professional habit — store an instant in one canonical form.** Choose UTC whole seconds at the
> boundary. Convert to local time only when displaying, never when writing.

---

## 2. The limits of our utility state clearly

`std` has no calendar type, no date formatter, and no timezone database. We implement one narrow
conversion and write its contract down:

| Limit | Consequence |
|---|---|
| UTC only | no offsets (`+02:00`) and no named zones |
| Seconds precision | sub-second part of `SystemTime` is discarded |
| No leap seconds | an instant is counted as 86,400 seconds per day; `23:59:60` is rejected |
| Fixed four-digit year | years outside `0000`…`9999` cannot be formatted at fixed width — reject them |
| `i64` seconds | far beyond any realistic task timestamp; overflow must still be checked in arithmetic |
| Gregorian calendar proleptically | the leap rule is applied to all years, including ones before 1582 |

The last point is fine: our inputs are current dates. Define the supported range and test its edges
rather than silently producing a ten-digit year that breaks string ordering.

---

## 3. The civil-calendar algorithm

A **civil date** is a year, month, and day in the Gregorian calendar. The arithmetic below counts
whole days from 1970-01-01, called the **epoch day**. It works by reorganising the calendar into
400-year cycles.

### 3.1 The ideas in words

1. **Era.** Every 400 years contain exactly 146097 days (including 97 leap days). Number these
   cycles *eras*. Dividing a day count by 146097 yields an era number plus a remainder.
2. **Year-of-era.** Inside an era, years are numbered 0…399. The leap-year pattern repeats every
   400 years, so once you know the era and the year-of-era, the leap days are determined.
3. **Month shifting.** January and February are treated as months 13 and 14 of the *previous*
   year. This moves the irregular leap day to the end of the year, so a simple formula
   `(153 * month_index + 2) / 5` produces the day-of-year.
4. **Day-of-year (doy).** The day number within that March-based year, starting at 0.
5. **Day-of-era (doe).** `yoe * 365 + yoe/4 - yoe/100 + doy`: 365 days per year, plus one leap day
   every 4 years, minus one for each century, plus the day within the year.
6. **The shift.** The algorithm's internal zero is 0000-03-01, which is **719468 days before**
   1970-01-01. Add or subtract that constant to convert to and from Unix epoch days.
7. **Negative values.** Rust integer division truncates toward zero, but the formulas need floor
   division for negative years/days. Adjust the dividend first: `if y >= 0 { y } else { y - 399 } / 400`.

### 3.2 Days from civil date

**Generic utility reference — study sketch — retype and complete yourself.** Verify it against the
known dates in §6.

```rust
fn days_from_civil(y: i64, m: u32, d: u32) -> i64 {
    let m = m as i64;
    let d = d as i64;
    let y = y - if m <= 2 { 1 } else { 0 };          // Jan/Feb belong to previous year
    let era = (if y >= 0 { y } else { y - 399 }) / 400; // floor division
    let yoe = y - era * 400;                          // [0, 399]
    let mp = m + if m > 2 { -3 } else { 9 };          // Mar=0 ... Feb=11
    let doy = (153 * mp + 2) / 5 + d - 1;             // [0, 365]
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;  // [0, 146096]
    era * 146097 + doe - 719468
}
```

For 1970-01-01: y becomes 1969, era 4, yoe 369, mp 10, doy 306, doe 135080, and
`4 * 146097 + 135080 - 719468 = 0`. Work one example by hand before trusting the code.

### 3.3 Civil date from days

**Generic utility reference — study sketch — retype and complete yourself.** This is the inverse:
add the shift, split out the era, undo the leap-day corrections, then recover month and day.

```rust
fn civil_from_days(z: i64) -> (i64, u32, u32) {
    let z = z + 719468;
    let era = (if z >= 0 { z } else { z - 146096 }) / 146097;
    let doe = z - era * 146097;                                  // [0, 146096]
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // [0, 399]
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);            // [0, 365]
    let mp = (5 * doy + 2) / 153;                                 // [0, 11]
    let d = doy - (153 * mp + 2) / 5 + 1;                         // [1, 31]
    let m = mp + if mp < 10 { 3 } else { -9 };                    // [1, 12]
    let y = y + if m <= 2 { 1 } else { 0 };                       // undo the shift
    (y, m as u32, d as u32)
}
```

The `1460`, `36524`, and `146096` terms undo the `+1/4`, `-1/100`, `+1/400` leap corrections. The
`(5 * doy + 2) / 153` formula inverts the variable month lengths by treating months as blocks of
153 days. Adding 1 back to `d` converts from zero-based to human day numbers.

---

## 4. Composing an epoch second into a timestamp

Split the second count into whole days and seconds within the day, then unpack both.

1. Convert seconds to `i64`. If your clock gave `u64`, check the range before casting.
2. `let days = secs.div_euclid(86_400);` — `div_euclid` floors, which matters for negative values.
3. `let sod = secs.rem_euclid(86_400);` — always `0..=86399`, even for negative epochs.
4. `let (y, m, d) = civil_from_days(days);`
5. `let h = sod / 3600; let mi = (sod % 3600) / 60; let s = sod % 60;`
6. Format with fixed width:

```rust
// Study sketch — retype and complete yourself.
let text = format!("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", y, m, d, h, mi, s);
```

`{:04}` pads the year with leading zeros to four digits; `{:02}` does the same for each two-digit
field. `T` separates date and time and `Z` states UTC. No newline is included: the caller decides
whether to print one.

```text
epoch secs 0           -> 1970-01-01T00:00:00Z
epoch secs 1795478400  -> 2026-11-24T00:00:00Z
```

### Negative epochs

A negative second count is before 1970. With `div_euclid`/`rem_euclid`, the calendar conversion is
correct: `-1` becomes day `-1` plus 86399 seconds of that day, i.e. `1969-12-31T23:59:59Z`.
Decide deliberately: either support it (using Euclidean division everywhere) or reject it as out
of range with a clear error. Never mix truncating `/` with Euclidean division — that produces an
off-by-one day for negative values.

> **Professional habit — make the format a single function.** One `fn format_utc(secs: i64) ->
> Result<String, TimeError>` is the only place that knows the layout. Tests and callers share it,
> so a stray space or a newline cannot creep into half the code.

---

## 5. Parsing a timestamp string back

Reading stored timestamps is needed if you want to compare by epoch seconds or validate a file.
Parse **the exact shape**, not a permissive subset.

### 5.1 Fixed-shape validation

`YYYY-MM-DDTHH:MM:SSZ` is always 20 bytes. Check `text.len() == 20` first; because the expected
characters are ASCII, byte offsets equal character offsets here.

| Byte indices | Meaning |
|---|---|
| `0..4`, `5..7`, `8..10` | year, month, day digits |
| `4`, `7` | `-` |
| `10` | `T` |
| `11..13`, `14..16`, `17..19` | hour, minute, second digits |
| `13`, `16` | `:` |
| `19` | `Z` |

```rust
// Study sketch — retype and complete yourself.
fn digits(text: &str, range: std::ops::Range<usize>) -> Result<u32, TimeError> {
    let bytes = text.as_bytes();
    let slice = bytes.get(range.clone()).ok_or(TimeError::BadShape)?;
    if !slice.iter().all(u8::is_ascii_digit) { return Err(TimeError::BadShape); }
    std::str::from_utf8(slice)
        .map_err(|_| TimeError::BadShape)?
        .parse::<u32>()
        .map_err(|_| TimeError::BadShape)
}
```

`str::parse` is the standard way to turn text into a number; it returns `Result`, so no manual digit
arithmetic is needed once shape is checked.

### 5.2 Range and calendar checks

1. `month` in `1..=12`, `hour` in `0..=23`, `minute` and `second` in `0..=59`.
2. **Leap-year rule:** a year is leap if divisible by 4 **and** (not divisible by 100 **or**
   divisible by 400). So 2000 is leap, 1900 is not, 2024 is leap.
3. Days per month: `[31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]`, with February 29 in leap
   years. Check `day` against this table — `2025-02-29` must be rejected.
4. Convert back with `days_from_civil(y, m, d) * 86_400 + h * 3600 + mi * 60 + s`, using `i64`
   arithmetic.
5. If your chosen contract rejects pre-epoch timestamps, check the result is `>= 0` and return an
   error otherwise. If it supports them, keep Euclidean semantics consistently.

```rust
// Study sketch — retype and complete yourself.
fn parse_utc(text: &str) -> Result<i64, TimeError> {
    if text.len() != 20 { return Err(TimeError::BadShape); }
    let b = text.as_bytes();
    if b[4] != b'-' || b[7] != b'-' || b[10] != b'T'
        || b[13] != b':' || b[16] != b':' || b[19] != b'Z' {
        return Err(TimeError::BadShape);
    }
    let year = digits(text, 0..4)? as i64;
    let month = digits(text, 5..7)?;
    // let day = ...; let hour = ...; let minute = ...; let second = ...;
    // validate ranges and leap year, then:
    // Ok(days_from_civil(year, month, day) * 86_400
    //    + hour as i64 * 3600 + minute as i64 * 60 + second as i64)
    todo!("finish the validation and conversion")
}
```

Do not accept lowercase `z`, a space instead of `T`, or an offset. A strict writer and a strict
reader together guarantee that files your program writes are the only files it needs to read.

---

## 6. Injectable clock

A service that calls `SystemTime::now()` directly cannot be tested deterministically: every run
produces a different `updatedAt`. Pass the time source in instead.

```rust
// Study sketch — retype and complete yourself.
trait Clock {
    fn now_secs(&self) -> Result<i64, TimeError>;
}

struct SystemClock;
impl Clock for SystemClock {
    fn now_secs(&self) -> Result<i64, TimeError> { now_secs() }
}

struct FixedClock(i64);
impl Clock for FixedClock {
    fn now_secs(&self) -> Result<i64, TimeError> { Ok(self.0) }
}
```

The parameter `&dyn Clock` is a **trait object**: a borrowed value of any type implementing `Clock`.
An even smaller alternative is `fn() -> i64`, but a trait gives you a place to attach names and
state later. See `03-structs-enums-and-match.md` for traits and `09-modules-and-architecture.md`
for where the clock belongs in your module layout.

**Capture once per operation.** At the start of `update`, call `clock.now_secs()` one time and pass
that value to every helper in that operation. If `createdAt` and `updatedAt` are read separately,
a second-boundary crossing can give two different values for what is logically one edit. Fixed
clocks also make tests instant — no sleeps, no flaky assertions.

---

## 7. Tests to write

Use the known-day table as your oracle; it is independent of your implementation.

| Input | Expected |
|---|---|
| `days_from_civil(1970, 1, 1)` | `0` |
| `days_from_civil(2000, 2, 29)` | `11016` |
| `days_from_civil(2026, 11, 24)` | `20781` |
| `civil_from_days(0)` | `(1970, 1, 1)` |
| `civil_from_days(11016)` | `(2000, 2, 29)` |
| `civil_from_days(-1)` | `(1969, 12, 31)` |

| Group | Cases |
|---|---|
| round trip | format then parse for all table dates; parse then format for canonical strings |
| known values | epoch `0`, `1795478400`, a leap day, a day before epoch |
| invalid dates | `2025-02-29`, `1900-02-29`, `2026-13-01`, `2026-00-10`, `2026-04-31` |
| invalid times | hour 24, minute 60, second 60 (no leap seconds) |
| shape | 19 or 21 bytes, lowercase `z`, space instead of `T`, missing dash, trailing newline |
| width | year 9999 stays four digits; year 10000 is rejected rather than printed as five |
| fixed clock | two updates with `FixedClock` produce exactly the same timestamp |
| error path | a clock before `UNIX_EPOCH` returns `Err`, not a panic |

```rust
// Study sketch — retype and complete yourself.
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn civil_days_round_trip() {
        for days in [-1_i64, 0, 11016, 19782, 20781] {
            let (y, m, d) = civil_from_days(days);
            assert_eq!(days_from_civil(y, m, d), days);
        }
    }

    #[test]
    fn known_format() {
        assert_eq!(format_utc(0).unwrap(), "1970-01-01T00:00:00Z");
        assert_eq!(format_utc(1795478400).unwrap(), "2026-11-24T00:00:00Z");
    }

    #[test]
    fn leap_year_rules() {
        assert_eq!(parse_utc("2000-02-29T00:00:00Z").unwrap(), 951782400);
        assert!(parse_utc("1900-02-29T00:00:00Z").is_err());
    }
}
```

Also test the composition around midnight and month boundaries; arithmetic that is right for one
date is often wrong at a rollover. Test strategy in general is in `10-testing-and-debugging.md`.

---

## 8. Common mistakes

| Mistake | Symptom | Fix |
|---|---|---|
| Local time drifting into storage | timestamps differ between machines | Use UTC only; never `Local`-style conversion |
| `duration_since(UNIX_EPOCH).unwrap()` | panic on a pre-epoch clock | Match on `Result`, return an error |
| Off-by-one day | date is one day early/late, often only before 1970 | Use `div_euclid`/`rem_euclid` consistently |
| Wrong leap rule | 1900 accepted, 2000 rejected | `y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)` |
| Comparing formatted strings for ordering | wrong order across widths/zones | Compare epoch seconds; format only for display/output |
| `as u64` without a range check | negative or overflowed seconds wrap | Convert with checked casts and explicit errors |
| Reading the clock twice in one operation | `createdAt` and `updatedAt` disagree | Capture once, pass the value down |
| Trusting `parse` without shape checks | `2026-1-1` accepted or panics | Validate length and separators first, then digits |
| Truncating seconds | `.999` silently changes the second | Document whole-second precision and stick to it |

> **Professional habit — separate instants from display.** Internally compare `i64` epoch seconds.
> Only convert to a string at the file boundary and at the CLI output boundary.

---

## 9. Apply it to Task Tracker

1. What exact error message should appear for a stored timestamp like `2026-02-29T00:00:00Z`?
2. Should your CLI accept timestamps before 1970? Write the decision into a test either way.
3. Where will `SystemClock` and `FixedClock` live, and which functions receive `&dyn Clock`?
4. How do you guarantee `createdAt` and `updatedAt` come from the same captured second?
5. Which value will you sort and filter on: the formatted string or epoch seconds? Why?
6. How will `06-json-without-crates.md` represent these strings — as a JSON string or a number?

---

## 10. Checkpoint

- [ ] I can obtain whole UTC seconds from `SystemTime` without panicking.
- [ ] I can explain why a monotonic clock is not used for stored timestamps.
- [ ] I can state all five limits of the utility (UTC, seconds, no leap seconds, year width, calendar).
- [ ] I can explain era, year-of-era, day-of-year, day-of-era, and the 719468 shift in words.
- [ ] I can convert both ways with the two generic calendar functions and verify known dates.
- [ ] I can format `1795478400` as `2026-11-24T00:00:00Z` and parse it back.
- [ ] I can reject invalid shapes, ranges, and non-leap February 29 without panicking.
- [ ] I can inject a fixed clock and explain why time is captured once per operation.

If a box fails, retype the calendar sketches into a scratch crate and assert the known-day table.

## Next

Open `09-modules-and-architecture.md` to place the JSON codec, clock, file store, and command layer
into modules with clear boundaries, then continue with `11-feature-by-feature.md`.
