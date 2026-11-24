# Task Tracker in Rust — Self-Contained Learning Path

> **Purpose.** This is your tutor, reference manual, and build journal for implementing the
> roadmap.sh Task Tracker CLI in Rust and, optionally, an HTTP API for it. It is deliberately
> complete: after reading the relevant chapters you should never need the internet to understand
> a Rust concept used by the project. The Rust toolchain installs full documentation locally
> (`rustup doc --std`, `--book`, `--reference`, `--cargo`), and the standard-library source is on
> disk. Those are your primary references. Dependency docs become local too after `cargo add`
> via `cargo doc --open --package <name>`.
>
> **Important boundary.** These documents teach concepts and algorithms. They do **not** contain
> a finished Task Tracker implementation. You will write that yourself, phase by phase, using
> `11-feature-by-feature.md` for the CLI and `16-api-feature-by-feature.md` for the API.
>
> **Two tracks.** Chapters `00`–`13` build the required std-only CLI: no crates, a hand-written
> JSON codec, and a hand-written UTC formatter. Chapters `14`–`16` are an optional Phase 2.
> They first explain HTTP with bare `std::net`, then use Axum, Tokio, and serde because those are
> the right tools for a real API. Finish the CLI first; the API reuses its core.

---

## 1. The special challenge of Rust here

The required CLI constraints say **no external libraries or frameworks**. In Rust that has two
consequences you must plan for:

1. **The standard library has no JSON support.** There is no `serde_json`. You will implement a
   small, documented JSON reader and writer yourself. `06-json-without-crates.md` teaches the
   grammar, a byte-by-byte parser design, string escaping, and error reporting in enough detail
   to do this offline.
2. **The standard library has no calendar-date formatter.** `std::time` gives you
   `SystemTime` and seconds since the Unix epoch, but no "2026-11-24T09:15:00Z". You will write
   a tiny UTC conversion utility. `08-time-without-chrono.md` gives you the complete algorithm
   and its reasoning.

This is more work than the Go version, but it is also the fastest way to truly understand Rust:
you will meet ownership, errors, iterators, traits, and testing in a real program.

For the optional API track the constraint changes deliberately. HTTP has enough protocol detail
(fragmented reads, headers, chunked bodies, keep-alive, concurrency, routing, async) that writing
it barebones is educational once but wasteful as a final design. Chapter `14` gives you that one
educational pass with `TcpListener`. Chapter `15` then introduces Axum, Tokio, and serde for the
real API. This is not a contradiction: the CLI core stays std-only, and the API boundary uses
tools chosen for the job.

---

## 2. Ground rules

1. **Type everything yourself.** Short snippets in these notes illustrate isolated ideas. Retype
   them into a scratch project, run them, break them, fix them.
2. **Use the offline reference, not the web.**
   ```bash
   rustup doc --std        # standard library API docs in your browser
   rustup doc --book       # The Rust Programming Language
   rustup doc --reference  # language reference
   rustup doc --cargo      # Cargo book
   rustc --print sysroot   # toolchain root; std source under lib/rustlib/src/rust/library
   ```
   For API chapters, add dependencies once, then read their docs locally:
   ```bash
   cargo doc --open --package axum
   cargo doc --open --package tokio
   cargo doc --open --package serde
   cargo tree
   ```
   When the compiler suggests a fix, read the whole error — Rust's diagnostics are tutorials.
3. **One feature at a time.** Follow `11-feature-by-feature.md` for the CLI and
   `16-api-feature-by-feature.md` for the API; test after each phase.
4. **Keep a build journal.** After each phase, record: what you implemented, which compiler or
   borrow-checker error confused you, how you fixed it, and one Rust idea that became clearer.
5. **Professional habits from day one.** `cargo fmt`, `cargo clippy`, `cargo check`, zero
   unwraps in normal paths, errors returned with context, small modules, and tests for behavior.
   See `12-professional-habits.md`.

---

## 3. How the requirements map to Rust concepts

| Requirement | Rust concept you must learn | Chapter(s) |
|---|---|---|
| Runs from CLI, positional arguments | `std::env::args`, `str::parse`, dispatch with `match`, `ExitCode`, stdout/stderr | `00`, `01`, `03`, `07` |
| Store tasks in JSON in current directory | `std::fs`, `Path`/`PathBuf`, `env::current_dir`, custom JSON codec | `06`, `07` |
| JSON file created if missing | `ErrorKind::NotFound`, `OpenOptions`, `metadata` | `04`, `07` |
| Add / update / delete | structs, `Vec`, ownership, traits, service functions | `02`, `03`, `05`, `09` |
| Mark in-progress / done | enums, `match`, exhaustive status handling | `03`, `11` |
| List all / filtered | iterators, `filter`, `collect`, formatting | `01`, `05`, `07` |
| id / description / status / createdAt / updatedAt | structs, `String` vs `&str`, custom enums, UTC epoch conversion | `02`, `03`, `08`, `06` |
| Handle errors and edge cases gracefully | `Result`, `?`, custom error enum, `Display`, `Error`, `From` | `04` |
| No external libraries (CLI track) | std-only implementation of JSON and time formatting | `06`, `08` |
| Clean code, README, tests | modules, `#[cfg(test)]`, temp dirs, `cargo test`, `clippy` | `09`, `10`, `12` |
| Optional HTTP API (Phase 2) | `TcpListener` fundamentals, Axum, Tokio, serde DTOs, extractors, shared state, Tower testing | `14`, `15`, `16` |

---

## 4. The chapters, in order

| # | File | What you will be able to do | Approx. effort |
|---|---|---|---|
| 00 | `00-toolchain-and-cargo.md` | Use rustup, cargo, rustfmt, clippy, and the offline docs; understand crates and `Cargo.toml` | 1–2 h |
| 01 | `01-rust-basics.md` | Write small Rust programs: bindings, types, functions, expressions, control flow, printing | 2–3 h |
| 02 | `02-ownership-and-borrowing.md` | Reason about moves, borrows, slices, and lifetimes without fighting the compiler | 3–4 h |
| 03 | `03-structs-enums-and-match.md` | Model records and statuses with structs, enums, `Option`, and exhaustive `match` | 2–3 h |
| 04 | `04-errors-and-validation.md` | Design a std-only error type, use `?`, `map_err`, `ok_or`, and exit cleanly | 2–3 h |
| 05 | `05-collections-and-strings.md` | Use `Vec`, `String`, `&str`, `HashMap`/`BTreeMap`, and iterators fluently | 2–3 h |
| 06 | `06-json-without-crates.md` | Write a minimal JSON encoder and decoder by hand, with escaping and useful errors | 4–6 h |
| 07 | `07-filesystem-and-cli.md` | Parse positional args, resolve paths, read/write files safely, replace atomically | 2–3 h |
| 08 | `08-time-without-chrono.md` | Convert epoch seconds to UTC date/time strings and parse them back, std-only | 2–3 h |
| 09 | `09-modules-and-architecture.md` | Split a crate into modules with clear privacy and dependency direction | 2 h |
| 10 | `10-testing-and-debugging.md` | Unit-test with std only, create temp dirs, inject clock/store, debug compile errors | 2–3 h |
| 11 | `11-feature-by-feature.md` | Implement the full CLI in small, verified phases | 8–12 h (spread out) |
| 12 | `12-professional-habits.md` | Apply formatting, linting, naming, error, and documentation standards | 1–2 h |
| 13 | `13-troubleshooting-and-cheatsheet.md` | Diagnose compiler/borrow errors and look up syntax quickly | reference |
| 14 | `14-api-barebones.md` | Explain HTTP as bytes over TCP and build one educational std-only server | 2–3 h |
| 15 | `15-axum-api.md` | Use Axum, Tokio, extractors, DTOs, shared state, errors, and `oneshot` tests | 4–6 h |
| 16 | `16-api-feature-by-feature.md` | Add the API in small tested phases without rewriting the CLI core | 6–10 h (spread out) |

Total for the CLI track: roughly 35–50 focused hours. The JSON and time chapters are the long
poles; that is expected and is precisely why they are taught in full. Add roughly 12–19 hours
for the optional API track.

---

## 5. Suggested weekly rhythm

- **Session A (learn, ~90 min).** Read one chapter. Create a scratch crate under
  `/tmp/opencode/rust-scratch` and retype the snippets. Predict compiler errors before running.
- **Session B (apply, ~90 min).** Do the chapter's "Apply it to Task Tracker" exercise in the real
  crate. Run `cargo check`, `cargo test`, `cargo clippy` before committing.
- **Session C (consolidate, ~30 min).** Re-read your notes. Explain the hardest borrow error out
  loud. Add three lines to your journal.

If you only have 30 minutes, still do Session A. Consistency beats intensity.

---

## 6. Definition of done

### CLI track

- [ ] `cargo run -- add "Buy groceries"` prints `Task added successfully (ID: 1)`.
- [ ] `tasks.json` appears in the current directory with all five required properties.
- [ ] `update`, `delete`, `mark-in-progress`, and `mark-done` work and update `updatedAt`.
- [ ] `list`, `list todo`, `list in-progress`, `list done` produce correct subsets.
- [ ] Missing files, corrupt JSON, bad arguments, non-numeric IDs, unknown IDs, and invalid
      statuses produce clear errors and non-zero exit codes.
- [ ] Writes are atomic: an interrupted write never leaves invalid JSON.
- [ ] For the CLI track, no external crates appear in `Cargo.toml`; `Cargo.lock` contains only
      your own package. The API track adds dependencies deliberately in chapter `15`.
- [ ] `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings`, and `cargo test` pass.
- [ ] `README.md` explains build, commands, JSON schema, timestamp format, and error behavior.
- [ ] You can explain every line of your code without notes.

The last bullet is the actual goal.

### Optional API track

After chapters `14`–`16`, add these checks:

- [ ] A barebones `TcpListener` experiment can read a complete request and return a correct
      response with `Content-Length`, and you can explain why it is not the final design.
- [ ] Axum serves `GET /healthz` and the task routes on a known address.
- [ ] `POST`, `GET`, `PATCH`, and `DELETE` routes cover the same behavior as the CLI.
- [ ] `GET /tasks?status=todo` and the other filters return correct subsets.
- [ ] Malformed JSON, unknown fields, invalid statuses, invalid IDs, unknown IDs, unknown
      routes, and wrong methods produce consistent JSON errors with correct status codes.
- [ ] Shared state is cloneable, locks are short, and no lock is held across `.await`.
- [ ] Handler tests use `tower::ServiceExt::oneshot` and need no live port.
- [ ] `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings`, and `cargo test` pass.
- [ ] The README explains how to run the CLI and API, the route map, and example requests.
- [ ] You can explain what Axum and Tokio do for you that the barebones server did not.

---

## 7. Local reference map (no internet required)

```text
rustup doc --std          # standard library
rustup doc --book         # The Rust Programming Language
rustup doc --reference    # syntax and semantics
rustup doc --cargo        # Cargo, features, profiles
rustc --print sysroot     # locate installed source and docs
cargo help <command>      # e.g. cargo help test
cargo <command> --help    # e.g. cargo clippy --help
cargo doc --open --package axum     # after adding the API dependencies
cargo doc --open --package tokio
cargo doc --open --package serde
cargo tree                          # see the dependency graph you chose
```

Useful APIs to look up as you meet them:
`std::env::args`, `std::env::current_dir`, `std::fs::read_to_string`,
`std::fs::OpenOptions`, `std::io::Write`, `std::path::PathBuf`, `std::time::SystemTime`,
`std::process::ExitCode`, `Iterator::filter`, `Result::map_err`, `Option::ok_or`,
`std::error::Error`, `std::fmt::Display`. For the API track add `std::net::TcpListener`,
`std::io::Read`, `std::io::Write`, `std::thread::spawn`, `axum::Router`, `axum::serve`,
`axum::extract::Path`, `axum::extract::Query`, `axum::extract::State`, `axum::Json`,
`axum::response::IntoResponse`, `tokio::net::TcpListener`, and `tower::ServiceExt::oneshot`.

---

## 8. The existing folders

```text
rust/
├── learnrust/            # loose rustc exercise, not a Cargo package
│   ├── main.rs           # formatting experiments; contains a typo, see below
│   ├── main              # an old compiled binary — ignore it
│   └── Cargo.lock        # stale leftover; no Cargo.toml exists here
├── starter-project/      # real Cargo package, edition 2024
│   ├── Cargo.toml
│   └── src/main.rs
└── docs/                 # this learning path
```

`learnrust/main.rs` is a loose file of `println!` experiments and contains a typo
(`printl!`) that will not compile. Its `Cargo.lock` and compiled `main` are leftovers from
some earlier experiment; treat the whole directory as scratch, not a starting point. Use
`starter-project/` as the real crate: it already declares `edition = "2024"` and starts with an
empty `[dependencies]` section, matching the std-only CLI track. In chapter `15` you will add
Axum and friends there deliberately for the API track. You may rename the package to something
like `task-cli` when you are ready; decide deliberately and record it.

`cargo run` from `starter-project/` runs `src/main.rs`. Relative paths such as `tasks.json`
resolve against the shell's current directory, **not** the crate directory. Use a scratch
directory for smoke tests, just as in the Go path.

---

## 9. Final encouragement

Rust's compiler is strict because it is trying to tell you the truth about memory and failure.
Every "borrow checker" fight you win here makes the next one shorter. Build the small JSON and
time utilities first, keep them tested, and the rest of the CLI will feel straightforward. Then
write one tiny HTTP server by hand so Axum's conveniences make sense, and use Axum for the real
API.

Open `00-toolchain-and-cargo.md` and begin. Return to `14-api-barebones.md` after the CLI
checkpoint passes.
