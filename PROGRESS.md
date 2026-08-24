# Learning Progress

Master tracker for my programming journey across all languages and projects.

> **Last updated:** 2026-08-21

---

## Current Focus

**Go** — Learning through building. Currently on Project #1 (Go Bank, v5 — HTTP API).

**Roadmap:** Go → several serious projects → Rust

---

## Go

### Status: Active — Primary Language

### Projects

| #   | Project                | Description                                                     | Status           | Key Concepts                                                                       |
| --- | ---------------------- | --------------------------------------------------------------- | ---------------- | ---------------------------------------------------------------------------------- |
| 1   | [go-bank](go/go-bank/) | CLI → HTTP banking API                                          | In progress (v5) | Structs, methods, pointers, errors, JSON, file I/O, HTTP server, routing, handlers |
| 2   | TBD                    | Should introduce interfaces, concurrency, testing, architecture | Not started      | —                                                                                  |
| 3   | TBD                    | TUI or networking-heavy app (Postman-like TUI client is a goal) | Not started      | —                                                                                  |

### Concepts Demonstrated

#### Language Fundamentals

- [x] Variables, constants, types, zero values
- [x] Functions, multiple return values
- [x] Structs
- [x] Methods (pointer receivers)
- [x] Pointers
- [x] Maps
- [x] Slices
- [x] Constants (grouped `const` block)
- [ ] Closures
- [ ] Variadic functions
- [ ] Interfaces
- [ ] Type assertions / type switches
- [ ] Embedding / composition
- [ ] Generics

#### Error Handling

- [x] `error` values, `if err != nil`
- [x] `errors.New()`
- [x] `panic`
- [x] `defer`
- [x] `recover`
- [ ] `fmt.Errorf` / error wrapping
- [ ] `errors.Is` / `errors.As`
- [ ] Sentinel errors
- [ ] Custom error types (implementing `error` interface)

#### Standard Library

- [x] `net/http` — server, ServeMux, handlers, path params
- [x] `encoding/json` — Marshal, Unmarshal, Encoder, Decoder, struct tags
- [x] `os` — ReadFile, WriteFile, Stat
- [x] `time` — Now, Format, RFC1123
- [x] `strings` — ToLower, Replace
- [x] `fmt` — Printf, Sprintf, Fprintln
- [x] `log` — Fatalln
- [ ] `context`
- [ ] `sync`
- [ ] `io` / `bufio`
- [ ] `testing`
- [ ] `net/http/httptest`
- [ ] `strconv`

#### HTTP / Web

- [x] HTTP server setup
- [x] Routing with ServeMux
- [x] Path parameters (`r.PathValue`)
- [x] JSON request/response
- [x] HTTP methods and status codes
- [x] Content-Type headers
- [ ] Query parameters
- [ ] Middleware
- [ ] Context / timeouts
- [ ] Graceful shutdown
- [ ] Authentication / authorization

#### Concurrency

- [ ] Goroutines
- [ ] Channels (buffered / unbuffered)
- [ ] `select`
- [ ] Mutexes / sync primitives
- [ ] WaitGroup
- [ ] Race detection
- [ ] Context cancellation

#### Testing

- [ ] Unit tests (`*_test.go`)
- [ ] Table-driven tests
- [ ] `httptest`
- [ ] Benchmarks
- [ ] Mocking / fakes

#### Architecture

- [x] Multi-file packages
- [x] Separation of handlers / storage / models
- [ ] Service layer pattern
- [ ] Repository / storage abstraction (interfaces)
- [ ] Package separation (beyond `package main`)
- [ ] Dependency injection

### Go Notes

- Currently all code is in `package main` — haven't worked with multiple packages yet
- Haven't used interfaces at all yet — this is a priority for Project #2
- Concurrency is not yet explored but is critical since the HTTP server handles concurrent requests
- TUI project is an important future goal (Postman-like terminal API client)
- USSD service is a future goal for Go Bank specifically

---

## Rust

### Status: Not Started — Future

Will transition after ~3 substantial Go projects.

### Projects

| #   | Project                                  | Description                          | Status      |
| --- | ---------------------------------------- | ------------------------------------ | ----------- |
| 1   | [starter-project](rust/starter-project/) | Initial scaffold (Cargo.toml + src/) | Not started |

### Concepts to Learn (When Ready)

- [ ] Ownership / borrowing / lifetimes
- [ ] Traits
- [ ] Enums / pattern matching
- [ ] Error handling (Result, Option)
- [ ] Concurrency
- [ ] Async Rust
- [ ] Systems programming

### Rust Transition Readiness

**Not ready.** Need to complete Go roadmap first.

---

## Python

### Status: Exploratory

Python is not my primary learning focus but I've done a few things here.

### Projects

| Project                                                        | Description                                              | Status   |
| -------------------------------------------------------------- | -------------------------------------------------------- | -------- |
| [codecrafters-shell-python](python/codecrafters-shell-python/) | Shell implementation (CodeCrafters challenge, submodule) | Complete |

---

## C

### Status: Not Started (Exploratory)

Just an appreciation for the OG of programming languages.

### Projects

| Project            | Description                                                        | Status     |
| ------------------ | ------------------------------------------------------------------ | ---------- |
| [main.c](c/main.c) | Calculate total/average of numbers (input parsing, arrays, printf) | Experiment |
| [temp.c](c/temp.c) | —                                                                  | Experiment |

---

## Overall Language Progression

```
JavaScript / TypeScript / Node.js / React / Next.js  ← existing background (3-5 years)
          ↓
     Go (current)  ← learning deeply through projects
          ↓
     Rust (future) ← after Go fundamentals are solid
```

---

## Version History

| Date       | Change                                                      |
| ---------- | ----------------------------------------------------------- |
| 2026-08-21 | Created initial progress tracker. Go Bank at v5 (HTTP API). |
