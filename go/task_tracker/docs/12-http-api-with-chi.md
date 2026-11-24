# 12 — HTTP API with net/http and chi

> **Scope note.** Chapters `00`–`11` used only the standard library. This chapter keeps that
> discipline for the HTTP *model* (`net/http`) and adds one small external library for *routing*:
> `github.com/go-chi/chi/v5`. The chi release used in this path is **v5.5.0**, which requires
> **Go 1.24+**; your toolchain satisfies it. The import path never changes with the version: it
> is always `github.com/go-chi/chi/v5`. After `go get`, use `go doc` and the local module cache —
> no web.
>
> **Boundary.** Examples below are short and generic. This chapter contains no Task Tracker API
> implementation; the next chapter gives you a build plan, and you write the code.

## Why this matters

The CLI from chapters `00`–`11` is one way to *transport* commands into your task service: a
human types words, the CLI parses them, the service changes `tasks.json`. An HTTP API is a
**second transport** over the same core. Validation, IDs, timestamps, status words, and file
storage do not change when a request arrives over a network instead of a terminal.

```text
   terminal ────►  transport/cli ──┐
   HTTP client ─►  transport/api ──┤  args/JSON → service calls
                                   ▼
                            internal/task      validation, IDs, statuses, timestamps
                                   ▼
                         storage/jsonstore ──► tasks.json on disk
```

| Shared with the CLI | New with the API |
|---|---|
| `internal/task` model, service, and rules | HTTP methods, paths, status codes |
| `internal/storage/jsonstore` and `tasks.json` | JSON request/response bodies (DTOs) |
| Validation rules and error sentinels | Routing, middleware, headers, request IDs |
| ID policy and `updatedAt` behavior | Concurrency, graceful shutdown, timeouts |

Duplicate business rules in handlers and you have two definitions of "valid status" and two
places to fix a bug. Treat the API as a transport and it inherits behavior you already tested.

## What you will learn

- HTTP/JSON first principles: request/response, method, path, query, headers, body, status, and
  why DTOs differ from domain models.
- Go's `net/http` model: `Request`, `ResponseWriter`, `Handler`, `ServeMux`, `http.Server`.
- Handler tests with `httptest.NewRecorder` and `httptest.NewServer`.
- chi routing: `NewRouter`, `{id}` parameters, groups, and middleware.
- Safe body decoding and writing exactly one well-formed response.
- Domain error → status code mapping without leaking internals.
- Why concurrency changes your obligations around the shared JSON store.

---

## 1. HTTP from first principles

A **client** sends a **request**; a **server** replies with a **response**; then the exchange is
finished. The server does not remember you between requests unless you deliberately store state.
That is **statelessness**: every request carries what it needs — resource ID in the path, filters
in the query, data in the body. No "current task" variable is required.

> **Jargon first.** An *endpoint* is a method-plus-path combination such as `GET /tasks`. A
> *resource* is what a path names (`/tasks/3` names task 3). A *representation* is the format used
> to describe it — here, JSON.

```text
POST /tasks?verbose=true
Host: localhost:8080
Content-Type: application/json
X-Request-Id: 7f3c9a1e

{"text": "Buy groceries"}
```

| Part | Example | Meaning |
|---|---|---|
| Method | `POST` | The verb: what the client wants to do |
| Path | `/tasks` | Which resource or collection is targeted |
| Query | `?verbose=true` | Optional parameters that do not change resource identity |
| Headers | `Content-Type`, `X-Request-Id` | Metadata: format, tracing, client info |
| Body | JSON text | Data sent with the request, common for `POST` and `PATCH` |

`Content-Type` describes the bytes in the body; it is not the data itself. `GET` requests
normally have no body; filtering belongs in the query string.

**Idempotency** means repeating an operation leaves the same final state — it describes the
effect, not the response text.

| Method | Typical meaning | Idempotent? | Notes |
|---|---|---|---|
| `GET` | Read a resource | Yes | Should not change stored data |
| `POST` | Create or trigger an action | No (usually) | Two creates can make two records |
| `PATCH` | Partially update | Usually in practice | Depends on your rules; document it |
| `PUT` | Replace wholly | Yes | Not required here |
| `DELETE` | Remove | Yes | Deleting twice must not corrupt anything |

`POST /tasks/3/mark-done` is an **action route**: it changes status without requiring the client
to know the whole wire representation. `PATCH /tasks/3` with a status field is the alternative;
pick one per operation and stay consistent.

The status code tells the client the outcome before it parses a body:

| Range | Meaning | Examples |
|---|---|---|
| `2xx` | Success | `200 OK`, `201 Created`, `204 No Content` |
| `4xx` | Client's request was wrong | `400`, `404`, `405`, `409` |
| `5xx` | Server failed | `500 Internal Server Error` |

Use `201 Created` with a `Location` header for creates, and `204 No Content` with no body for
deletes. A `4xx` is not an error in your program's control flow — it is a *valid answer* to an
invalid request. JSON is text serialized by the client and parsed by the server (and back), so
set `Content-Type: application/json` on responses and treat all incoming JSON as untrusted.

---

## 2. Go's `net/http` model

Read along with `go doc net/http`, `go doc net/http.Request`, `go doc net/http.ResponseWriter`.

| Type | Role |
|---|---|
| `http.Request` | Method, URL, headers, body, context of the incoming request |
| `http.ResponseWriter` | Set headers and status, write response bytes |
| `http.Handler` | Anything with `ServeHTTP(ResponseWriter, *Request)` |
| `http.HandlerFunc` | Adapter letting an ordinary function act as a `Handler` |

The core interface is tiny:

```go
type Handler interface {
    ServeHTTP(ResponseWriter, *Request)
}
```

`HandlerFunc` has a `ServeHTTP` method calling the underlying function, which is why
`func(w http.ResponseWriter, r *http.Request)` works anywhere a handler is expected.

A tiny generic handler — a `Health` value, not a Task Tracker type:

```go
type Health struct {
    Status string `json:"status"`
}

func health(w http.ResponseWriter, r *http.Request) {
    w.Header().Set("Content-Type", "application/json")
    w.WriteHeader(http.StatusOK)
    _ = json.NewEncoder(w).Encode(Health{Status: "ok"})
}
```

Set headers **before** `WriteHeader` or `Write`: the first write freezes the status. `Encode`
returns an error, but the handler cannot pass it anywhere once bytes are written — log it or
ignore it deliberately and say why in a comment.

A tiny test with `httptest.NewRecorder`:

```go
func TestHealth(t *testing.T) {
    rec := httptest.NewRecorder()
    health(rec, httptest.NewRequest(http.MethodGet, "/healthz", nil))
    if rec.Code != http.StatusOK {
        t.Fatalf("status = %d, want 200", rec.Code)
    }
    var got Health
    if err := json.Unmarshal(rec.Body.Bytes(), &got); err != nil {
        t.Fatalf("decode response: %v", err)
    }
    if got.Status != "ok" {
        t.Errorf("status = %q, want %q", got.Status, "ok")
    }
}
```

`NewRecorder` implements `ResponseWriter` and records `Code`, `HeaderMap`, and `Body` in memory:
no sockets, ports, or permissions. `httptest.NewRequest` builds a suitable request. See
`go doc net/http/httptest`.

A **mux** chooses a handler from method and path. The standard library's `http.ServeMux` supports
method and wildcard patterns since Go 1.22 (for example `GET /tasks/{id}`); chi adds expressive
groups, constrained parameters, and middleware — not capabilities `net/http` lacks.

```go
srv := &http.Server{
    Addr:              "localhost:8080",
    Handler:           chi.NewRouter(), // or http.NewServeMux()
    ReadHeaderTimeout: 5 * time.Second,
    ReadTimeout:       10 * time.Second,
    WriteTimeout:      10 * time.Second,
    IdleTimeout:       60 * time.Second,
}
if err := srv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
    log.Fatal(err)
}
```

| Field | Why it exists |
|---|---|
| `Addr` | Address and port to listen on |
| `Handler` | Root handler; `nil` means `http.DefaultServeMux` |
| `ReadHeaderTimeout` | Limits time reading headers; a slow-client defense |
| `ReadTimeout` / `WriteTimeout` | Limits for reading the whole request / writing a response |
| `IdleTimeout` | How long an idle keep-alive connection stays open |

`ListenAndServe` **blocks**: it listens and accepts connections until listening fails or the
server is shut down. That is what a long-running process wants. Notice the `errors.Is` check:
after a planned `Shutdown`, `ListenAndServe` returns `http.ErrServerClosed`, which is success,
not a crash. For a graceful stop, call `srv.Shutdown(ctx)` from a signal handler; it stops new
requests and lets in-flight ones finish within the context deadline. See
`go doc net/http.Server.Shutdown` and `go doc net/http.ErrServerClosed`.

> **Professional habit.** Always set `ReadHeaderTimeout`. Its zero value means no timeout, which
> exposes the process to a client that never finishes its headers. Other timeouts are decisions,
> not magic constants.

Manual path parsing with `strings.Split(r.URL.Path, "/")` looks simple until you handle trailing
slashes, extra segments, percent-encoding, shared prefixes, `tasks/abc` versus `tasks/3`, and
`tasks/3/mark-done` versus `tasks/3`. Every case becomes a custom `if` plus more tests. A router
expresses the rules as patterns and hands you the extracted values.

---

## 3. chi: routing without a framework

```bash
go get github.com/go-chi/chi/v5
go doc github.com/go-chi/chi/v5
go doc github.com/go-chi/chi/v5/middleware
```

`go get` records the version in `go.mod` and downloads source into the cache reported by
`go env GOMODCACHE`. The `/v5` suffix is the Go module major-version convention (`go help modules`);
it stays in the import path. Read the cached source offline.

```go
router := chi.NewRouter()
router.Get("/healthz", health)
router.Post("/notes", createNote)
router.Patch("/notes/{id:[0-9]+}", updateNote)
router.Delete("/notes/{id:[0-9]+}", deleteNote)
```

Inside one of those handlers, `chi.URLParam` reads the captured value from the request:

```go
func updateNote(w http.ResponseWriter, req *http.Request) {
    id := chi.URLParam(req, "id") // string; convert with strconv.Atoi
    _ = id
}
```

`chi.URLParam` reads a named parameter from the request context. `{id:[0-9]+}` adds a regex
constraint: `notes/abc` does not match. Decide whether a non-numeric ID should be `400` (matched,
then rejected) or `404` (not a valid resource path); both are defensible if documented.

| Tool | What it does |
|---|---|
| `r.Get/Post/Patch/Delete` | Register a method-specific route |
| `r.Route(prefix, fn)` | Subrouter with a shared path prefix, e.g. `/tasks` |
| `r.Group(fn)` | Subrouter without a prefix; share middleware across routes |
| `r.With(mw...)` | Attach middleware to a single route |
| `r.Mount(prefix, handler)` | Attach a whole `http.Handler` (often another router) under a prefix |
| `r.Use(mw...)` | Add middleware to this router |
| `r.NotFound` | Custom handler for no matching path (JSON `404`) |
| `r.MethodNotAllowed` | Custom handler for wrong method (JSON `405`; set `Allow` yourself) |

Call `r.Use` **before** registering routes on that router. chi panics with
`chi: all middlewares must be defined before routes on a mux` if you call `Use` after a route exists.
Subrouters from `Route` or `Group` inherit parent middleware; `Mount` hands the whole request to
another handler, so decide which router owns each middleware. The default chi `405` handler sets
the `Allow` header; once you install a custom `MethodNotAllowed` handler, setting `Allow` is your
job.

Middleware is just a function wrapping a handler:

```go
type Middleware func(http.Handler) http.Handler

func annotate(next http.Handler) http.Handler {
    return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
        w.Header().Set("X-App", "task-tracker")
        next.ServeHTTP(w, r) // exactly once
    })
}
```

It may work before and after `next.ServeHTTP`. With `r.Use(a, b)`, `a` runs first and `b` is
inside it; put `Recoverer` near the outside so it catches panics beneath it.

Bundled middleware in `github.com/go-chi/chi/v5/middleware`:

| Middleware | Purpose |
|---|---|
| `RequestID` | Adds/propagates `X-Request-Id` and a context value; read with `middleware.GetReqID(r.Context())` |
| `Logger` | Logs method, path, status, duration |
| `Recoverer` | Turns a panic into `500` instead of killing the process |
| `Timeout(d)` | Cancels the request context after a duration |
| `RealIP` | Optional: derives client IP from proxy headers; trust only behind your proxy |

> **Professional habit.** Keep your own middleware generic. Middleware that knows what a "task"
> is has become a handler in disguise and belongs in the transport package as a testable function.

---

## 4. The JSON boundary: DTOs and careful decoding

A **DTO** (data transfer object) describes bytes on the wire. Keep it separate from your
`internal/task` model even when fields look identical:

```go
type Note struct {
    ID   int    `json:"id"`
    Text string `json:"text"`
}
```

Separate types let you rename a JSON field without renaming the domain field, omit internals, add
request-only fields, or change validation — without touching the service. Conversion between DTO
and domain value is explicit transport code and a good place to validate. Tags set JSON names:
`json:"-"` skips a field, `omitempty` hides zero values, and an untagged `ID` would encode as
`"ID"`.

```go
w.Header().Set("Content-Type", "application/json")
w.WriteHeader(http.StatusCreated)
if err := json.NewEncoder(w).Encode(note); err != nil {
    // Too late to change status: log the failure server-side.
}
```

`Encoder.Encode` writes JSON plus a newline; `json.Marshal` returns bytes. Either is fine; pick
one style (`go doc encoding/json.Encoder`, `go doc encoding/json.Decoder`).

Three defenses belong at the boundary:

1. **Limit the body.** `http.MaxBytesReader(w, r.Body, limit)` fails when the client sends more
   than `limit` bytes; otherwise you may allocate a huge value before rejecting it.
2. **Close the body.** `defer r.Body.Close()` documents ownership; the server also closes it
   after the handler returns.
3. **Reject unknown fields and trailing data.** `DisallowUnknownFields()` turns `"descripton"`
   into a `400` instead of a silently empty value. A second `Decode` should return `io.EOF`.

```go
r.Body = http.MaxBytesReader(w, r.Body, 1<<20) // 1 MiB, for example
defer r.Body.Close()
dec := json.NewDecoder(r.Body)
dec.DisallowUnknownFields()
var in Note
if err := dec.Decode(&in); err != nil {
    // malformed JSON, wrong type, or unknown field → 400 with a specific message
    return
}
```

| Failure | Decoder report | Response |
|---|---|---|
| Broken syntax (`{"text":`) | *unexpected end of JSON input* | `400` |
| Wrong type (`"id": "x"`) | *cannot unmarshal string into Go struct field...* | `400` |
| Unknown field (`"txt": "..."`) | *json: unknown field "txt"* | `400` |
| Too much data | error from the limited reader | `413` or `400`, documented |

These are different client mistakes: give different messages but keep one response shape. Do not
echo raw error strings that may contain internal paths or struct details.

---

## 5. Error mapping: domain errors to status codes

Your service already returns errors (`03-errors-and-validation.md`); the API translates them:

| Domain situation | Status | Rationale |
|---|---|---|
| Empty description, bad status value | `400 Bad Request` | Client can fix the input |
| Unknown ID | `404 Not Found` | Resource does not exist |
| Illegal transition, duplicate resource | `409 Conflict` | Request understood, state disagrees |
| Corrupt file, failed rename, I/O error | `500 Internal Server Error` | Client cannot fix it |
| Wrong method on a known path | `405 Method Not Allowed` | Resource exists; verb does not |
| No route at all | `404 Not Found` | Nothing matches the path |

Use `errors.Is(err, task.ErrNotFound)`, not string matching; wrapping with `%w` preserves it.
Keep mapping in **one** transport function so handlers behave alike. A generic envelope:

```go
type apiError struct {
    Error     string `json:"error"`
    RequestID string `json:"request_id,omitempty"`
}
```

| Rule | Reason |
|---|---|
| Write the response exactly once | First `WriteHeader`/`Write` freezes status and headers |
| Set `Content-Type` first | Headers cannot change afterward |
| Return immediately after writing | Prevent a second path appending bytes |
| Log details with the request ID | Client gets a useful message; you keep diagnostics |
| Never send stack traces, SQL, or absolute paths | They help attackers and confuse users |
| Keep 5xx bodies generic | `"internal server error"` plus a request ID is enough |

> **Professional habit.** Treat the error writer as one choke point: a new domain sentinel should
> add one line to the mapping, not edit every handler.

---

## 6. Concurrency changes your obligations

A CLI is sequential; an HTTP server handles requests in parallel. Everything that was "safe
because only one command ran at a time" must be reconsidered.

| Concern | Failure mode | Defense |
|---|---|---|
| Shared slice of tasks | Overlapping appends, data race | `sync.RWMutex`, or return copies |
| ID generation | Two requests choose the same ID | Assign inside the locked read-modify-write |
| File writes | Interleaved renames | Serialize writes with the same lock |
| Partial file | Crash mid-write leaves invalid JSON | Temp file then `os.Rename` (`10-professional-habits.md`) |
| Temp collisions | Two writers pick one name | `os.CreateTemp(dir, ".tasks-*.tmp")` in the **same directory** |
| Long lock hold | Readers block on slow I/O | Keep the critical section short when correctness allows |

`sync.RWMutex` (`go doc sync.RWMutex`) allows many readers or one exclusive writer. Readers must
still copy returned slices: a released lock does not protect a caller iterating shared memory.
Correctness first — if ID assignment and saving must be atomic as one operation, hold the write
lock across that sequence and accept serialization. Optimize only with measurements.

A context deadline from `Timeout` middleware matters only if service and storage calls check
`r.Context()`. A tiny local file may not need cancellation today, but passing the context through
keeps the door open.

> **Professional habit.** Run `go test -race ./...` once concurrency exists. The race detector
> finds overlaps ordinary tests miss; use it as a checkpoint, not on every keystroke.

---

## 7. Testing HTTP without a network

| Tool | Use it when |
|---|---|
| `httptest.NewRecorder` | Unit-testing one handler or middleware, in memory |
| `httptest.NewServer(handler)` | End-to-end test with a real local listener and client |
| `httptest.NewTestServer(t, handler)` | A test server tied to `t`; newer Go versions can use an in-memory network |
| Direct service tests | The rule has nothing to do with HTTP |

`NewRecorder` needs no cleanup. `NewServer` starts a real loopback listener and must be closed
with `defer srv.Close()` or `t.Cleanup(srv.Close)`; it still stays on your machine, not the wider
network. **Table-driven handler tests** define cases with name, method, path, body, expected
status, and body assertion; one loop covers happy path, unknown ID, malformed JSON, wrong method,
and unknown route. Use a temporary store so tests never touch real `tasks.json`.

**Middleware tests** wrap a tiny probe handler that records whether it ran, sets a header, or
panics. Assert the wrapper's effect (for example a request ID header) and that `next` ran once;
for `Recoverer`, make the probe panic and check for `500`.

---

## 8. Professional habits for HTTP work

- **Thin handlers.** Decode, validate, call the service, map the result to a status. Nothing else.
- **DTOs at the edge.** Domain types do not gain `json` tags just because an API exists.
- **Consistent statuses.** Write the matrix once; handlers follow it, not the reverse.
- **Small routers per package.** Build a router in `internal/transport/api`; register it once in
  `main`, not inside handlers.
- **Context and timeouts.** Honor `r.Context()` for long operations; set server timeouts.
- **One error writer.** Same envelope, request ID, and logging call everywhere.
- **curl smoke tests.** Tests prove rules; curl proves the process listens and speaks JSON.
- **README first.** Document address, routes, payloads, and statuses as you go.
- **Offline references.** `go doc net/http`, `go doc net/http/httptest`, `go doc encoding/json`,
  `go doc sync`, and the module cache after `go get` are enough.

---

## Common mistakes

| Mistake | Consequence | Habit |
|---|---|---|
| Business rules in handlers | CLI and API disagree | Keep the service authoritative |
| Headers after `WriteHeader` | Header silently missing | Set headers first |
| Two writes on one response | Corrupt body, ignored status | Return after writing |
| No `Content-Type` | Clients guess the format | Set it on every JSON response |
| Unlimited body decode | Memory blowup | `http.MaxBytesReader` |
| No `DisallowUnknownFields` | Typos become empty values | Enable at the boundary |
| Matching error strings | Mapping breaks on rewording | Sentinels and `errors.Is` |
| Shared slice without lock/copy | Races under parallel requests | `RWMutex` plus copies |
| In-place file writes | Corrupt `tasks.json` on crash | Atomic temp file and rename |
| No `ReadHeaderTimeout` | Slow-client exposure | Set all server timeouts |
| `r.Use` after routes | chi panics at setup | Register it first |
| Panicking for bad input | Process instability | Return errors, map to `4xx` |

---

## Apply it to Task Tracker

Answer on paper; do not implement yet.

1. Which packages stay untouched, and which new package owns handlers, DTOs, and error mapping?
2. Sketch two DTOs for one task: a create request and a response. Which domain fields must the
   client never set?
3. Map each CLI command to a method and path. Which are actions rather than CRUD, and why?
4. Decide statuses for: empty description, unknown ID, duplicate create, corrupt file, wrong
   method, unknown route.
5. Where does the mutex live — service, store, or router? Justify by "who owns the data".
6. Which lines of a read-modify-write must be atomic for ID generation to stay unique?
7. List six tests: three handler cases, one middleware, one store, one end-to-end. Name the bug
   each catches.
8. Decide what `GET /tasks?status=bogus` returns and write the rule down before coding it.

---

## Checkpoint

1. Why is an API a second transport, and why should error mapping live in one function?
2. Name the six parts of an HTTP request and give an example of each.
3. Which methods are idempotent, and why does that matter for retries?
4. What does `HandlerFunc` adapt, and what does a `ServeMux` decide?
5. Which four `http.Server` timeouts were named, and why set `ReadHeaderTimeout`?
6. Why set `Content-Type` before the first write, and how do malformed JSON and unknown-field
   errors differ in your messages?
7. What do `MaxBytesReader` and `DisallowUnknownFields` defend against?
8. Why call `r.Use` before registering routes?
9. What changes in the store when requests arrive in parallel, and when do you choose
   `NewRecorder` over `NewServer`?

## Next

Continue with `13-api-feature-by-feature.md` to apply these pieces phase by phase.
