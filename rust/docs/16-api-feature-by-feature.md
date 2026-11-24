# 16 — API feature by feature in Rust

> **Purpose.** This is a behavioral specification for adding an HTTP API to the Task Tracker crate
> you already built. It says *what* each phase must do, *where* each responsibility belongs, and
> *how to prove* it works. It contains no finished implementation: no handler set, no `AppState`
> definition, no DTO definitions, no route table code, and no `main.rs` to paste. Prerequisites are
> chapters 00–13, especially the CLI core from `11-feature-by-feature.md`, plus the concepts in
> `14-api-barebones.md` and `15-axum-api.md`.

## Why this matters

A second transport is a design test. The CLI and the API will both create, list, update, mark, and
delete tasks. If those rules are duplicated in both front ends, the two will drift: the CLI will
treat a blank description one way and the API another, and one day `tasks.json` will contain a value
neither side intended. If instead the domain rules stay in the modules you already wrote, the API
is mostly translation: JSON in, domain call, domain result out, JSON back.

Phasing matters for the same reason it mattered in chapter 11. A server has more moving parts than a
CLI: routing, extraction, state, async, locking, and error mapping. Adding all of them while also
inventing the create endpoint gives any bug four places to hide. Start with a health route that
proves the plumbing, then add one endpoint at a time, checking after each.

## What you will learn

- Add an HTTP transport to an existing crate without rewriting the CLI core.
- Choose between a single dispatch binary and a separate `src/bin/api.rs` entry point.
- Map every CLI behavior to a method, path, status code, and JSON shape.
- Design DTOs, a shared state type, and one error envelope for the whole API.
- Phase the work from health check through CRUD, transitions, concurrency, and tests.
- Decide deliberately whether the hand-rolled JSON codec stays as the file format.
- Finish with a definition of done you can verify offline.

## 1. What this track adds — and what it does not

The CLI core from chapters 00–13 is already done: `task`, `store`, `json`, `time`, `cli`, and the
error type. This track **adds a second binary and an HTTP transport**. It does not replace the CLI,
and it does not require rewriting the store or the UTC utility.

Use `starter-project/` as the real crate. Add the API dependencies **only when you begin Phase 1**;
until then, leave the std-only crate from the CLI path intact. That keeps your earlier commits
clean and lets you compare "before" and "after" via Git.

> **Professional habit.** A new interface is a reason to review old boundaries, not a reason to
> rebuild them. Change a module only when the API exposes a real gap in its contract.

### Entry point options

| Option | Shape | Tradeoff |
|---|---|---|
| First-argument dispatch | `src/main.rs` inspects `args[1]` and calls either CLI or API code | One binary and one build command; the entry point mixes two very different lifecycles |
| Separate binary | `src/bin/api.rs` next to `src/main.rs` | Two binaries (`task-cli` and `api`); clean separation, a little duplicated startup wiring |
| Cargo features | Conditional compilation of the API modules | Smaller single builds, but more configuration and a feature matrix to test |

Keep it simple: choose the first-argument dispatch or a separate `src/bin/api.rs`. This document
does not implement either. If you value an untouched CLI binary and obvious `cargo run --bin api`,
choose the separate binary. If you value one executable and minimal files, choose dispatch. Record
the choice in your README.

### Module layout to grow into

```text
starter-project/
├── Cargo.toml
├── src/
│   ├── main.rs          # thin CLI entry point (already exists)
│   ├── bin/
│   │   └── api.rs       # optional thin HTTP entry point
│   ├── cli.rs           # existing CLI parsing
│   ├── lib.rs           # exposes shared modules to both binaries and tests
│   ├── task.rs          # existing domain model and status type
│   ├── store.rs         # existing load/save and next-id rules
│   ├── time.rs          # existing UTC clock seam
│   ├── error.rs         # existing application error type
│   ├── json.rs          # existing hand-rolled codec (if you kept it)
│   └── api/
│       ├── mod.rs       # router assembly and state wiring
│       ├── dto.rs       # request/response types and conversions
│       ├── error.rs     # HTTP error type and JSON envelope
│       └── routes.rs    # thin handlers calling domain/store functions
└── tests/               # in-process router tests and CLI smoke tests
```

The dependency direction is one-way: `api` depends on the shared domain modules; the domain modules
know nothing about HTTP. If you ever find `axum` imported in `task.rs` or `store.rs`, the layering
has broken.

## 2. Route map

Use these routes as the contract. Path parameters follow Axum 0.8's `{id}` syntax.

| CLI command | Method and route | Success | Meaning |
|---|---|---|---|
| `add <description>` | `POST /tasks` | `201 Created` | Create a task; include `Location: /tasks/{id}` |
| `list` | `GET /tasks` | `200 OK` | All tasks, ascending numeric ID, `[]` when empty |
| `list todo` / `in-progress` / `done` | `GET /tasks?status=todo` etc. | `200 OK` | Exact status filter, same order |
| (no CLI equivalent) | `GET /tasks/{id}` | `200 OK` | Fetch one task by ID |
| `update <id> <description>` | `PATCH /tasks/{id}` | `200 OK` | Replace the description |
| `mark-in-progress <id>` | `POST /tasks/{id}/mark-in-progress` | `200 OK` | Set status to `in-progress` |
| `mark-done <id>` | `POST /tasks/{id}/mark-done` | `200 OK` | Set status to `done` |
| `delete <id>` | `DELETE /tasks/{id}` | `204 No Content` (or `200` with a body) | Remove one task |
| (health probe) | `GET /healthz` | `200 OK` | Liveness check with a tiny JSON body |

Two contract choices need a written decision:

- **Delete response.** `204` is lean but has no body; `200` with a confirmation body is friendlier
  to scripted clients. Pick one and test it.
- **Status transitions.** The table uses explicit action routes because they mirror the CLI
  commands. An alternative is `PATCH /tasks/{id}` with a `status` field. Both are defensible; do not
  silently support both with different rules.

## 3. Rules that govern every phase

**Wire format.** JSON bodies with `Content-Type: application/json`. Request and response shapes are
defined in `api/dto.rs`, never inferred from domain structs. Timestamps keep the CLI's
`YYYY-MM-DDTHH:MM:SSZ` format and the JSON key `createdAt` / `updatedAt`.

**Status vocabulary.** Only `todo`, `in-progress`, and `done`, exactly as in chapter 11. Encode the
constraint in a type (an enum with serde renaming) so an invalid string fails at the boundary.

**Error envelope.** Every error, including ones you map from extractor rejections, uses one shape:
`{ "error": "<code>", "message": "<human sentence>" }`. Keep internal details out of `500`
responses; log them on the server.

**Store path.** Relative paths resolve against the process's current directory. Run the API from a
scratch directory during smoke tests so repository state is untouched. Decide whether the API
accepts a configurable data file path through state; hard-coding a path is acceptable for a learning
project but should be documented.

**Clock.** One clock read per operation. Inject it through state or a constructor, exactly as
chapter 10 did for tests. Never call the wall clock from a DTO or a display formatter.

**No-op discipline.** Carry over chapter 11's table: identical `description` on update changes
nothing; marking a task with the status it already has changes nothing. Both still succeed.

**Testability.** Handlers remain thin: extract values, call a domain function, map the result to a
response. Pure logic stays in domain modules where a unit test can reach it without a router.

> **Professional habit.** Write these rules into the API section of the README *before* Phase 3.
> A contract written once prevents a dozen small inconsistencies.

## 4. Phase 1 — Dependencies and a health server

**Goal.** A running Axum server with a single generic health route, proving binding, routing, and
serving before any task concept exists.

**Files to touch.** `Cargo.toml`, your chosen entry point (`src/bin/api.rs` or `src/main.rs`), and a
short README section describing how to start the server.

**Work.**

- Add the dependencies from chapter 15: `axum`, `tokio` with `full`, `serde` with `derive`,
  `serde_json`, and `tower` with `util` as a dev-dependency.
- Build the minimal `Router` with a `GET /healthz` handler returning a small JSON status object.
- Bind `127.0.0.1:3000` with `tokio::net::TcpListener` and serve with `axum::serve`.
- Keep the handler generic (`Health`-style); do not mention tasks yet.

**Check commands.**

```bash
cargo tree
cargo check
cargo run --bin api                # or your chosen command
curl -i http://127.0.0.1:3000/healthz
curl -i -X POST http://127.0.0.1:3000/healthz
```

**Check question.** What proves the server is listening on the address you intended — and what does
the `POST` response tell you about how Axum handles a known path with the wrong method?

## 5. Phase 2 — Module layout, state, and the error type

**Goal.** Create the `api/` module boundary, decide how shared dependencies reach handlers, and
define one HTTP error type before there are real endpoints to scatter errors across.

**Files to touch.** `src/api/mod.rs`, `src/api/error.rs`, `src/lib.rs` exports, and the entry point
wiring from Phase 1.

**Work.**

- Sketch `api/mod.rs` as the place that assembles the router. Keep route *definitions* in
  `api/routes.rs` once that file exists; for now a health route is enough.
- Design a clonable state type conceptually: handles to the existing store and clock, plus settings
  such as the data file path. Use `Arc` for shared ownership when the underlying value should exist
  once. Decide whether mutation needs a mutex now or in Phase 7.
- In `api/error.rs`, define an HTTP error enum with cases for bad request, not found, and internal
  failure. Implement `IntoResponse` so each case becomes the same JSON envelope with the right
  `StatusCode`.
- Decide how domain errors from your existing `error.rs` map into the HTTP error type. Prefer an
  explicit `From` conversion where the meaning is mechanical; add context where it is not.
- Add a `Router::fallback` plan for unknown routes so `404` can also use the envelope. Record that
  Axum's built-in `405` body may need a custom mapping if you require identical error shapes
  everywhere.

**Check commands.**

```bash
cargo check
cargo doc --open --package axum
```

**Check question.** How does a handler receive the store without a global variable, and which layer
turns a domain "unknown ID" into an HTTP `404`?

## 6. Phase 3 — DTOs and the create endpoint

**Goal.** Accept a JSON body, validate it at the boundary, create a task through the existing domain
code, and return `201` with a `Location` header.

**Files to touch.** `src/api/dto.rs`, `src/api/routes.rs`, `src/api/mod.rs`, and, only if a real gap
appears, `src/task.rs` or `src/store.rs`.

**Work.**

- Define a request DTO with exactly the fields the client may set (a description). Decide and
  document whether unknown fields are rejected with `#[serde(deny_unknown_fields)]` or ignored.
- Define a response DTO with the five task fields, using `#[serde(rename = "createdAt")]` and the
  same key set as chapter 11's JSON schema.
- Write one thin `POST /tasks` handler: extract `Json<CreateTaskRequest>`, validate the description,
  ask the domain layer to build a task with one clock read, save through the store, then return a
  `201` response with `Location: /tasks/{id}`.
- Reuse the store's next-ID rule from chapter 11; do not invent a second ID policy in the API.
- Map malformed JSON to `400` in your envelope. Map a corrupt store to `500` with a safe message.

**Check commands.**

```bash
cargo check && cargo test
curl -i -X POST http://127.0.0.1:3000/tasks \
  -H 'Content-Type: application/json' \
  -d '{"description":"Buy groceries"}'
curl -i -X POST http://127.0.0.1:3000/tasks \
  -H 'Content-Type: application/json' -d '{}'
```

**Check question.** Which layer rejected a blank description, and does the saved file have exactly
the schema your CLI produces?

## 7. Phase 4 — List, filter, and get

**Goal.** Read tasks back with a stable order, an optional status filter, and a single-task lookup.

**Files to touch.** `src/api/dto.rs` (query and response types), `src/api/routes.rs`, `src/api/mod.rs`.

**Work.**

- Define a query DTO with an optional status field. Model the three valid values as an enum with
  `#[serde(rename_all = "kebab-case")]` so `in-progress` deserializes cleanly and `in_progress` does
  not.
- For `GET /tasks`, return all tasks sorted by ascending numeric ID. An empty store returns `[]`,
  not an error.
- For `GET /tasks?status=...`, filter with the exact lowercase token. An invalid token is a client
  error (`400`), not an empty list — otherwise a typo looks like "no tasks".
- For `GET /tasks/{id}`, use `Path<u64>` so non-numeric IDs are rejected by extraction. An unknown
  but well-formed ID maps to `404` with the standard envelope.
- Do not write to `tasks.json` on any read path; a `GET` must not change the file's contents or
  modification time.

**Check commands.**

```bash
curl -i http://127.0.0.1:3000/tasks
curl -i 'http://127.0.0.1:3000/tasks?status=in-progress'
curl -i 'http://127.0.0.1:3000/tasks?status=pending'
curl -i http://127.0.0.1:3000/tasks/abc
stat tasks.json
```

**Check question.** Can you distinguish "filter matched nothing" from "filter token was invalid" in
both status code and body?

## 8. Phase 5 — Update and delete

**Goal.** Support partial description updates and deletion while preserving the no-op and timestamp
rules from the CLI.

**Files to touch.** `src/api/dto.rs`, `src/api/routes.rs`, and the store only if it lacks a needed
operation.

**Work.**

- Define a PATCH request DTO. If you use `Option<String>` for the description, decide how you treat
  an explicit `null`; either reject it or document that it means "not supplied".
- For `PATCH /tasks/{id}`, use the domain "did it change?" rule: a different description refreshes
  `updatedAt` and saves; an identical description succeeds without a rewrite. `createdAt` never
  changes.
- For `DELETE /tasks/{id}`, remove exactly one task; a second delete of the same ID is `404`, not
  silent success. Choose `204` or `200` from section 3 and apply it consistently.
- Keep validation in one place: the same non-empty description rule the CLI uses must apply here.
  If you discover the rule lives only in `cli.rs`, move it into the domain layer now — that is a real
  gap, not a rewrite.

**Check commands.**

```bash
curl -i -X PATCH http://127.0.0.1:3000/tasks/1 \
  -H 'Content-Type: application/json' -d '{"description":"Buy groceries and milk"}'
curl -i -X PATCH http://127.0.0.1:3000/tasks/1 \
  -H 'Content-Type: application/json' -d '{"description":"Buy groceries and milk"}'
curl -i -X DELETE http://127.0.0.1:3000/tasks/2
curl -i -X DELETE http://127.0.0.1:3000/tasks/2
```

**Check question.** Did the no-op PATCH leave both `updatedAt` and the file's modification time
unchanged, and did the second DELETE return `404`?

## 9. Phase 6 — Status transitions and `updatedAt`

**Goal.** Add the two action routes with the same transition semantics as the CLI's mark commands.

**Files to touch.** `src/api/routes.rs`, `src/task.rs` (if transitions are not yet explicit), and
the state wiring for the clock.

**Work.**

- Add `POST /tasks/{id}/mark-in-progress` and `POST /tasks/{id}/mark-done` as action routes. These
  bodies can be empty; the path names the action.
- Keep `Status` as an enum with three variants. A new status should force a compile error where a
  `match` is non-exhaustive rather than silently falling through.
- Apply chapter 11's rule: a real transition refreshes `updatedAt`; re-marking the current status is
  a successful no-op. `createdAt` is untouched.
- Read the clock once per operation through the injected seam. Tests must be able to assert an exact
  timestamp without consulting the wall clock.
- Return the updated task representation so a client can see the new status and timestamp in one
  response.

**Check commands.**

```bash
curl -i -X POST http://127.0.0.1:3000/tasks/1/mark-in-progress
curl -i -X POST http://127.0.0.1:3000/tasks/1/mark-done
curl -i -X POST http://127.0.0.1:3000/tasks/1/mark-done
curl -i http://127.0.0.1:3000/tasks?status=done
```

**Check question.** Which response field proves the second mark was a no-op, and where did that
decision come from — the handler or the domain type?

## 10. Phase 7 — Concurrency, locking, writes, and timeouts

**Goal.** Make the server safe when more than one client is active and when a client is slow.

**Files to touch.** The state definition in `src/api/mod.rs` or a helper module, `src/store.rs`, and
the entry point for graceful shutdown.

**Work.**

- Decide the locking model. If state is `Arc<Mutex<Store>>`, keep the critical section short: acquire,
  compute or mutate, save, drop the guard. Never hold a `std::sync::MutexGuard` across `.await`, and
  report lock poisoning instead of unwrapping it.
- Prefer one load-and-save cycle per mutating request. Read-only requests should not need the write
  lock at all if your design can avoid it.
- Reuse the atomic write pattern from chapter 12: write to a temporary file in the same directory,
  flush, close, rename. A crashed server must not leave half a JSON document.
- Bound the inputs: a maximum request body size and a timeout around request processing. `tower`
  has timeout support behind a feature; a `tokio::time::timeout` inside a `from_fn` middleware is
  another option. Promote any dependency to a normal dependency only if the server needs it at
  runtime.
- Add graceful shutdown with `axum::serve(...).with_graceful_shutdown(...)` and a Ctrl-C signal, so
  a running save is not cut in half.
- Run two clients that mutate the same task concurrently and confirm no update is silently lost.

**Check commands.**

```bash
cargo clippy --all-targets -- -D warnings
curl -i -X POST http://127.0.0.1:3000/tasks -H 'Content-Type: application/json' \
  -d '{"description":"concurrent one"}' &
curl -i -X POST http://127.0.0.1:3000/tasks -H 'Content-Type: application/json' \
  -d '{"description":"concurrent two"}' &
wait
```

**Check question.** If two creates receive the same next ID, which layer prevents it, and what does
the losing client see?

## 11. Phase 8 — Tests and the smoke checklist

**Goal.** Prove behavior in-process with fixed inputs, then verify the real binary once.

**Files to touch.** `src/api/routes.rs` or `tests/api.rs`, the README, and test helpers for temporary
directories and a fixed clock.

**Work.**

- Use `tower::ServiceExt::oneshot` with `axum::body::Body` and `axum::http::Request` for every route.
  Assert status codes and decode bodies into `serde_json::Value`.
- Give each test its own temporary directory and the fixed clock from chapter 10. Never assert
  against wall-clock time.
- Cover the happy paths and the error paths: malformed JSON, blank description, invalid status,
  non-numeric ID (`/tasks/abc`), unknown ID, method mismatch, unknown route, and a corrupt store.
- Test the no-op rules explicitly: repeated identical PATCH and repeated mark leave `updatedAt`
  unchanged.
- Add a README table of routes with request and response examples, the error envelope, the chosen
  delete status, and the unknown-field policy. Add a curl smoke checklist that can be run from a
  scratch directory.
- Finish with the standard baseline: `cargo fmt --check`, `cargo clippy --all-targets -- -D
  warnings`, `cargo check`, `cargo test`, and a release build.

**Check commands.**

```bash
cargo test
cargo test -- --nocapture
cargo fmt --check && cargo clippy --all-targets -- -D warnings
cargo build --release
```

**Check question.** Which failure would your test suite catch that a single happy-path `curl` would
not?

## 12. Error and status matrix

| Situation | Status | Where it is decided | What the client should see |
|---|---|---|---|
| Malformed JSON syntax or JSON that does not fit the DTO | `400` (chosen policy) | `JsonSyntaxError` / `JsonDataError`, mapped in `api/error.rs` | Envelope with a `bad_request` code |
| Unknown fields in a request | `400` if denied, ignored if allowed | serde attribute on the DTO | Either a clear rejection or documented tolerance |
| Invalid status string or filter token | `400` | Status enum deserialization / query DTO | Envelope naming the valid values |
| Non-numeric `id` | `400` | `Path<u64>` rejection, mapped to your envelope | Envelope, not a panic or a `404` |
| Unknown but numeric ID | `404` | Domain/store lookup → `ApiError::NotFound` | Envelope naming the ID |
| Corrupt `tasks.json` | `500` | Store error mapped to internal failure | Generic safe message; detail logged server-side |
| Known path, wrong method | `405` | Axum's method router | Optionally remapped to the envelope with an `Allow` header |
| Unknown route | `404` | No matching route or `Router::fallback` | Envelope if you supply a fallback |
| Successful create | `201` | Create handler | Body plus `Location: /tasks/{id}` |
| Successful delete | `204` or `200` | Your documented contract | No body, or one confirmation body |

Axum's unmodified defaults are `400` for `JsonSyntaxError`, `422` for `JsonDataError` (valid JSON
that does not fit the DTO), and `415` for a missing `Content-Type`. The matrix above chooses `400`
for the first two; map those rejections deliberately and test the choice so every route agrees.

The distinction between `400` and `404` is a contract, not a detail. A client can retry a `500` and
fix a `400`; it must stop asking for a `404` ID. Make each code mean one thing everywhere.

## 13. Reuse or replace the hand-rolled JSON codec

You have two valid choices; make one deliberately.

**Option A — keep it.** The file format stays exactly as chapter 11 produced it, read and written by
your own codec. `api/dto.rs` uses `serde_json` only for the HTTP boundary and converts between
wire DTOs and domain `Task` values. The CLI and API read the same bytes with the same code. No
migration, no risk to existing `tasks.json` files. The cost is maintaining two serializers: one for
the file, one for HTTP.

**Option B — switch file storage to `serde_json`.** Derive `Serialize`/`Deserialize` on a
*file-format* struct (not necessarily on the domain `Task`), keep the same keys and timestamp
strings, and delete the hand-rolled codec if nothing else uses it. The cost is a compatibility test:
old files must still load, and the CLI must still produce byte-identical output where your README
promises it.

**Option C — hybrid.** Keep the codec for now, and note the switch as a future exercise. Perfectly
reasonable; just say so in the README.

> **Professional habit.** "We added an API" is not a justification for rewriting the store. If the
> store's behavior is already correct and tested, let it serve both front ends.

## Professional habits

- After each phase: `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings`, `cargo check`,
  `cargo test`, then a scratch-directory curl check.
- Commit one behavior per commit: "Add health route", "Add create endpoint", "Map 400 to envelope".
- Update the README in the same commit as the behavior it describes.
- Keep the dependency list reviewed with `cargo tree`; every crate needs a one-line reason in the
  README.
- Continue the build journal. Record the extractor rejection or lock error that cost you time; it
  will save the next session.
- Keep `cargo doc --open --package axum`, `rustup doc --std`, `rustup doc --cargo`, and
  `rustc --explain E....` as your references. No web lookup is needed.

## Common mistakes

- Rewriting `task`, `store`, or `time` before the health route works.
- Duplicating validation rules in DTOs and CLI parsing instead of moving shared rules into the
  domain layer.
- Letting invalid status tokens produce an empty list instead of `400`.
- Returning serde's default rejection text from some routes and your JSON envelope from others.
- Using `Json<T>` before other body-consuming extractors, or trying to read the body twice.
- Holding a mutex guard across `.await`, or unwrapping a poisoned lock.
- Writing to `tasks.json` from a `GET` handler, or testing from the repository directory so stale
  state hides the bug.
- Forgetting that `Location`, `Content-Length`, and `Content-Type` are part of the API contract.
- Adding tower-http, tracing, a database, or authentication before the nine core routes are tested.
- Treating the hand-rolled JSON codec as technical debt that *must* be paid now. It is a choice.

## Apply it to Task Tracker

1. Which existing module will own the "blank description is invalid" rule after this track?
2. If the CLI and API disagree about ID reuse after a delete, where is the single rule that should
   decide it?
3. What does `GET /tasks/abc` exercise that `GET /tasks/999` does not?
4. How will a fixed clock reach a handler without turning into a global variable?
5. Which phase would you stop at if you had only one hour, and what would remain unverified?
6. If you keep the manual JSON codec, what test proves the CLI still reads API-written files?

## Checkpoint

Explain the difference between a transport concern and a domain concern using one example from the
route table. Then explain why Phase 1 exists before Phase 3. If your answer involves "because the
tutorial said so", revisit `15-axum-api.md` section 1 and this chapter's section 1.

## Final API definition of done

- [ ] The CLI from chapters 00–13 still passes every check from `LEARNING_PATH.md`.
- [ ] The API entry point starts a server on a documented address; `/healthz` returns `200`.
- [ ] All nine routes in section 2 behave as specified, including both mark actions.
- [ ] Create returns `201` with `Location`; delete returns the single documented status.
- [ ] Every error, including mapped extractor rejections, uses one JSON error envelope.
- [ ] Malformed JSON, unknown fields, invalid status, bad IDs, unknown IDs, and corrupt files produce
      the codes in section 12.
- [ ] No-op update and no-op mark leave `updatedAt` and the file unchanged.
- [ ] Concurrent mutations cannot silently overwrite each other; writes are atomic.
- [ ] Automated in-process tests use a temporary directory and fixed clock and need no network
      port; curl smoke checks are a separate live-server step.
- [ ] A scratch-directory curl checklist passes with the release binary.
- [ ] README documents routes, DTO policy, error envelope, delete choice, entry-point choice, and
      the JSON-codec decision.
- [ ] `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings`, `cargo check`, and
      `cargo test` all pass.
- [ ] You can explain every module's responsibility and every status code without notes.

## Next

You are done with the documented Rust path. Revisit `LEARNING_PATH.md` for the full checklist.
