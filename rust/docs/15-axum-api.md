# 15 — HTTP APIs with Axum

> **Purpose.** This is the first chapter in the Rust path where external crates are allowed, because
> for HTTP they are the right tool. You will learn the shape of an Axum 0.8 application: routing,
> extractors, shared state, DTOs, errors, middleware, and in-process tests. The snippets are generic
> (`Health`, `Widget`, `User`) and deliberately short. There is no Task Tracker handler set, no full
> `main.rs` for the project, and no solution to paste. Chapter 16 asks *you* to design that.

## Why this matters

Chapter 14 showed how much work it is to turn TCP bytes into a reliable request. You parsed a head,
counted body bytes, chose status codes by hand, and spawned threads. Every one of those steps is
correct, and every one of them is a place for a subtle bug. Axum takes over that layer so your
attention can move to the part that is actually yours: the rules of your API.

Axum also fits Rust unusually well. Handlers are plain functions. Their arguments declare what they
need from a request, and the compiler checks those declarations. Invalid input arrives as a typed
rejection instead of an unchecked string. State is passed explicitly rather than fetched from a
global. The result is an API whose contract is visible in function signatures.

> **Professional habit.** Adopt a framework because it removes work you understand and do not want
> to repeat — not because you hope it hides a concept you skipped. Chapter 14 is what makes this
> choice deliberate.

## What you will learn

- Explain why Axum, Tokio, Tower, and hyper are a good fit for typed Rust.
- Add the dependencies with Cargo and read their local documentation offline.
- Write an `async fn` handler and explain `#[tokio::main]`, futures, and `.await`.
- Declare routes with `Router`, method routers, nested paths, and `{id}` parameters.
- Use `State`, `Path`, `Query`, and `Json` extractors, and explain why the body extractor is last.
- Share a store and a clock with `Arc`, `Mutex`, and careful lock scopes.
- Separate wire DTOs from domain types with serde attributes.
- Return consistent JSON errors with `IntoResponse` and `Result<T, ApiError>`.
- Add middleware with `Router::layer` and test a router with `oneshot`, without opening a socket.

## 1. Why Axum, and what the alternatives are

This path uses Axum 0.8.9. Its minimum supported Rust version is 1.80; this environment's 1.99.1
toolchain is comfortably above that. It sits on a small stack of well-defined layers:

| Layer | Role |
|---|---|
| **hyper** | HTTP/1 semantics, connections, and byte-level request/response types |
| **Tokio** | Async runtime: tasks, timers, networking, signals |
| **Tower** | `Service` and `Layer` abstractions used for middleware |
| **Axum** | Routing, extractors, response conversion — a thin typed layer over the rest |

What you get in practice:

- **Macro-free routing.** `Router::new().route("/healthz", get(healthz))` is ordinary Rust code; no
  attribute macros generate hidden machinery for your routes.
- **Extractors.** A handler argument like `Path(id): Path<u64>` means "parse the `{id}` segment into
  a `u64` or reject the request". The compiler sees the type; you do not repeat parsing in each
  handler.
- **Async concurrency.** Requests run as Tokio tasks, not one OS thread per client.
- **State and middleware.** Shared dependencies and cross-cutting concerns have explicit homes.

Other tools are good; they are simply different tradeoffs:

| Tool | Fair description | Why not here |
|---|---|---|
| `hyper` | Lowest-level HTTP implementation; maximum control | You would rebuild routing, extractors, and error mapping yourself |
| `actix-web` | Mature, more batteries-included framework with its own actor ecosystem | More framework-specific concepts to learn before writing a handler |
| `Rocket` | Different ergonomics, attribute-driven guards and routes | A different mental model; the typed-extractor style is our goal here |
| **Axum** | Small, typed, composable, directly from the async Rust ecosystem | Chosen for this project |

You are not learning "the only Rust web framework". You are learning one good one, on top of pieces
(hyper, Tokio, Tower) that are useful knowledge elsewhere.

## 2. Add the dependencies

Run these from `starter-project/`, the real Cargo package, **when you start the API track** — not
during the earlier CLI phases:

```bash
cargo add axum
cargo add tokio --features full
cargo add serde --features derive
cargo add serde_json
cargo add --dev tower --features util
```

| Command | What it adds |
|---|---|
| `cargo add axum` | `axum = "0.8"` in `[dependencies]`; Cargo selects the latest compatible 0.8.x |
| `cargo add tokio --features full` | The async runtime, macros, net, sync, time, and signal modules |
| `cargo add serde --features derive` | `Serialize` and `Deserialize` derive macros for DTOs |
| `cargo add serde_json` | JSON encoding and decoding at the HTTP boundary |
| `cargo add --dev tower --features util` | `ServiceExt::oneshot` for in-process router tests only |

Two details are easy to miss:

- **Pinning.** A bare `"0.8"` means "any compatible 0.8.x". `cargo add axum@0.8.9` records `0.8.9`
  as the minimum compatible version; use `cargo add axum@=0.8.9` if you truly want `=0.8.9` and no
  upgrades. Decide deliberately and record the decision in your README.
- **`tower`'s `util` feature.** `ServiceExt` (and therefore `oneshot`) is gated behind it in Tower
  0.5. Without `--features util`, the import in section 11 will not compile. It is a
  dev-dependency: nothing ships in the server binary.

Adding dependencies changes `Cargo.toml` and `Cargo.lock`. That is expected now; the std-only rule
governed chapters 00–13, not this track. Inspect what arrived:

```bash
cargo tree                         # who pulled in what
cargo doc --open --package axum    # local API docs for axum
cargo doc --open --package tokio   # local API docs for the runtime
```

`cargo doc --open --package axum` is your primary reference for the rest of this chapter. Everything
is generated from the installed source, so it matches your exact version. Keep `rustup doc --std`,
`rustup doc --book`, `rustup doc --reference`, and `rustup doc --cargo` open as before; and use
`rustc --explain E0277` when a trait bound confuses you.

## 3. Async basics you need

You do not need to write a runtime. You need four ideas.

**A future is lazy.** An `async fn` returns a value implementing `Future`. Nothing inside it runs
until something polls it. Calling `cleanup().await` is what actually drives it to completion.

**`.await` yields.** At an `.await`, the task says "I am waiting; run something else". When the
awaited operation is ready, execution continues. This is how one thread serves many connections.

**`#[tokio::main]` builds the runtime.** On `main`, that attribute expands to code that creates a
multi-threaded Tokio runtime and blocks until the async main future completes. Inside it, your
handlers can await sockets, timers, and locks.

**Handlers are async because their inputs and outputs may await.** The `Json<T>` extractor reads the
request body asynchronously; a graceful shutdown awaits a signal. Making a handler `async fn` is the
normal case, not an optimization.

Two safety ideas show up in compiler errors:

- **`Send`** means a value may be transferred between threads. Tokio can move tasks between worker
  threads, so most spawned futures must be `Send`.
- **`Sync`** means sharing `&T` between threads is safe. Shared state handed to every handler is
  typically required to be both `Send` and `Sync`.

When you see "future is not Send", the cause is usually a non-`Send` value — often a synchronous
mutex guard — held across an `.await`.

The practical rule: **do not block the async runtime**. Avoid `std::thread::sleep`, long CPU-bound
loops, and blocking file or network I/O inside a handler. Use `tokio::time::sleep`, `tokio::fs`, and
`tokio::net` equivalents; move genuinely blocking work to `tokio::task::spawn_blocking`. For a tiny
local file, a short synchronous store call may be acceptable — but make that a measured, documented
choice, not an accident.

## 4. A minimal generic Axum app

This is the smallest useful shape: one route, one handler, one server. It is a teaching skeleton,
not the Task Tracker API.

```rust
use axum::{routing::get, Json, Router};
use serde_json::{json, Value};

async fn healthz() -> Json<Value> {
    Json(json!({ "status": "ok" }))
}

#[tokio::main]
async fn main() {
    let app = Router::new().route("/healthz", get(healthz));

    let listener = tokio::net::TcpListener::bind("127.0.0.1:3000")
        .await
        .expect("could not bind 127.0.0.1:3000");

    axum::serve(listener, app)
        .await
        .expect("server failed");
}
```

Read it in order:

1. `Router::new()` builds a router; `.route(path, method_router)` attaches a path.
2. `get(healthz)` is a **method router** for `GET` requests to that path.
3. `tokio::net::TcpListener::bind(...).await` is the async cousin of the std listener from chapter
   14. Note the different type: `axum::serve` needs Tokio's listener.
4. `axum::serve(listener, app).await` is the current serving pattern for Axum 0.8. It loops,
   accepts connections, and dispatches requests to your router.

`expect` is acceptable in a fifteen-line teaching skeleton so the shape stays visible. In your own
server, return a `Result` from `main` or map the error into a clear message.

## 5. Routing

### Methods on one path

Attach several method routers by chaining:

```rust
let app = Router::new()
    .route("/widgets", get(list_widgets).post(create_widget))
    .route("/widgets/{id}", get(get_widget).patch(update_widget).delete(delete_widget));
```

The handler names are illustrative; you write your own. If a path is known but the method is not,
Axum answers `405 Method Not Allowed`; an unknown path yields `404`. You do not hand-write those
decisions.

### Path syntax in Axum 0.8

Parameters use braces: **`/widgets/{id}`**, not the older `/widgets/:id` form you may see in stale
examples. A `{id}` segment captures one path segment and makes it available to the `Path` extractor.
Other forms exist (such as a catch-all `{*rest}`), but you do not need them here.

> **Professional habit.** When an online example disagrees with your local docs, trust
> `cargo doc --open --package axum`. The installed documentation matches the code you compile.

### Nested routes and prefixes

`Router::nest("/admin", admin_router)` mounts a whole router under a prefix, so the inner routes
keep short paths. Use it to group unrelated areas of an API. Prefixes must not overlap ambiguously
with other routes in the same router; Axum rejects conflicting layouts rather than silently choosing
one.

### Path and query parameters

```rust
use axum::extract::{Path, Query};
use serde::Deserialize;

#[derive(Deserialize)]
struct ListQuery {
    status: Option<String>,
}

async fn get_widget(Path(id): Path<u64>) -> String {
    format!("widget {id}")
}

async fn list_widgets(Query(query): Query<ListQuery>) -> String {
    match query.status {
        Some(status) => format!("filter: {status}"),
        None => "all widgets".to_string(),
    }
}
```

`Path<u64>` parses and validates the captured segment. A non-numeric `id` becomes a rejection — by
default `400 Bad Request` — before your handler body runs. `Query<T>` deserializes the part after
`?` using serde; `Option` fields model "not supplied".

### Why the body extractor must be last

An HTTP request body is a stream that can be consumed only once. Extractors such as `Path`, `Query`,
and `State` read only the request *parts* (method, URI, headers, extensions) and never touch the
body. `Json<T>` reads the body. Therefore it must be the final argument:

```rust
async fn example(
    // parts-only extractors first
    Path(id): Path<u64>,
    Query(query): Query<ListQuery>,
    // body-consuming extractor last
    Json(payload): Json<CreateWidget>,
) { /* ... */ }
```

If you put another body reader after `Json`, it will find an empty body. Axum enforces this by
requiring all extractors except the last to implement `FromRequestParts`.

## 6. Extractors, and failures

An **extractor** is a value a handler asks for, which Axum builds from the request before calling
the handler. The four you need:

| Extractor | Source | Typical use |
|---|---|---|
| `State<T>` | Router state | The shared store, clock, and settings |
| `Path<T>` | Captured path segments | `Path(u64)` or a struct with named fields |
| `Query<T>` | Query string | `?status=done`, paging, filters |
| `Json<T>` | Request body | Deserialize a JSON object into a DTO |

There are two traits behind them (`rustup doc` for axum shows the details):

- `FromRequestParts` builds values from request parts without consuming the body. `Path`, `Query`,
  and `State` work this way.
- `FromRequest` may consume the body. `Json` works this way, which is why it goes last.

When an extractor cannot build its value, it produces a **rejection**. Axum turns a rejection into a
response automatically. In Axum 0.8.9 the defaults you will meet are: `Path<u64>` with `abc` → `400`,
malformed JSON syntax → `400`, syntactically valid JSON that does not fit the DTO (for example a
missing required field, `JsonDataError`) → `422`, and a missing or wrong `Content-Type` → `415`.
The default bodies are plain text. If your API promises one JSON error envelope and one status
policy for *every* failure, you need to map rejections:

- Accept `payload: Result<Json<CreateWidget>, axum::extract::rejection::JsonRejection>` and convert
  the rejection into your own error type.
- Or write a small custom extractor whose rejection type is already your `ApiError`.
- Or accept the default rejection format and document it. Consistency matters more than which
  choice you make.

Handler return values are equally flexible. `String` becomes `text/plain`, `Json<T>` becomes JSON,
`StatusCode` becomes an empty response with that code, and tuples compose:
`(StatusCode, Json<T>)`. A handler may return `Result<T, E>` whenever `E: IntoResponse`.

## 7. Shared state

Every handler that touches tasks needs the same store and the same clock. Axum passes them through
**state**:

```rust
#[derive(Clone)]
struct AppState {
    // generic shared dependencies: a store handle, a clock, settings
}

// at construction time:
// let app = Router::new().route(/* ... */).with_state(AppState { /* ... */ });

// in a handler:
// async fn handler(State(state): State<AppState>) { /* use state */ }
```

Before `.with_state`, the router's type is `Router<AppState>`; after it, the router is `Router<()>`
and can be handed to `axum::serve`. That type change is how the compiler checks that you supplied
state before serving.

`#[derive(Clone)]` is the usual approach because Axum clones the state for each request. Cloning an
`Arc<T>` increments a reference count — cheap, and all clones point to the same value. That is why
real state fields are typically `Arc<...>` rather than large owned structures.

For mutation, two mutexes are relevant:

| Choice | Behavior | Use when |
|---|---|---|
| `std::sync::Mutex<T>` | Blocking, synchronous lock | Critical section is tiny and contains no `.await` |
| `tokio::sync::Mutex<T>` | Async-aware lock; waiting yields to the runtime | The section may await, or you need async-friendly fairness |

**Never hold a lock guard across `.await`.** With a `std::sync::Mutex`, that typically produces a
"future is not Send" compiler error because the guard is not `Send`; with either mutex it invites
deadlocks when two tasks wait on each other. Take the lock, copy out the small value you need,
drop the guard, then await. Also remember that a `std::sync::Mutex` can be **poisoned** if a thread
panics while holding it; decide how to report that instead of unwrapping the lock result.

## 8. DTOs and serde

A **DTO** (data transfer object) is a struct whose only job is to describe a shape on the wire. Keep
it separate from your domain `Task`, for the same reason chapter 09 kept modules separated: the
file format, the HTTP format, and the internal model may each change for different reasons.

```rust
use serde::{Deserialize, Serialize};

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct CreateWidgetRequest {
    name: String,
}

#[derive(Debug, Serialize)]
struct WidgetResponse {
    id: u64,
    #[serde(rename = "createdAt")]
    created_at: String,
    status: WidgetStatus,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "kebab-case")]
enum WidgetStatus {
    Todo,
    InProgress,
    Done,
}
```

What each attribute does:

- `#[serde(rename = "createdAt")]` on a **field** changes that JSON key only. Rust keeps
  `created_at`; the wire gets `createdAt`, matching the CLI's timestamp key.
- `#[serde(rename_all = "kebab-case")]` on the **enum** converts unit variants to strings:
  `Todo` → `todo`, `InProgress` → `in-progress`, `Done` → `done`. That is exactly the status
  vocabulary from chapter 11. Renaming rules attach to the item they decorate; field names and enum
  variants are separate concerns.
- `#[serde(deny_unknown_fields)]` rejects unexpected keys instead of ignoring them. It is optional:
  strictness gives early feedback for typos, while ignoring unknown fields is friendlier to clients
  that send extra metadata. Choose one policy and document it.
- `Option<T>` fields model absence. For partial updates, decide how you distinguish "field missing"
  from "explicitly set to null"; a plain `Option<String>` collapses those two cases, and that may be
  the wrong contract.

Why the separation pays off:

1. You can rename an internal field without breaking clients.
2. Invalid states are harder to express: a DTO can be decoded, validated, and only then converted
   into a domain value.
3. Response shape is explicit. Adding an internal cache field does not accidentally leak into JSON.
4. The same domain store can serve the CLI and the API without either dictating the other's bytes.

Do not derive `Deserialize` straight onto a domain type just because it is convenient. That couples
your persisted invariant to whatever a stranger put in a request body.

## 9. Responses and errors

Use `StatusCode` from `axum::http` (Axum re-exports the `http` crate, so you do not add it
separately). Compose responses with tuples and `IntoResponse`:

```rust
use axum::{
    http::StatusCode,
    response::{IntoResponse, Response},
    Json,
};
use serde::Serialize;

#[derive(Serialize)]
struct ErrorEnvelope {
    error: String,
    message: String,
}

enum ApiError {
    BadRequest(String),
    NotFound(String),
    Internal(String),
}

impl IntoResponse for ApiError {
    fn into_response(self) -> Response {
        let (status, code, message) = match self {
            ApiError::BadRequest(m) => (StatusCode::BAD_REQUEST, "bad_request", m),
            ApiError::NotFound(m) => (StatusCode::NOT_FOUND, "not_found", m),
            ApiError::Internal(m) => (StatusCode::INTERNAL_SERVER_ERROR, "internal", m),
        };
        (status, Json(ErrorEnvelope { error: code.into(), message })).into_response()
    }
}
```

Now a handler can return `Result<Json<WidgetResponse>, ApiError>` and use `?` for domain failures.
Every error, including extractor failures you mapped, has one shape:

```json
{ "error": "not_found", "message": "no widget with id 7" }
```

Guidelines:

- Map *client* mistakes to `4xx` and *server* failures to `5xx`. A corrupt store is `500`; an
  unknown ID is `404`; a malformed body is `400`.
- Keep `500` messages safe. Log the detail server-side and return a generic message to the client.
- Use `201 Created` with a `Location` header for creation and `204 No Content` for deletion if you
  want no body. `200 OK` with a body is also valid — decide once and write it down.
- Implement `std::fmt::Display` and `std::error::Error` for your error type if it also flows through
  non-HTTP code, and add `From` impls only for mechanical conversions. Add context where a bare
  conversion would lose the ID or file name.

## 10. Middleware and layers

A **layer** wraps your router so every request passes through extra logic. The common entry point
is `axum::middleware::from_fn`:

```rust
use axum::{
    extract::Request,
    middleware::{self, Next},
    response::Response,
    Router,
};

async fn add_request_id(mut request: Request, next: Next) -> Response {
    // Generate an id, put it in request.extensions_mut(), then:
    next.run(request).await
}

// let app = Router::new().route(/* ... */).layer(middleware::from_fn(add_request_id));
```

Useful middleware for this project: request IDs, access logging, authentication, body-size limits,
and timeouts. `Router::layer` applies to every route in that router; calling it again stacks another
layer. The order changes which wrapper sees the request first and the response last, so verify it
with a tiny two-layer experiment rather than guessing.

A timeout can be built with `tokio::time::timeout` around `next.run(request)`, or with Tower's
timeout support if you promote Tower to a normal dependency and enable its `timeout` feature.
`TraceLayer` from **tower-http** is a popular logging choice, but it is another crate; add it only if
you deliberately decide you want it.

> **Professional habit.** Middleware is a place for behavior that is truly cross-cutting. Put a
> rule in a handler if it belongs to one route; put it in a layer only when every route needs it.

## 11. Testing without a network

Because a `Router` is a Tower `Service`, you can send it a request directly. That makes tests fast
and independent of ports and firewalls. This is one generic health test:

```rust
#[cfg(test)]
mod tests {
    use axum::{
        body::{to_bytes, Body},
        http::{Request, StatusCode},
        routing::get,
        Json, Router,
    };
    use serde_json::{json, Value};
    use tower::ServiceExt; // requires tower's `util` feature

    async fn healthz() -> Json<Value> {
        Json(json!({ "status": "ok" }))
    }

    #[tokio::test]
    async fn healthz_returns_ok() {
        let app = Router::new().route("/healthz", get(healthz));

        let response = app
            .oneshot(
                Request::builder()
                    .uri("/healthz")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::OK);
        let bytes = to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let value: Value = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(value["status"], "ok");
    }
}
```

Notes:

- `oneshot` consumes the router. Clone it first if you need several requests in one test.
- `#[tokio::test]` supplies a runtime for the async test.
- `to_bytes(body, limit)` collects the response body; pass a real limit in tests that care about
  size.
- The same pattern works for POST: add `.method("POST")`, a `Content-Type` header, and a `Body::from`
  containing JSON bytes. Assert the status and decode the body into `serde_json::Value`.
- These tests do not exercise real sockets, so keep one or two `curl` smoke checks for the wiring.

## 12. Running it: addresses, curl, and shutdown

Bind to `127.0.0.1:3000` while developing: only your machine can reach it. `0.0.0.0:3000` exposes the
server on every interface and should be a deliberate choice. Port numbers below 1024 usually need
elevated privileges.

```bash
cargo run                       # with the health-only skeleton from section 4
curl -i http://127.0.0.1:3000/healthz
```

The widget request below is for a later experiment, not the health-only skeleton:

```bash
curl -i -X POST http://127.0.0.1:3000/widgets \
  -H 'Content-Type: application/json' \
  -d '{"name":"lamp"}'
```

Until you add that route, expect `405` or `404` — which is itself a useful check that the router
dispatches only what you declared.

A production-style server stops cleanly: it finishes in-flight requests, closes listeners, and
releases resources instead of being killed mid-write. Axum supports this with a shutdown future:

```rust
async fn shutdown_signal() {
    let _ = tokio::signal::ctrl_c().await;
}

// axum::serve(listener, app)
//     .with_graceful_shutdown(shutdown_signal())
//     .await?;
```

Keep a local data file out of the repository while smoke-testing: start the server from a scratch
directory so relative paths such as `tasks.json` resolve there, exactly as chapter 11 did for the
CLI.

## Professional habits

- Run `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings`, `cargo check`, and
  `cargo test` after each phase. The API track changes `Cargo.toml`, so re-run `cargo tree` when a
  dependency surprise appears.
- Document each public handler and DTO with `///`; state what it accepts, what it rejects, and what
  it returns.
- Keep the dependency list intentional. Every crate in `Cargo.toml` should have a sentence in the
  README explaining why it is there.
- No `unwrap()` in handlers or store code. Client input is untrusted; errors are values.
- Commit one behavior at a time: "Add health route", "Add JSON error envelope", "Add oneshot
  tests".
- Keep your build journal going: what changed, which error taught you something, one idea that
  became clearer.

## Common mistakes

- Binding to a port and then connecting to a different one; check the address in both places.
- Using `std::net::TcpListener` with `axum::serve`. The serving pattern expects
  `tokio::net::TcpListener`.
- Writing `/widgets/:id` from an older tutorial. Axum 0.8 uses `{id}`.
- Putting `Json<T>` before another extractor that consumes the body.
- Forgetting `--features util` on the dev `tower` dependency, then wondering why `oneshot` is
  missing.
- Holding a `std::sync::MutexGuard` across `.await`, and treating the resulting `Send` error as a
  framework bug.
- Returning different error shapes from different handlers. Consistency is part of the contract.
- Letting serde deserialize a request straight into a domain type with no validation step.
- Running blocking file I/O or `std::thread::sleep` inside an async handler.
- Adding tower-http, tracing subscribers, or a database "because APIs have those" before the core
  routes work.

## Apply it to Task Tracker

1. Which of your CLI rules ID computation, status vocabulary, timestamp format, no-op behavior,
   error messages — belong in the domain layer, and which belong only in HTTP DTOs?
2. What should `GET /tasks/abc` return, and which layer notices the problem first?
3. Where would you inject your existing fixed clock so a test can assert `createdAt` exactly?
4. Which state is truly shared across requests, and can you avoid a mutex entirely for read-only
   paths?
5. If you promise a JSON error envelope, how will extractor rejections be mapped into it?
6. What would you test with `oneshot`, and what would you still verify with `curl`?

## Checkpoint

Write down, without looking, the order of a handler's arguments when it needs state, a path ID,
query filters, and a JSON body — and explain why the last one is last. Then explain what
`axum::serve` receives and why the listener type matters. If either answer is fuzzy, re-read
sections 5 and 4 before starting chapter 16.

## Next

Continue with `16-api-feature-by-feature.md` to apply these pieces phase by phase.
