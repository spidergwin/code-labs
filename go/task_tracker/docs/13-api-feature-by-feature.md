# 13 — API feature by feature

> **Prerequisite.** Your CLI core from chapters `00`–`11` is finished and tested: the model,
> service, JSON store, validation, ID policy, and timestamp rules all work. This phase adds a
> **second binary**, not a rewrite. If a CLI feature is still shaky, finish it first; the API
> will amplify that uncertainty into two transports with the same bug.
>
> **Boundary.** There is no finished implementation in this chapter. You will get a route map,
> eight phases, checklists, and questions. You write the handlers, DTOs, tests, and `main.go`.

## Why this matters

"A simple HTTP API" sounds like one task. In practice it is routing, serialization, validation,
error translation, concurrency, testing, and documentation — each with its own failure mode.
Doing all of it at once produces a program that runs on the happy path and is painful to change.

The CLI was built phase by phase in `08-feature-by-feature.md` for exactly that reason. Use the
same rhythm here: one phase, tests and manual checks, a checkpoint question, a commit. Each phase
must leave the previous ones working, and the CLI must keep passing its own tests throughout.

## What you will learn

- Where the API binary and transport package fit in the existing layout.
- How to add chi as a dependency using only local tooling and documentation.
- A REST-ish route map that mirrors every CLI command.
- Eight ordered phases with concrete files to touch, tests to write, and questions to answer.
- A status/error matrix that covers malformed input, unknown resources, and server failures.
- How to smoke-test with `curl` without accidentally copying a solution from these notes.
- When to share state between CLI and API, and how a future SQLite store would fit.
- A final definition-of-done checklist for the whole project.

---

## 1. Target architecture

The layout from `07-architecture-and-project-layout.md` already reserved the slots. Now the
placeholders become real files:

```text
go/task_tracker/
├── cmd/
│   ├── cli/main.go                  # unchanged: existing binary
│   └── api/main.go                  # NEW: thin process entry for the server
└── internal/
    ├── task/                        # unchanged: model, service, repository contract
    ├── storage/
    │   └── jsonstore/               # shared: same tasks.json, same atomic writes
    └── transport/
        ├── cli/                     # unchanged
        └── api/                     # now used
            ├── router.go            # chi router and route registration
            ├── handlers.go          # thin HTTP handlers
            ├── dto.go               # request/response structs and conversion
            ├── errors.go            # domain error → status mapping, JSON error writer
            └── middleware.go        # your own small middleware, if any
```

| Location | Responsibility | Must not do |
|---|---|---|
| `cmd/api/main.go` | Read process config (address, file path), construct store/service/router, run `http.Server`, handle shutdown | Decode JSON, decide statuses, know task rules |
| `internal/transport/api` | Routes, DTOs, decode/encode, error mapping, middleware | Change business rules or write files directly |
| `internal/task` | Validation, IDs, statuses, timestamps, transitions | Know about HTTP methods or status codes |
| `internal/storage/jsonstore` | Read and atomically replace `tasks.json` | Know about HTTP or CLI argument parsing |

Dependency direction stays the same: `cmd/api` wires concrete pieces together; `transport/api`
calls the service; both binaries call the same store. If you find yourself importing
`transport/api` from `internal/task`, stop — that is a cycle and a design error.

> **Professional habit.** Keep `main.go` short enough to read in one breath. Anything in it that
> is not process setup or shutdown is probably a transport or service concern.

---

## 2. Adding the dependency

```bash
go get github.com/go-chi/chi/v5
go doc github.com/go-chi/chi/v5
go doc github.com/go-chi/chi/v5/middleware
go help modules
```

`go get` records the chosen version in `go.mod` and places source in your local module cache
(`go env GOMODCACHE`). This path uses chi **v5.5.0**; its `go.mod` requires **Go 1.24+**, and your
toolchain is 1.27.3, so there is no version problem. Always import it as
`github.com/go-chi/chi/v5` — the `/v5` is a permanent part of the module path. Read the cached
source and `go doc` output instead of searching the web; that is a complete and offline reference.

Why chi and not a larger framework? You need path parameters, method-based routes, groups, and
middleware. chi provides those as a thin layer over `http.Handler`, so your handlers remain
ordinary `func(w http.ResponseWriter, r *http.Request)` functions and `net/http` stays visible.

**No SQLite in this phase.** The Go standard library has no SQL database driver; using SQLite
would add another external dependency plus schema and migration work. Keep `tasks.json` and the
same `jsonstore` package both binaries already rely on.

---

## 3. Route map

These routes are **REST-ish**, not dogmatically REST: the two status transitions are action
routes because the CLI exposes them as actions. The table is a design map, not code to copy.

| CLI equivalent | Method and path | Purpose | Success status | Notes |
|---|---|---|---|---|
| `add "<description>"` | `POST /tasks` | Create one task | `201 Created` | `Location: /tasks/{id}`; client sends only the description |
| `list` | `GET /tasks` | List all tasks | `200 OK` | Order inherited from the service (recommend ascending ID) |
| `list todo` / `in-progress` / `done` | `GET /tasks?status=todo` | List a status subset | `200 OK` | Unknown filter value → `400` |
| *(no direct CLI equivalent)* | `GET /tasks/{id}` | Read one task | `200 OK` | Unknown ID → `404` |
| `update <id> "<description>"` | `PATCH /tasks/{id}` | Replace the description | `200 OK` | Response body may contain the updated task |
| `mark-in-progress <id>` | `POST /tasks/{id}/mark-in-progress` | Set status to `in-progress` | `200 OK` | Action route; document repeat behavior |
| `mark-done <id>` | `POST /tasks/{id}/mark-done` | Set status to `done` | `200 OK` | Action route; `updatedAt` rules mirror the CLI |
| `delete <id>` | `DELETE /tasks/{id}` | Remove one task | `204 No Content` | No body; unknown ID → `404` |
| *(optional)* | `GET /healthz` | Liveness check | `200 OK` | Small JSON body; no storage access needed |

Design questions to settle before you code:

- Will you also allow `PATCH /tasks/{id}` to change status? If yes, decide which route is
  canonical; two ways to do the same thing means two sets of tests.
- Will `POST /tasks/{id}/mark-done` on an already-done task be idempotent with no `updatedAt`
  change? Reuse the rule you documented for the CLI in `08-feature-by-feature.md`.
- Will the ID come from `{id:[0-9]+}` and produce `404` for `abc`, or will a looser pattern let
  the handler return `400`? Pick one and write it into the error matrix.

---

## 4. Decisions to make before Phase 1

Write these down in your build journal, then follow them consistently:

| Decision | Choices to consider |
|---|---|
| Listen address | `localhost:8080` for development; avoid a fixed privileged port |
| Data file location | Current working directory (matching the CLI) or an explicit flag/env value |
| Body limit | A small constant such as 1 MiB, documented in the README |
| Non-numeric ID | `400` (you parsed it and rejected it) or `404` (not a valid resource path) |
| Delete semantics | `404` for unknown ID; `204` with an empty body for success |
| Error envelope | One JSON shape for every failure, including `404` and `405` |
| Logging | Request ID in the response and in server logs; no stack traces to clients |
| Timeouts | At least `ReadHeaderTimeout`; sensible `Read`, `Write`, and `Idle` values |

Run both binaries from the same scratch directory during manual tests. That is the simplest way
to verify that they share `tasks.json` — and the easiest way to see what happens when they do not.

---

## 5. The eight phases

Each phase follows the same shape as the CLI work: **goal → files → what to do → what to test →
checkpoint**. Do not begin the next phase while a previous checkpoint is fuzzy.

### Phase 1 — Server skeleton, health route, graceful shutdown

**Goal.** A binary starts, listens, answers one health request with JSON, and stops cleanly on a
shutdown signal. No task behavior yet.

**Files to touch.** `cmd/api/main.go`; a minimal `internal/transport/api/router.go` containing
only the health route; `README.md` sketch. Leave the task service untouched.

**What to do.** Build an `http.Server` with `Addr`, `Handler`, and the four timeouts from chapter
`12`. Decide how a SIGINT/SIGTERM triggers `srv.Shutdown(ctx)` with a bounded context. Return a
small generic health value; set `Content-Type` before writing. Keep `main.go` limited to wiring
and process lifecycle.

**What to test.** A `httptest.NewRecorder` call to the health handler: status `200`, JSON content
type, decoded body. Manually start the binary, hit the health endpoint with `curl`, then stop it
and confirm a clean exit with no "in use" warnings on a quick restart.

**Checkpoint.** Why does `ListenAndServe` block, and what does `Shutdown` do differently from
`Close`?

### Phase 2 — Router package, middleware stack, JSON error writer

**Goal.** All cross-cutting HTTP behavior exists before any task route does.

**Files to touch.** `internal/transport/api/router.go`, `middleware.go`, `errors.go`; wire the
router into `cmd/api/main.go` in place of the single health handler.

**What to do.** Create the chi router, register middleware **before** routes (`RequestID`,
`Logger`, `Recoverer`, and optionally `Timeout`), and define `NotFound` and `MethodNotAllowed`
handlers that use your JSON error writer. Write one error-mapping function from domain sentinels
to statuses and one error-writing function that emits the envelope with the request ID. Reuse
chapter `12`'s guidance; do not add task-specific logic here.

**What to test.** Unknown path → JSON `404`; known path with the wrong method → JSON `405` with
an `Allow` header; request ID header present and echoed in the error body; a probe handler that
panics produces `500` rather than killing the process; each domain sentinel maps to the expected
status in a table-driven test.

**Checkpoint.** Why must `r.Use` come before route registration, and why should `Recoverer` sit
near the outside of the chain?

### Phase 3 — DTOs and create task (`POST`)

**Goal.** A client can create a task over HTTP with the same validation the CLI uses.

**Files to touch.** `internal/transport/api/dto.go` and `handlers.go`; no changes to
`internal/task` unless you discover a genuinely missing domain rule.

**What to do.** Define a request DTO that accepts only what a client should control (the
description) and a response DTO for what clients receive. Decode with `MaxBytesReader` and
`DisallowUnknownFields`; convert DTO → domain call → DTO. Ignore or reject client-supplied `id`,
`status`, and timestamps — those belong to the service. On success, use `201 Created`, set
`Location`, and encode the created task. On validation failure, return `400` with a clear message.

**What to test.** Valid create gives `201`, a `Location` header, correct five-field response, and
one new task in `tasks.json`; empty or whitespace-only description gives `400`; malformed JSON,
wrong type, unknown field, and oversized body each give `400` (or your documented `413`); failed
requests leave the file unchanged; two sequential creates get your documented IDs.

**Checkpoint.** Why is the create DTO different from the domain `Task`, and who assigns `id`,
`status`, and timestamps?

### Phase 4 — List and get with filtering

**Goal.** Clients can read all tasks, a status subset, and a single task.

**Files to touch.** `handlers.go`, `dto.go` if a response wrapper helps; possibly a read-only
helper in `internal/task` if filtering logic is not already reusable from the CLI work.

**What to do.** Implement `GET /tasks`, `GET /tasks?status=...`, and `GET /tasks/{id}` against the
same service. Validate the query value against the three known statuses before calling the
service. Reuse the service's deterministic ordering rather than sorting again in the handler.
Return `200` with a JSON array for lists and one object for a single task; unknown ID → `404`.

**What to test.** Empty store → `200` with `[]`, not `null` or an error; every status filter
returns exactly the matching tasks; `status=bogus` and an unexpected query parameter produce the
documented `400`; `GET /tasks/abc` follows your non-numeric-ID rule; `GET /tasks/999` → `404`;
no read changes `tasks.json`.

**Checkpoint.** Is filtering a service rule or a transport convenience in your design, and why
must reads never rewrite the file?

### Phase 5 — Update and delete

**Goal.** Clients can change a description and remove a task, with clear success and failure
semantics.

**Files to touch.** `handlers.go`, `dto.go` if you add an update request type; `errors.go` if a
new mapping appears.

**What to do.** `PATCH /tasks/{id}` accepts a new description, calls the service, returns `200`
with the updated task. `DELETE /tasks/{id}` calls the service and returns `204` with **no body**.
Map unknown IDs to `404` in both. Failures write nothing. Keep the `updatedAt` behavior identical
to the CLI's `update`.

**What to test.** Only the target's description and `updatedAt` change; `200` has a JSON body
and `204` has none; unknown ID → `404` for both routes; empty description → `400`; deleting the
last task leaves a valid `[]`; a failed update leaves the file byte-for-byte unchanged.

**Checkpoint.** Why does `204` forbid a body, and why is "not found" an explicit `404` instead of
a silent no-op?

### Phase 6 — Status transitions and `updatedAt`

**Goal.** The two action routes mirror `mark-in-progress` and `mark-done`, including your
idempotency rule.

**Files to touch.** `handlers.go`; the service only if a transition rule is genuinely missing;
`README.md` for the documented behavior.

**What to do.** Add `POST /tasks/{id}/mark-in-progress` and `POST /tasks/{id}/mark-done`. Call the
existing service transitions rather than assigning status strings in the handler. Decide and
document whether repeating the same status changes `updatedAt`; the CLI's rule from chapter `08`
is the natural reference. Use `200` with the updated task, or `204` if you prefer no body — but
be consistent with your earlier choices.

**What to test.** Each transition changes only status and `updatedAt`; the three list filters
reflect the new status; repeated transitions follow the documented rule; unknown ID → `404`;
`createdAt` never changes; concurrency-independent assertions (compare parsed values, not
formatted strings).

**Checkpoint.** Why is an action route a reasonable alternative to sending a status field in
`PATCH`, and what prevents a typo like `donee` from entering the file?

### Phase 7 — Concurrency hardening and atomic writes

**Goal.** Two clients can use the API at once without lost updates or a corrupt file.

**Files to touch.** `internal/storage/jsonstore/store.go` (locking and temp-file details),
possibly `internal/task/service.go` if the read-modify-write sequence needs to be one critical
section; new concurrency tests.

**What to do.** Add a `sync.RWMutex` wherever the shared task slice lives; protect ID assignment
and save as one operation if correctness requires it. Return copies from read paths. Keep the
existing temp-file-plus-rename write strategy, with unique temp names in the same directory.
Consider holding the write lock only as long as needed, but never sacrifice correctness for a
hypothetical speed gain.

**What to test.** `go test -race ./...` with parallel handler tests; two simultaneous creates
produce two distinct IDs and two persisted tasks; concurrent reads see either the old or the new
complete snapshot, never a partial one; a failed write leaves the previous file intact.

**Checkpoint.** Who owns the mutex, and which exact lines must be inside the locked section for
ID generation to be safe?

### Phase 8 — Integration tests, curl smoke script, README

**Goal.** Prove the whole binary works, then document it for a future reader.

**Files to touch.** New `*_test.go` files in `internal/transport/api`, a scratch curl script you
keep outside the repository or as a clearly named development helper, and `README.md`.

**What to do.** Use `httptest.NewServer` with your real router and a temporary data directory for
end-to-end tests: create, list, filter, get, patch, transition, delete, plus error cases. Write a
short smoke script that starts the binary and checks the health and create paths. Extend the
README with build/run commands, the route table, example requests and responses, the status
matrix, the body limit, the ID and `updatedAt` rules, and how to run tests.

**What to test.** A clean rebuild passes `gofmt -l .`, `go vet ./...`, `go test ./...`, and
`go test -race ./...`; the smoke script passes in a fresh scratch directory; both binaries still
pass their own tests; README examples match observed output.

**Checkpoint.** Which single automated test would catch the most regressions in this API, and
why?

---

## 6. Error and status matrix

Keep this table beside you while writing handlers. Your implementation should be able to produce
each row deliberately.

| Situation | Status | Rationale |
|---|---|---|
| Invalid or truncated JSON body | `400 Bad Request` | Client can fix the syntax |
| Valid JSON with unknown field or wrong type | `400 Bad Request` | Boundary validation rejected it |
| Empty description or unknown `status` filter | `400 Bad Request` | Input violates a documented rule |
| Non-numeric `id` in the path | `400` **or** `404` | `400`: the value was parsed and rejected; `404`: with `{id:[0-9]+}` the path matches no resource. Pick one, document it, test it |
| Unknown numeric ID | `404 Not Found` | The requested resource does not exist |
| Known path with an unsupported method | `405 Method Not Allowed` | Resource exists; verb is wrong. Send `Allow` |
| No route matches the path | `404 Not Found` | Nothing to dispatch to |
| Conflict, such as an illegal transition | `409 Conflict` | Request understood, state disagrees |
| Corrupt `tasks.json` or failed atomic rename | `500 Internal Server Error` | Server-side failure; log with request ID |
| Panic in a handler | `500` via `Recoverer` | Process survives; client gets a generic message |

Two extra rules matter as much as the codes: write the response **once**, and never send stack
traces, absolute paths, or internal field names to the client. Errors are data at the API
boundary, not a printing mechanism.

---

## 7. curl smoke examples

These examples target a **generic notes API** so they illustrate the requests without giving
away your Task Tracker handlers. Adapt the base address to whatever you chose in Phase 1.

Health check:

```bash
curl -i localhost:8080/healthz
```

Create a note:

```bash
curl -i -X POST localhost:8080/notes \
  -H 'Content-Type: application/json' \
  -d '{"text": "Buy milk"}'
```

List with a filter:

```bash
curl -i 'localhost:8080/notes?status=todo'
```

Bad JSON:

```bash
curl -i -X POST localhost:8080/notes \
  -H 'Content-Type: application/json' \
  -d '{"text": '
```

For each one, predict the status, headers, and body shape **before** running it. Then compare.
`-i` includes headers, which is how you check `Content-Type`, `Location`, and `Allow`. Keep a
scratch script of these checks, but never point it at real data.

---

## 8. Share or split? design notes

| Question | Guidance |
|---|---|
| Same `tasks.json`? | Yes. Both transports call the same store; that is the whole reason the layering pays off. |
| Current-directory behavior? | If both binaries resolve `tasks.json` relative to the process's cwd, running them from different directories produces different data. Decide whether that is acceptable or pass an explicit path. Write it down. |
| Can CLI and API run simultaneously? | Only with a lock shared by **both processes**. An in-process mutex protects one binary; two processes can still overwrite each other's writes. An explicit decision here prevents a confusing bug later. |
| Lock scope | In-process: protect the in-memory slice and the read-modify-write sequence. Cross-process: you would need file locking or a single-writer design; do not pretend a Go mutex solves it. |
| One service instance or two? | Two binaries mean two service instances even with one file. Keep rules in `internal/task` so both call identical code. |
| Migration path to SQLite | Later, add a new package under `internal/storage`, satisfy the same small repository interface, and change wiring in both `main.go` files. Handlers and service should not change. This is why no SQL driver is needed now. |
| Placeholder packages | Use `internal/transport/api` and `cmd/api`. Leave `internal/storage/sqlitestore` empty until there is a real reason and a real driver. |

> **Professional habit.** Write the "two processes, one file" decision in the README, even if the
> decision is "use one at a time". Undocumented assumptions become bugs when you forget them.

---

## Common mistakes

| Mistake | Consequence | Habit |
|---|---|---|
| Rewriting the CLI rules inside handlers | Two sources of truth; CLI and API drift | Call the service; add tests that compare behavior |
| Returning `200` for every outcome | Clients cannot distinguish failure | Follow the status matrix |
| Sending client-supplied `id` or timestamps straight through | Broken IDs and inconsistent time rules | Accept only intended fields in the DTO |
| Forgetting `Location` on `201` | Clients must guess the new resource path | Set it in the create handler |
| Writing a body with `204` | Confusing clients and broken HTTP semantics | Return no body |
| Reading the file without a lock while a write runs | Data races and torn snapshots | `RWMutex` and defensive copies |
| Assuming a mutex protects two processes | Silent lost updates | Decide and document the concurrency policy |
| Testing only against a real running server | Slow, brittle tests dependent on ports | `httptest` for tests, curl for smoke checks |
| Leaving `tasks.json` in the repository root | Stray runtime data and accidental commits | Work in a scratch directory |
| Documenting routes after "it works" | README drifts from behavior | Update it in the same phase |
| Declaring victory after happy paths only | Edge failures appear later | Tick the matrix and definition of done |

---

## Definition of done — API

- [ ] `go build ./cmd/cli ./cmd/api` succeeds; `gofmt -l .` and `go vet ./...` are clean.
- [ ] `go test ./...` and `go test -race ./...` pass from a clean checkout.
- [ ] The health endpoint returns JSON with the right content type.
- [ ] Every CLI command has a working HTTP equivalent from the route map.
- [ ] Created, updated, transitioned, and deleted tasks match CLI behavior field for field.
- [ ] `201` includes `Location`; `204` has no body; all errors use the JSON envelope.
- [ ] Every row of the error/status matrix has a test or a documented manual check.
- [ ] `tasks.json` stays valid after failures, crashes simulated by tests, and repeated operations.
- [ ] Two concurrent creates produce distinct IDs under the race detector.
- [ ] README documents routes, payload shapes, statuses, storage location, and concurrency policy.
- [ ] curl smoke script passes in a fresh scratch directory with no real data.
- [ ] You can explain which layer owns each rule without looking at your notes.

---

## Apply it to Task Tracker

Work through these prompts after the phases, in order:

1. Draw the dependency arrows for both binaries on one sheet. Circle any arrow that points from
   `internal/task` toward either transport; if you find one, fix it.
2. Compare the route map with your actual routes. List any route you added and justify it in one
   sentence.
3. Pick three error cases and trace each from the HTTP request through decode, service call, error
   wrapping, status mapping, and response body. Name the file responsible at each step.
4. Find every place that constructs a task. Confirm that `id`, `createdAt`, and `updatedAt` can
   only be set in one layer.
5. Write down what happens if the CLI and API are both running and both write at the same time.
   If the answer surprises you, revisit the share-or-split table.
6. For each phase, name the test you would add *first* if that feature regressed next month.
7. Re-run the CLI's full manual script from chapter `08` after the API work. Any difference in
   `tasks.json` is a bug in the new code.

---

## Checkpoint

1. Which files are new, and which existing packages remain unchanged?
2. Why add chi instead of writing manual path parsing with `strings.Split`?
3. Which CLI command maps to `PATCH /tasks/{id}`, and which to `POST /tasks/{id}/mark-done`?
4. What status and headers belong on a successful create?
5. How do you decide between `400` and `404` for a non-numeric ID?
6. Where does the mutex go, and what must happen while it is held?
7. What does `go test -race` tell you that ordinary tests do not?
8. Why is an in-process mutex insufficient when two binaries run at once?
9. Which phase would you repeat first if the README no longer matched the code?
10. What would have to change in the handlers if you later replaced `jsonstore` with a SQL store?

## Next

You are done with the documented Go path. Revisit `LEARNING_PATH.md` for the full checklist.
