# 14 — APIs in Rust, barebones first

> **Purpose.** Chapters 00–13 were deliberately std-only: you wrote a JSON codec, a UTC clock, a
> store, and a CLI with nothing but the standard library. This chapter keeps that constraint for one
> more stop. You will look at HTTP as *bytes on a socket* and build the smallest possible mental
> model of a server with `std::net::TcpListener`. Nothing here is a finished Task Tracker API; the
> snippets are generic illustrations with deliberate gaps, and the code you write belongs in a
> scratch crate. The next two chapters introduce crates where they are the right tool.

## Why this matters

A framework makes HTTP look like magic: you write an `async fn`, and somehow a browser gets a JSON
response. That magic is comfortable until it fails. When a client hangs, a body is truncated, a
header is duplicated, or a request times out, the abstraction is exactly what hides the cause.
Knowing that a request is a sequence of bytes with a very specific shape lets you reason about
those failures instead of guessing which framework setting to flip.

There is a second reason to start here. The standard library already has everything needed to
*listen* on a port and *read and write bytes*. `std::net::TcpListener`, `Read`, and `Write` are
small APIs you can hold in your head. Once you have felt their sharp edges — partial reads, no
message boundaries, no parsing, no async — the features Axum provides stop being a list of buzzwords
and become answers to problems you have actually met.

> **Professional habit.** Learn the protocol before the framework. A framework is a set of
> decisions made on your behalf; you should be able to say which decisions those are.

## What you will learn

- Describe an HTTP request and response as raw bytes: request line, headers, blank line, body.
- Explain methods, paths, queries, status codes, statelessness, and JSON as a wire format.
- Bind a `TcpListener`, accept `TcpStream`s, and use `Read`/`Write` to exchange bytes.
- Read a header block until `\r\n\r\n`, then read *exactly* `Content-Length` body bytes.
- Explain why a single `read()` is not a message and write a read-until loop.
- Spawn a thread per client with `std::thread::spawn`, and explain why the stream must move.
- Sketch a manual router and name the reasons it becomes fragile.
- State the limitations of the barebones approach and what Axum will fix.

## 1. An API is a conversation over TCP bytes

An **API** (application programming interface) is an agreed-upon way for two programs to talk. An
**HTTP API** is one where the conversation follows the HTTP protocol over a TCP connection.

The sequence is ordinary networking:

1. Your program **binds** to an address such as `127.0.0.1:3000`. The IP identifies the machine;
   the port identifies your process. Binding reserves that address for listening.
2. A client opens a TCP connection to that address. TCP gives you a reliable, ordered **byte
   stream** — but no notion of "one message".
3. The client writes a request: some text, a blank line, and possibly a body.
4. Your program reads those bytes, decides what they mean, and writes a response.
5. Either side closes the connection. In this chapter the server closes after one response
   (`Connection: close`), which keeps the state machine simple.

HTTP is therefore a layer of *formatting conventions* on top of a byte stream. It supplies the
message boundaries that TCP does not: `\r\n` ends a line, an empty line ends the head, and
`Content-Length` says how many body bytes follow.

### Request shape

| Part | Example | Meaning |
|---|---|---|
| Method | `GET`, `POST`, `PATCH`, `DELETE` | The verb: read, create, partially change, remove |
| Request target | `/widgets?status=done` | Path plus optional query string |
| Version | `HTTP/1.1` | Protocol version |
| Headers | `Content-Type: application/json` | Metadata: content type, length, host, authentication |
| Blank line | `\r\n` | Separates head from body |
| Body | `{"name":"lamp"}` | Optional payload, usually JSON for an API |

The **path** is what your router matches on. The **query** is the part after `?`; it is a list of
key/value pairs used for filtering, paging, and options. A path like `/widgets/7` carries an **ID**
in the path itself. All three are just text until you parse them.

The common methods form a useful contract:

| Method | Intended meaning | Idempotent? |
|---|---|---|
| `GET` | Retrieve a representation; no body expected | Yes |
| `POST` | Create a resource or trigger an action | No |
| `PATCH` | Apply a partial update | Depends on how you define it |
| `DELETE` | Remove a resource | Yes, in effect |

"Idempotent" means repeating the request has the same effect as making it once. That matters when a
client retries after a timeout: a retried `GET` is harmless, a retried `POST` may create a duplicate.

### Response shape and status codes

A response starts with a **status line** (`HTTP/1.1 201 Created`), then headers, a blank line, and a
body. The status code tells the client what happened without parsing the body:

| Class | Meaning | Examples you will use |
|---|---|---|
| `2xx` | Success | `200 OK`, `201 Created`, `204 No Content` |
| `3xx` | Redirection | `301`, `304` (not needed for this project) |
| `4xx` | The client's request was wrong | `400 Bad Request`, `404 Not Found`, `405 Method Not Allowed` |
| `5xx` | The server failed | `500 Internal Server Error`, `503 Service Unavailable` |

Choose status codes as part of your API's contract, not as an afterthought. `201` with a `Location`
header says "a new resource exists at this URL"; `204` says "done, and deliberately no body".

### Statelessness

HTTP is **stateless**: the server does not automatically remember the previous request. Every
request must carry what it needs — path, query, headers, body, and any credentials. Your server may
*own* state (the `tasks.json` file), but the protocol does not link two requests together. This is
why you can handle clients in any order, why retries matter, and why a filter such as `?status=done`
must be restated on every list request.

### JSON as a wire format

JSON is just UTF-8 text with a small grammar for objects, arrays, strings, numbers, booleans, and
`null`. It is a **wire format**, not a Rust type: bytes arrive, you decode them into a value, and
you map that value into your own structs. In chapter 06 you wrote that mapping by hand; later,
`serde_json` will do the byte-level part for you while you keep the same boundary discipline.

Two headers make JSON unambiguous:

- `Content-Type: application/json; charset=utf-8` describes the body.
- `Content-Length: <n>` gives the body length **in bytes**, not characters. A description containing
  `é` or `日` occupies more bytes than characters, so always measure `body.as_bytes().len()`.

## 2. One raw request, annotated

In a fenced code block the byte sequences below are written as the two visible characters
`\` and `r` and `\` and `n` so you can see them. On the wire, `\r\n` is one carriage return byte
(`0x0D`) followed by one line feed byte (`0x0A`).

```text
POST /widgets HTTP/1.1\r\n
Host: 127.0.0.1:3000\r\n
Content-Type: application/json\r\n
Content-Length: 15\r\n
Connection: close\r\n
\r\n
{"name":"lamp"}
```

| Line | What it tells the server |
|---|---|
| `POST /widgets HTTP/1.1` | Method, path (no query here), protocol version |
| `Host: 127.0.0.1:3000` | Required by HTTP/1.1; tells the server which host was addressed |
| `Content-Type: application/json` | The body is JSON text in UTF-8 |
| `Content-Length: 15` | Read exactly 15 more bytes as the body |
| `Connection: close` | After this response, close the socket |
| empty line | The four bytes `\r\n\r\n` ending the last header and the blank line; the body starts next |
| `{"name":"lamp"}` | The 15 body bytes; decode, then validate |

Verify the count yourself rather than trusting the prose:

```bash
printf '%s' '{"name":"lamp"}' | wc -c    # 15
```

And a matching response:

```text
HTTP/1.1 201 Created\r\n
Content-Type: application/json; charset=utf-8\r\n
Content-Length: 11\r\n
Location: /widgets/1\r\n
Connection: close\r\n
\r\n
{"ok":true}
```

The response body `{"ok":true}` is 11 bytes. `Location` points at the newly created resource. A
client that understands HTTP needs no documentation to find it.

> **Professional habit.** When debugging by hand, look at bytes first and meanings second. A wrong
> `Content-Length` or a missing blank line explains more failures than a wrong domain rule.

## 3. The smallest std-only server shape

The standard library's entry point is `std::net::TcpListener`. The whole lifecycle fits in a few
steps; the fragments below show the shape, not a drop-in solution.

```rust
use std::io::{Read, Write};
use std::net::TcpListener;

fn serve(addr: &str) -> std::io::Result<()> {
    let listener = TcpListener::bind(addr)?;          // reserve the address
    for stream in listener.incoming() {               // one item per accepted connection
        let mut stream = stream?;                     //  accept can itself fail
        let response = handle_one_request(&mut stream)?;  // your code: read + decide
        stream.write_all(response.as_bytes())?;       //  write_all handles short writes
        stream.flush()?;                              //  push bytes out now
    }                                                  //  stream drops here: connection closed
    Ok(())
}
```

Points worth pausing on:

- `bind` returns `io::Result<TcpListener>`. An address already in use is an error, not a panic.
- `incoming()` is an iterator of `io::Result<TcpStream>`. A failed *accept* should be logged and
  skipped, not allowed to kill the whole server; the sketch uses `?` for brevity, and you should
  improve that in your own version.
- `TcpStream` implements both `Read` and `Write`, so the same value lets you receive and reply.
- `write_all` keeps writing until every byte is handed to the OS. Plain `write` may write fewer
  bytes than you ask, which silently truncates a response.
- Dropping the stream closes the connection. That is only correct because we advertise
  `Connection: close`.

The helper `handle_one_request` is yours to design: read the head, parse it, read the body, choose a
response. The next sections describe each piece.

Open `rustup doc --std` and read the pages for `std::net::TcpListener`, `std::net::TcpStream`,
`std::io::Read`, and `std::io::Write` before writing your version. The signatures are short and the
"Errors" sections explain the edge cases you will hit.

## 4. Partial reads: why one `read()` is not a message

This is the single most important idea in the chapter. TCP is a **stream**, not a mailbox:

- A `read()` may return fewer bytes than requested. A header split across network packets can
  arrive as two reads.
- A `read()` may return bytes from *later* in the stream too. If a client pipelined two requests,
  one read could contain the end of the first head *and* the beginning of the next request.
- `Ok(0)` does not mean "try again". It means the peer performed an orderly shutdown and no more
  bytes will ever arrive on this connection.

So the unit of work is "read until I have the bytes I need", not "call read once". Keep an
accumulating buffer and search the **whole** buffer, not just the newest chunk.

```rust
use std::io::{self, Read};
use std::net::TcpStream;

const MAX_HEAD: usize = 16 * 1024;

fn read_head(stream: &mut TcpStream) -> io::Result<Vec<u8>> {
    let mut head = Vec::new();
    let mut chunk = [0u8; 1024];

    loop {
        if let Some(at) = head.windows(4).position(|w| w == b"\r\n\r\n") {
            if at + 4 > MAX_HEAD {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidData,
                    "request head too large",
                ));
            }
            return Ok(head);                       // terminator is in the buffer
        }
        let n = stream.read(&mut chunk)?;          // n may be small, but never > chunk.len()
        if n == 0 {
            return Err(io::Error::new(
                io::ErrorKind::UnexpectedEof,
                "peer closed before the header ended",
            ));
        }
        head.extend_from_slice(&chunk[..n]);       // keep everything, including a body prefix
        if head.len() > MAX_HEAD
            && head.windows(4).position(|w| w == b"\r\n\r\n").is_none()
        {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "request head too large",
            ));
        }
    }
}
```

Two details deserve attention:

1. `head.windows(4)` checks every adjacent group of four bytes in the accumulated buffer, so a
   terminator split across two reads is still found. `windows` is in `rustup doc --std` under
   slices.
2. We do not throw away bytes after the terminator. A client may send the head and the body in one
   write, so bytes belonging to the body can already be in `head`. Store the result of
   `head.windows(4).position(|w| w == b"\r\n\r\n")` in `at` and treat `&head[at + 4..]` as the start
   of the body. Section 5 shows that split explicitly.

The `MAX_HEAD` limit is a safety habit. Without it, a client that never sends a blank line can make
your server allocate memory forever. Count only the bytes up to and including the terminator: a
large body that arrives in the same `read()` as a small head must not be mistaken for a large head.

For the body the problem is simpler because the length is known. Once you have parsed
`Content-Length`, either use `Read::read_exact` (which loops for you and errors on early EOF) or
write your own loop that appends until `body.len() == content_length`. Do **not** use
`read_to_end` here: it waits for the peer to close, which defeats any future keep-alive support and
lets a client hang your thread indefinitely.

> **Professional habit.** Put bounds on every unbounded read: a maximum header size, a maximum body
> size, and a timeout when you have one. An API that trusts a stranger's `Content-Length` is an API
> waiting to be exhausted.

## 5. Parsing the head and reading the body

Once `read_head` returns, split the accumulated bytes:

```rust
let at = head.windows(4).position(|w| w == b"\r\n\r\n").ok_or_else(/* error */)?;
let head_bytes = &head[..at];
let mut body = head[at + 4..].to_vec();   // bytes that arrived early
let text = std::str::from_utf8(head_bytes).map_err(|e| /* error */)?;
```

`std::str::from_utf8` is the step that turns bytes into text; it can fail, so it belongs in your
error path. Headers are ASCII in practice, but the compiler cannot know that.

Parse the request line first:

```rust
let mut lines = text.split("\r\n");
let request_line = lines.next().unwrap_or("");
let mut parts = request_line.split_whitespace();
let method = parts.next().unwrap_or("");
let target = parts.next().unwrap_or("");
let version = parts.next().unwrap_or("");
```

`split_whitespace` tolerates extra spaces. Validate that all three parts exist and that the version
is one you support; an empty string is not a valid method. Split the target on the first `?`:

```rust
let (path, query) = target.split_once('?').unwrap_or((target, ""));
```

Now the remaining lines are headers. Each has the form `Name: value`; use `split_once(':')`, trim
whitespace from the value, and compare names case-insensitively by lowercasing a copy. For this
barebones server you only need `Content-Length` and `Connection`; store them in local variables
rather than building a general header map. Treat a missing `Content-Length` as zero, but reject
conflicting duplicate values with `400`.

Finally, fill the body to the promised length:

```rust
let needed = content_length.saturating_sub(body.len());
if needed > MAX_BODY {
    return Err(/* 413 Payload Too Large */);
}
let mut chunk = [0u8; 1024];
while body.len() < content_length {
    let n = stream.read(&mut chunk)?;
    if n == 0 {
        return Err(/* incomplete body */);
    }
    body.extend_from_slice(&chunk[..n]);
}
```

Notice that `needed` is used only for the size check: the loop condition uses the accumulated
length, which is the reliable source of truth. A `read()` may overshoot `content_length`, so after
the loop truncate `body` to `content_length`. Any surplus belongs to the next request, not this
payload. At that point `body` holds exactly the request payload, ready to be decoded as JSON in a
later chapter.

## 6. Writing a response

Build the head from a `Vec<u8>` body so the length is unambiguous, then write head and body in two
calls:

```rust
fn response(status: u16, reason: &str, body: &[u8]) -> Vec<u8> {
    let head = format!(
        "HTTP/1.1 {status} {reason}\r\n\
         Content-Type: application/json; charset=utf-8\r\n\
         Content-Length: {}\r\n\
         Connection: close\r\n\
         \r\n",
        body.len()
    );
    let mut out = head.into_bytes();
    out.extend_from_slice(body);
    out
}
```

The `\` at the end of each source line continues the string literal without inserting a newline;
the only real newlines are the explicit `\r\n` sequences. The final `\r\n` is the blank line that
ends the head.

Map the status number to its reason phrase with a small `match` (`200 => "OK"`, `201 => "Created"`,
`400 => "Bad Request"`, `404 => "Not Found"`, `500 => "Internal Server Error"`, and so on). The
reason phrase is not interpreted by modern clients, but it must be present and it helps a human
reading `curl` output.

## 7. One client at a time is not enough

The loop in section 3 handles one connection completely before accepting the next. If a client
connects and then stalls halfway through a header, every other client waits. That is a
**head-of-line blocking** problem.

The std-only fix is a thread per connection:

```rust
for stream in listener.incoming() {
    let mut stream = match stream {
        Ok(stream) => stream,
        Err(err) => {
            eprintln!("accept failed: {err}");
            continue; // one bad accept must not kill the server
        }
    };
    std::thread::spawn(move || {
        if let Err(err) = handle_client(&mut stream) {
            eprintln!("client error: {err}");
        }
    });
}
// the accept loop continues immediately after spawning
```

The `move` keyword is not cosmetic. `thread::spawn` requires a closure that can live independently
of the current stack frame, so everything it captures must be **owned** by the closure. Without
`move`, the closure would borrow `stream`, which dies at the end of the loop iteration while the
thread may still be running. The compiler reports that as a lifetime error; `rustc --explain E0597`
is a useful detour if you meet it. Moving the `TcpStream` transfers ownership into the thread, which
is exactly what we want.

This approach has real limits: each thread has a stack, and an attacker can open many connections,
so a production server needs limits and timeouts. Tokio's async tasks in the next chapter are much
cheaper than OS threads, but the ownership idea is the same.

What about data shared by all clients — for example one in-memory task list? Conceptually you would
wrap it in `Arc<T>` for shared ownership and `Mutex<T>` for exclusive access: `Arc` lets several
threads hold a handle to the same value, and `Mutex` ensures only one thread mutates it at a time.
Those types are in the standard library (`rustup doc --std`), but we will not build that here.
Notice two costs to keep in mind: lock contention when many clients queue up, and poisoned locks
after a panic. Axum's `State` will give this idea a nicer shape in chapter 15.

## 8. A tiny generic handler sketch

Keep the decision layer separate from the byte plumbing. A deliberately small, non-Task-Tracker
example returns a status and a body:

```rust
fn ping_or_echo(request_line: &str, body: &[u8]) -> (u16, String) {
    match request_line {
        "GET /ping HTTP/1.1" => (200, "pong".to_string()),
        "POST /echo HTTP/1.1" => (200, String::from_utf8_lossy(body).into_owned()),
        _ => (404, "not found".to_string()),
    }
}
```

`String::from_utf8_lossy` replaces invalid UTF-8 instead of failing, which is fine for an echo toy
and wrong for validation. The point is the shape: pure-ish decision code taking parsed inputs and
returning an outcome, with I/O kept outside. That separation is the one you will carry into Axum
handlers, where extractors hand you parsed values and you return a response.

## 9. A manual router, and where it cracks

With method and path in hand, a first router looks tempting:

```rust
let parts: Vec<&str> = path.trim_start_matches('/').split('/').collect();
match (method, parts.as_slice()) {
    ("GET", ["widgets"]) => { /* list */ }
    ("GET", ["widgets", id]) => {
        let id = id.parse::<u64>();   // you must handle the error
        /* fetch one */
    }
    ("POST", ["widgets"]) => { /* create */ }
    _ => { /* 404, or 405 if the path was known */ }
}
```

This works for three routes and then starts to hurt:

| Fragility | Why it bites |
|---|---|
| Trailing slashes and double slashes | `/widgets/` and `/widgets` become different shapes unless you normalize |
| Percent-encoding | `%20` and `+` in a path or query need decoding before matching |
| Query strings | Must be stripped before splitting, or IDs contain `?status=...` |
| Method mismatch | A known path with the wrong verb should be `405` with an `Allow` header, not `404` |
| Prefix ambiguity | `/widgets/new` versus `/widgets/{id}` depends on match order |
| No compile-time exhaustiveness | Adding a route cannot produce a compiler error where you forgot a case |
| No typed extraction | Every handler repeats parsing, validation, and error mapping |
| Nested resources | `/widgets/{id}/parts/{part_id}` multiplies the special cases |
| Security details | Path traversal and over-long segments need explicit rejection |

None of these are impossible to handle by hand. They are simply not where your learning time is best
spent once you understand *why* they exist. A framework turns this match pyramid into declarations
and typed extractors.

## 10. What this chapter deliberately omits

| Missing piece | Consequence |
|---|---|
| TLS | Credentials and data would travel in plain text; never expose this server beyond loopback |
| Keep-alive and chunked transfer | One request per connection, and `Content-Length` is mandatory |
| Robust parsing | No limits on duplicate headers, odd whitespace, or malformed request lines |
| Async | Thread-per-client is expensive and needs explicit limits |
| Extractors and middleware | Every handler repeats parsing, logging, auth, and error mapping |
| Timeouts and backpressure | A slow or hostile client can occupy a thread indefinitely |
| Testing ergonomics | End-to-end checks need a real socket; pure decision helpers can still be unit-tested |

This is a teaching instrument, not a final tool. Its value is that each missing item is now a
concrete name rather than a mystery.

## 11. Why Axum is next

Axum 0.8.9, the version used in this path, answers those gaps directly. Its minimum supported
Rust version is 1.80, and this environment's 1.99.1 toolchain is comfortably above it:

- **Routing** is declared, with path parameters written as `/{id}` and method handlers attached to
  each route.
- **Extractors** turn request parts and bodies into typed values (`Path<T>`, `Query<T>`, `Json<T>`,
  `State<T>`) and produce consistent rejections when input is bad.
- **Tokio** provides an async runtime, so many connections are handled by cheap tasks rather than
  one OS thread each.
- **Tower** supplies the middleware model: layers wrap your router for logging, timeouts, and
  request IDs.
- **hyper** does the protocol work underneath; Axum is a thin, typed layer over it rather than a
  replacement for it.
- **State** gives every handler the same shared store and clock without globals.

You will still make the decisions: what a DTO looks like, which status code means what, where a lock
is taken, and how errors are shaped. Axum removes the protocol plumbing, not the design work.

## Offline references

| Need | Command |
|---|---|
| Socket and I/O API details | `rustup doc --std` (`std::net`, `Read`, `Write`, `std::str::from_utf8`) |
| Ownership and `move` closures | `rustup doc --book`, `rustup doc --reference` |
| Cargo commands and features | `rustup doc --cargo` |
| "Use of moved value" | `rustc --explain E0382` |
| "Borrowed value does not live long enough" | `rustc --explain E0597` |
| "Trait bound not satisfied" | `rustc --explain E0277` |

## Common mistakes

- Treating one `read()` as one request. It is one chunk, and it may be half a header or two
  requests.
- Searching only the newest chunk for `\r\n\r\n` instead of the accumulated buffer.
- Forgetting to keep the body bytes that arrived together with the head.
- Using character counts for `Content-Length` instead of byte counts.
- Forgetting the blank line between head and body; clients will wait forever for bytes that never
  come.
- Using plain `write` and assuming all bytes were sent.
- `unwrap()` on a parse failure from a remote client. Bad input is an expected error, not a panic.
- Forgetting `Connection: close` while also closing the socket after one response.
- Letting a thread borrow a stack-local stream instead of moving it in.
- Believing a manual router is "good enough" once a fifth route or a query parameter appears.

## Apply it to Task Tracker

1. Which parts of your existing `tasks.json` knowledge are *state*, and which parts of an HTTP
   request must be restated on every call?
2. If a client sends `PATCH /tasks/1` with a JSON body in two TCP writes, what two bugs would a
   single-`read()` server show?
3. What should your manual router return for `PUT /tasks/1`, and why is that different from
   `GET /tasks/999`?
4. Where would you put a maximum body size, and how would you report a request that exceeds it?
5. If two clients add a task at the same time, what does your store need to guarantee before any
   framework is involved?

## Checkpoint

Explain in your own words why `Content-Length` exists, why a `read()` loop is still needed when you
have it, and why `thread::spawn` requires `move`. If you cannot explain all three, re-read sections
4 and 7 before adding dependencies.

## Next

Continue with `15-axum-api.md` once you can explain the three questions above.
