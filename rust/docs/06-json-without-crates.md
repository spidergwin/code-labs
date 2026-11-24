# 06 — JSON Without Crates

> **Scope.** A small, general-purpose JSON reader and writer built with the standard library only.
> This chapter is about the *codec*, not about tasks; the domain mapping and file workflow come in
> `11-feature-by-feature.md` and `07-filesystem-and-cli.md`.
>
> **Boundary.** Every Rust block is a **study sketch — retype and complete yourself**. Sketches show
> shapes, signatures, and algorithms but intentionally omit branches and error paths. There is no
> complete parser and no Task Tracker implementation here.

## Why this matters for Task Tracker

The CLI stores state in a JSON file in the current directory. With **no external crates allowed in
the CLI track**, *you* own the boundary between bytes and values:

```text
tasks.json (bytes)  --parse-->  Json value  --TryFrom-->  domain model
domain model        --From-->   Json value  --write-->    tasks.json (bytes)
```

Every feature sits behind this boundary: `add` parses, appends, writes; `update` mutates differently.
A codec that accepts corrupt input or emits invalid JSON causes missing tasks, wrong statuses, or a
file that no longer loads. Design for **output that is always valid JSON** and **input that fails
honestly** — a located `Result::Err`, never a panic or half-built value. Error design is covered in
`04-errors-and-validation.md`; collections in `05-collections-and-strings.md`.

## What you will learn

- The JSON grammar, including the rules beginners break: no trailing commas, no comments, one value.
- Why `std` has no JSON, and how to document a supported subset rather than imitate a library.
- An owning `Json` enum; `Vec<(String, Json)>` versus maps, and `Number(String)` versus `f64`.
- A recursive serializer with correct escaping and optional pretty-printing.
- A cursor-based recursive-descent parser: byte peeking, whitespace skipping, first-byte dispatch.
- A `JsonError` with byte offset and reason; `Result<Json, JsonError>` throughout.
- Mapping `Json` to typed values with `TryFrom`, and back with `From`.
- A test plan for round trips, escapes, nesting, whitespace, and malformed input.

## 1. The grammar, in one page

A **value** is one of seven possibilities; a **grammar rule** describes which character sequences
are legal.

| Value kind | Example | Notes |
|---|---|---|
| object | `{"id": 1, "done": false}` | name/value pairs; names are strings |
| array | `[1, 2, 3]` | ordered sequence of values |
| string | `"hello\nworld"` | double quotes only; backslash escapes |
| number | `-12.5e+3` | no leading `+`, no leading zeros, no `NaN`/`Infinity` |
| `true` / `false` | `true` / `false` | exact lowercase literals |
| `null` | `null` | exact lowercase literal |

Whitespace is exactly space, tab, line feed, or carriage return. It may appear around values and
punctuation, but **not inside a number or literal token**.

- **No trailing commas.** `{"a": 1,}` and `[1, 2,]` are invalid.
- **No comments.** Neither `//` nor `/* */` exists in JSON.
- **One top-level value.** `{} {}` is invalid; extra bytes after the first value are an error.
- **Control characters must be escaped.** A literal newline inside a string is invalid: use `\n`.
- **Keys are strings.** `{id: 1}` is invalid.

```json
{
  "id": 1,
  "description": "Buy \"oat milk\"",
  "tags": ["errand", "food"],
  "done": false,
  "updatedAt": null
}
```

The specification permits duplicate keys but says their interpretation is unpredictable; your
codec must make a deliberate choice (§2).

### Why there is no JSON in `std`

The standard library ships the *mechanisms* (`String`, `Vec`, slices, `Result`, `str::parse`) but not
the *format*. Rust keeps the core small and leaves serialization to crates such as `serde_json`,
which this project forbids. Writing a codec tailored to one shallow, fixed schema is acceptable
*here*:

| Concern | General-purpose crate | Your codec |
|---|---|---|
| Inputs | arbitrary JSON | one file your own program wrote |
| Numbers | arbitrary precision / `f64` | raw lexeme as `String` |
| Keys | any string, map semantics | insertion order in `Vec` |
| Errors | rich positional diagnostics | offset plus short reason |
| Extensions | follows spec updates | frozen, documented subset |

Know the boundary: "accept any JSON file" needs a stronger implementation — revisit the no-crates
constraint rather than pretending this codec is general-purpose.

> **Professional habit — write the subset down.** Put a comment at the top of your `json` module
> listing what you support and reject. A documented subset is a decision; an undocumented one is a
> bug someone discovers later.

## 2. An in-memory representation

Use a **recursive enum**: one variant per value kind. `Json` owns its data, so a parsed tree has no
borrows into the input and can be moved freely.

```rust
// Study sketch — retype and complete yourself.
#[derive(Debug, Clone, PartialEq, Eq)]
enum Json {
    Null,
    Bool(bool),
    Number(String),          // the exact lexeme, e.g. "-12.5e+3"
    String(String),
    Array(Vec<Json>),
    Object(Vec<(String, Json)>),
}
```

`Debug` helps while developing, `Clone` makes modified copies easy, and `PartialEq, Eq` makes round
trips a one-line assertion.

### `Number(String)` instead of `f64`

`1`, `1.0`, and `1e0` all become the same `f64`, losing the original text; binary floating point
also cannot represent every integer above 2^53. IDs and timestamps want their exact digits.

| Representation | Advantage | Cost |
|---|---|---|
| `Number(String)` | exact text, trivially round-trips | caller parses when doing arithmetic |
| `Number(f64)` | arithmetic is easy | lossy, unpredictable printing |
| `Number(i64)` | exact integers | cannot express fractions/exponents |

Storing the *validated string* keeps the codec generic and lets the domain layer choose the
interpretation. "Validated" matters: arbitrary text must never enter this variant (§5.3).

### `Object(Vec<...>)` instead of a map

| Representation | Key order | Duplicate keys | Output |
|---|---|---|---|
| `Vec<(String, Json)>` | preserved | both retained, caller decides | deterministic |
| `HashMap<String, Json>` | unspecified | later insert silently overwrites | nondeterministic |
| `BTreeMap<String, Json>` | sorted by key | overwritten | deterministic but reordered |

Deterministic output makes diffs readable and tests stable. `Vec` also lets you detect duplicates
and error instead of silently choosing a winner. The cost is O(n) lookup, irrelevant for a handful
of fields.

> **Professional habit — choose structures for observable behavior.** "Use a `HashMap`" sounds
> natural, but an unordered map produces flaky output-comparison tests. Decide whether order is part
> of the contract.

## 3. Serializer design

The writer is a pure function from `&Json` to `String`; with validated values it cannot fail.

```rust
// Study sketch — retype and complete yourself.
fn write_json(value: &Json, out: &mut String) {
    match value {
        Json::Null => out.push_str("null"),
        Json::Bool(true) => out.push_str("true"),
        Json::Bool(false) => out.push_str("false"),
        Json::Number(s) => out.push_str(s),        // already validated
        Json::String(s) => write_string(s, out),
        Json::Array(items) => { /* bracket, join with ',', bracket */ }
        Json::Object(fields) => { /* bracket, join "key": value, bracket */ }
    }
}
```

**Algorithm.**

1. Match on the borrowed value; never take ownership just to write it.
2. Push exact lowercase literals for `Null` and both `Bool` variants.
3. For `Number`, push the stored lexeme unchanged — it was validated on the way in.
4. For `String`, push `"`, then each escaped character, then `"`.
5. For `Array`, push `[`, write elements in order separated by `,`, push `]`; empty → `[]`.
6. For `Object`, push `{`, write each escaped key, `:`, its value, separated by `,`; empty → `{}`.
7. Append **no** newline. The caller decides; a writer that always adds one cannot be composed.

### 3.1 String escaping

Iterate `chars()`, not bytes, so non-ASCII text passes through untouched: é is two bytes in memory
but legal directly in a JSON string.

| Input | Output |
|---|---|
| `"` | `\"` |
| `\` | `\\` |
| backspace `U+0008` | `\b` |
| form feed `U+000C` | `\f` |
| line feed `U+000A` | `\n` |
| carriage return `U+000D` | `\r` |
| tab `U+0009` | `\t` |
| other `U+0000`…`U+001F` | `\u00XX` (four hex digits total) |
| anything else | itself |

```text
input:  Say "hi"<newline><control 0x01>
output: "Say \"hi\"\n\u0001"
```

```rust
// Study sketch — retype and complete yourself.
fn write_string(s: &str, out: &mut String) {
    out.push('"');
    for ch in s.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            '\u{08}' => out.push_str("\\b"),
            '\u{0c}' => out.push_str("\\f"),
            c if c <= '\u{1f}' => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out.push('"');
}
```

Note the asymmetry: in Rust source the four characters `\n` are written `"\\n"`. Test exact bytes
when in doubt.

### 3.2 Optional pretty-printing

Add pretty output as a separate function or a `pretty: bool` parameter, never a global flag. Pass
`depth` recursively, indent with two spaces (not tabs), keep empty containers on one line, and put
a newline plus indentation before each element and before the closing bracket.

```rust
// Study sketch — retype and complete yourself.
fn write_json_pretty(value: &Json, out: &mut String, depth: usize) {
    // Arrays/objects: after '[' or '{', newline + indent(depth + 1).
    // Between elements: ',' + newline + indent(depth + 1).
    // Before ']' or '}': newline + indent(depth). Empty containers stay compact.
    // Scalars: delegate to write_json.
}
```

> **Professional habit — make output stable.** Decide once whether the file is compact or pretty,
> whether it ends with a newline, and whether keys keep insertion order. Encode those decisions in
> tests.

## 4. Parser architecture

The reader is a **recursive-descent parser**: one function per grammar rule, each calling the next
for nested values. It reads a byte slice, not `Chars`, because structural tokens are ASCII and byte
offsets make error locations unambiguous.

### 4.1 The cursor

```rust
// Study sketch — retype and complete yourself.
struct Parser<'a> {
    input: &'a [u8],
    pos: usize,
}

impl<'a> Parser<'a> {
    fn new(text: &'a str) -> Self { Self { input: text.as_bytes(), pos: 0 } }

    fn peek(&self) -> Option<u8> { self.input.get(self.pos).copied() } // no movement

    fn next(&mut self) -> Option<u8> {                 // value and advance
        let byte = self.peek()?;
        self.pos += 1;
        Some(byte)
    }

    fn bump(&mut self) { self.pos += 1; }              // only after peek was Some

    fn skip_ws(&mut self) {
        while matches!(self.peek(), Some(b' ' | b'\t' | b'\n' | b'\r')) { self.bump(); }
    }
}
```

`peek` returns `Option<u8>`, so end-of-input is ordinary, not a panic. `bump` is fast but can
overrun; treat "bump only after `Some`" as an invariant. `'a` says the parser borrows the input;
the returned `Json` owns its strings (`02-ownership-and-borrowing.md`).

### 4.2 Errors with a byte offset

```rust
// Study sketch — retype and complete yourself.
#[derive(Debug, Clone, PartialEq, Eq)]
struct JsonError {
    offset: usize,   // index into the original input, in bytes
    reason: &'static str,
}
```

Use `&'static str` for fixed categories (`"expected value"`, `"trailing data"`); switch to `String`
only for dynamic detail such as an offending character. Every fallible method returns
`Result<T, JsonError>`, using `?` to propagate. See `04-errors-and-validation.md` for `Display` and
`std::error::Error`. A good message is `expected ',' or ']' at byte 12`, not `invalid json`.

### 4.3 Dispatch and the top level

After optional whitespace, one byte decides which parser to call:

| Next byte | Call | Result |
|---|---|---|
| `n` / `t` / `f` | `parse_literal` | `null` / `true` / `false` |
| `"` | `parse_string` | `Json::String` |
| `[` | `parse_array` | `Json::Array` |
| `{` | `parse_object` | `Json::Object` |
| `-`, `0`…`9` | `parse_number` | `Json::Number` |
| other / `None` | error | `expected value` |

```rust
// Study sketch — retype and complete yourself.
fn parse_value(&mut self) -> Result<Json, JsonError> {
    self.skip_ws();
    match self.peek() {
        Some(b'n') => self.parse_literal(b"null", Json::Null),
        Some(b't') => self.parse_literal(b"true", Json::Bool(true)),
        Some(b'f') => self.parse_literal(b"false", Json::Bool(false)),
        Some(b'"') => self.parse_string().map(Json::String),
        Some(b'[') => self.parse_array(),
        Some(b'{') => self.parse_object(),
        Some(b'-' | b'0'..=b'9') => self.parse_number(),
        _ => self.err("expected value"),
    }
}
```

`parse_literal` must check that the remaining input `starts_with` the literal before advancing;
otherwise `nulX` is accepted as `null`.

**Top-level algorithm.**

1. Build a `Parser` from the file contents as `&str`.
2. Call `parse_value`, which skips leading whitespace and consumes exactly one value.
3. Call `skip_ws` again.
4. If `pos != input.len()`, return `Err("trailing data")` with the current offset.
5. Otherwise return the `Json`.

Wrap those steps in `parse_document()`, the entry point used by file loading. That one check
enforces "one top-level value" and rejects `[] // comment` without comment-specific logic.

## 5. The value parsers, step by step

### 5.1 Strings

Build a `Vec<u8>` and convert once at the end.

1. Consume the opening `"`.
2. Loop on `peek()`: `None` → `unterminated string`; `"` → consume and stop; `\` → consume it and
   the next byte, then translate (table below); byte `< 0x20` → `unescaped control character`;
   anything else → push the byte and advance.
3. Finish with `String::from_utf8(bytes)`, mapping failure to `invalid utf-8`.

| Escape byte | `"` | `\` | `/` | `b` | `f` | `n` | `r` | `t` | `u` |
|---|---|---|---|---|---|---|---|---|---|
| Appended | `0x22` | `0x5C` | `0x2F` | `0x08` | `0x0C` | `0x0A` | `0x0D` | `0x09` | see below |

Copying multi-byte UTF-8 byte-for-byte preserves it exactly, and control checks are numeric
comparisons; you never need to decode a code point.

**Documented limitation — `\u` escapes and surrogate pairs.** `\uXXXX` names a Unicode code point;
UTF-8 cannot encode surrogates `U+D800`…`U+DFFF`, so `\uD83D\uDE00` (😀) must be combined into one
code point first. Choose and document one policy: reject all `\u` escapes, or support non-surrogate
four-digit escapes and reject surrogates. Real UTF-8 typed into the file works either way.

### 5.2 Arrays

1. Consume `[`, skip whitespace; `]` immediately → empty `Vec`.
2. Otherwise loop: skip whitespace, `parse_value`, push; skip whitespace, inspect `peek()`:
   `,` → consume and continue; `]` → consume and return; anything else → `expected ',' or ']'`.
3. A trailing comma fails automatically: after `,` the loop calls `parse_value`, sees `]`, and
   returns `expected value`.

### 5.3 Objects and numbers

Objects follow the same shape with a key and colon before the value:

1. Consume `{`, skip whitespace; `}` immediately → empty object.
2. Loop: skip whitespace, require `"` (`expected string key`), call `parse_string`.
3. Skip whitespace, require `:`, consume, skip whitespace, call `parse_value`, push `(key, value)`.
4. Skip whitespace: `,` continues, `}` returns, anything else errors.
5. To reject duplicates, scan the pairs collected so far before pushing; to use last-wins semantics,
   state that explicitly.

The JSON number grammar:

```text
number   = [ '-' ] int [ fraction ] [ exponent ]
int      = '0' | [ '1'..'9' ] { '0'..'9' }
fraction = '.' { digit }+             (at least one digit)
exponent = ('e' | 'E') [ '+' | '-' ] { digit }+
digit    = '0'..'9'
```

1. Remember the start offset; consume an optional `-`.
2. If the next byte is `0`, consume it and stop integer parsing — a following digit becomes
   trailing data. Otherwise require `1`…`9`, then consume any number of `0`…`9`.
3. On `.`, consume it and require at least one digit; consume the rest.
4. On `e`/`E`, consume it, optionally `+`/`-`, then require at least one digit.
5. Slice the exact bytes from start to current position, convert with `from_utf8`, and wrap in
   `Json::Number`. Every consumed byte is ASCII, so conversion cannot fail; use
   `String::from_utf8_lossy` if you want no `expect` anywhere.

This rejects `01`, `+1`, `.5`, `1.`, `1e`, `--1`, `NaN`, and `Infinity`. The parser may stop before a
bad following byte (`[1x]`); the enclosing array's punctuation check or the top-level check catches
it.

## 6. Mapping `Json` to your own types

A parsed tree is generic. Convert it at the boundary with the standard conversion traits:
`TryFrom<Json>` when conversion can fail (almost always), `From<T> for Json` when it cannot.

```rust
// Study sketch — retype and complete yourself.
#[derive(Debug, PartialEq, Eq)]
struct Envelope { id: u64, label: String, tags: Vec<String> }

impl TryFrom<Json> for Envelope {
    type Error = JsonError; // or a domain error wrapping it
    fn try_from(value: Json) -> Result<Self, Self::Error> {
        let Json::Object(fields) = value else { return Err(/* expected object */) };
        // let id_text = expect_string_field(&fields, "id")?;
        // let id = id_text.parse::<u64>().map_err(|_| /* bad id */)?;
        // let label = expect_string_field(&fields, "label")?;
        // let tags = expect_string_array_field(&fields, "tags")?;
        // Ok(Self { id, label, tags })
        todo!("finish the validation")
    }
}

impl From<Envelope> for Json {
    fn from(e: Envelope) -> Self {
        Json::Object(vec![
            ("id".to_string(), Json::Number(e.id.to_string())),
            ("label".to_string(), Json::String(e.label)),
            // ...
        ])
    }
}
```

| Check | Question |
|---|---|
| presence | Is the key required? What error names the missing key? |
| type | Is the variant exactly right? No silent `Number` → `String` coercion. |
| range | Does the integer fit the target type? Does the date string have the right shape? |
| unknown keys | Ignore for compatibility, or reject to catch typos? Decide and test. |
| duplicates | Error on the first duplicate, or define last-wins explicitly? |

Write generic helpers (`expect_object`, `expect_string`, `expect_array`) and reuse them for every
field. `missing field "label"` is actionable; `convert failed` is not.

> **Professional habit — validate at the boundary, once.** Bytes → `Json` → typed value, then let
> the rest of the program assume valid data. Ad-hoc checks scattered through command handlers
> hide bugs.

## 7. Tests to write

Test behavior, not private helper names. See `10-testing-and-debugging.md` for test strategy.

| Group | Cases |
|---|---|
| round trip | `write(parse(s)) == s` for canonical inputs, including empty `[]` and `{}` |
| strings | `\"`, `\\`, `\n`, `\t`, real unicode, control bytes |
| nesting | array of objects, object of arrays, three levels deep |
| numbers | `0`, `-1`, `12.5`, `1e3`, `-2.5E-4` preserved exactly |
| whitespace | around all punctuation and before/after the top-level value |
| malformed | `{`, `[1,]`, `{"a":}`, `{"a" 1}`, `nul`, `"abc`, `01`, `1.`, `truX` |
| errors | offsets point at the offending byte for at least three failures |
| trailing | `{} {}`, `[] x`, `true false`, `null // comment` |
| top level | bare string/number/bool/`null` parse; domain conversion may reject them |

```rust
// Study sketch — retype and complete yourself.
#[cfg(test)]
mod tests {
    use super::*;

    fn round_trip(text: &str) {
        let parsed = Parser::new(text).parse_document().unwrap();
        let mut output = String::new();
        write_json(&parsed, &mut output);
        assert_eq!(output, text);
    }

    #[test]
    fn compact_round_trips() {
        round_trip("null");
        round_trip("[1,-2.5,true,false,\"hi\",{\"k\":[]}]");
        round_trip("{}");
        round_trip("\"say \\\"hi\\\"\\n\"");
        round_trip("\"caf\u{e9}\""); // real text, not a \u escape
    }

    #[test]
    fn trailing_data_is_rejected() {
        let err = Parser::new("{} {}").parse_document().unwrap_err();
        assert_eq!(err.reason, "trailing data");
        assert_eq!(err.offset, 3);
    }
}
```

Also assert errors for invalid input. A suite containing only valid documents will happily accept
`nulX` as `null` for months.

## 8. Common mistakes

| Mistake | Symptom | Fix |
|---|---|---|
| Unescaped strings / forgotten `\` | `"a"b"` breaks the file; `\n` becomes a newline | Route **all** strings through `write_string` |
| Accepting any number text | `NaN`, `01`, `1e` slip through | State machine with mandatory digits |
| A loop that fails without advancing | `{x:1}` hangs | Every iteration must `bump` or return `Err` |
| `unwrap()` / indexing | panic on `{"a"` | Use `Option`/`peek`, return `JsonError` |
| Off-by-one offset | message points past the token | Record offset *before* consuming |
| `HashMap` for fields | output order changes between runs | `Vec` or `BTreeMap` |
| Ignoring trailing bytes | `[] garbage` accepted | Check `pos == input.len()` at top level |
| Losing number text | IDs print as `1.0` | Store the validated lexeme |
| Mixing char and byte offsets | wrong location for non-ASCII | Count bytes; say "byte N" |

Also: skip whitespace after commas/colons (`{"a" :\n1}` is valid), and remember that JSON's `\n`
is written `"\\n"` in Rust source.

> **Professional habit — never panic on user data.** Search the codec for `unwrap`, `expect`, and
> indexing; justify every remaining occurrence as operating on your own validated output.

## 9. Apply it to Task Tracker

1. Which `Json` variants may appear at the top level of `tasks.json`, and what error should anything
   else produce?
2. For `{"id":1,"id":2,...}`, what will your parser do, and why is that better than the alternative?
3. Which fields are required, and what exact error should a missing field produce?
4. Compact or pretty? Trailing newline or not? Write the first 120 expected bytes as a test fixture.
5. How will `JsonError` be wrapped into the CLI error type so `main` prints one line and exits
   non-zero (`04-errors-and-validation.md`)?
6. How will you atomically replace the file so a crash cannot leave half-written JSON
   (`07-filesystem-and-cli.md`)?

## 10. Checkpoint

- [ ] I can list the seven JSON value kinds and three things JSON forbids.
- [ ] I can explain why `Number(String)` and `Object(Vec<...>)` were chosen.
- [ ] I can write `write_json` recursively and escape all seven required characters.
- [ ] I can say what `peek`, `next`, `bump`, `skip_ws` do and why `peek` returns `Option`.
- [ ] I can dispatch a value from its first byte and validate a number without a regex.
- [ ] I can explain where and why trailing data is rejected.
- [ ] I can convert `Json` to a typed value with `TryFrom` and report missing/wrong fields.
- [ ] My tests cover escapes, nesting, empty containers, whitespace, and malformed input.

If a box fails, retype the sketch and add a test for the behavior that confused you.

## Next

Continue with `07-filesystem-and-cli.md` to connect your codec to real files and positional
arguments. Then `08-time-without-chrono.md` covers the other std-only problem: epoch seconds to
UTC strings and back.
