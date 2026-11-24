# Task Tracker in Go — Self-Contained Learning Path

> **Purpose.** This is your personal tutor, reference manual, and build journal for the
> Task Tracker CLI project and its optional HTTP API. It is deliberately complete: once you
> have read the relevant chapters, you should never need to search the internet to understand
> a Go concept used by this project. Everything you need is in this folder, plus the offline
> tools that ship with Go itself (`go doc`, `go help`, and the standard-library source code).
>
> **Important boundary.** These documents teach you *how* to build the project. They do
> **not** contain a finished implementation of Task Tracker or its API. You will write that
> yourself, one feature at a time, using the checkpoints and exercises here.
>
> **Two tracks.** Chapters `00`–`11` build the stdlib-only CLI required by `Requirements.txt`.
> Chapters `12`–`13` are an optional Phase 2 that adds an HTTP API using `net/http` and chi.
> Finish the CLI first; the API reuses the same model, service, and JSON store.

---

## 1. Ground rules

1. **Type the code yourself.** Copying a full solution teaches nothing. Short illustrative
   snippets in these notes are for learning isolated ideas; retype them, run them, break
   them, fix them.
2. **Use the offline reference, not the web.** When you want to know what a standard-library
   package offers, run:
   ```bash
   go doc <package>            # e.g. go doc encoding/json
   go doc <package>.<Symbol>   # e.g. go doc os.WriteFile
   go help <command>           # e.g. go help test
   ```
   The full source of the standard library is in `$(go env GOROOT)/src`, which you can read
   locally. That is a legitimate primary reference and requires no internet.
3. **One feature at a time.** Do not implement add, update, delete, and status changes in a
   single sitting. Follow `08-feature-by-feature.md` for the CLI, then
   `13-api-feature-by-feature.md` for the API, and test after each step.
4. **Keep a build journal.** After each checkpoint, write 3–5 lines in a file of your choice
   (for example `docs/MY_NOTES.md`): what you implemented, what broke, how you fixed it, and
   one thing you learned. This turns mistakes into durable knowledge.
5. **Professional habits from day one.** `gofmt` on every save, `go vet` before every commit,
   errors checked at every boundary, small functions, clear names, and tests for behavior
   that matters. See `10-professional-habits.md`.

---

## 2. How this project maps to Go concepts

| Requirement in `Requirements.txt` | Go concept you must learn | Chapter(s) |
|---|---|---|
| Runs from the command line, positional arguments | `os.Args`, argument validation, `flag` (optional), stdout/stderr, exit codes | `05`, `00` |
| Store tasks in a JSON file in the current directory | `os`, `path/filepath`, `encoding/json`, file modes | `04` |
| JSON file created if missing | `os.Stat`, `errors.Is(err, os.ErrNotExist)`, `os.WriteFile` | `03`, `04` |
| Add / update / delete tasks | structs, slices, maps, pointers, methods, CRUD logic | `02`, `07`, `08` |
| Mark in-progress / done | custom types, constants, validation, immutability | `02`, `03`, `07` |
| List all / filtered by status | slices, `range`, filtering, formatting output | `01`, `02`, `08` |
| id, description, status, createdAt, updatedAt | struct tags, `time.Time`, RFC3339, JSON field names | `02`, `06`, `04` |
| Handle errors and edge cases gracefully | `error`, wrapping, sentinels, user-facing messages | `03`, `09` |
| No external libraries (CLI track) | standard library only, module hygiene | `00`, `07` |
| Clean code, README, tests | `go test`, table-driven tests, `t.TempDir`, `go vet` | `09`, `10`, `08` |
| Optional HTTP API (Phase 2) | `net/http`, chi, DTOs, middleware, `sync.RWMutex`, `httptest`, JSON status mapping | `12`, `13` |

---

## 3. The chapters, in order

Work through them in this order. Each chapter ends with a checkpoint; do not move on until
you can answer it without scrolling back.

| # | File | What you will be able to do | Approx. effort |
|---|---|---|---|
| 00 | `00-environment-and-tooling.md` | Set up the toolchain, understand modules, build/run/test, use `go doc` offline | 1–2 h |
| 01 | `01-go-basics.md` | Write complete Go programs: packages, imports, variables, types, functions, control flow | 2–3 h |
| 02 | `02-collections-and-data-modeling.md` | Model a task with structs, slices, maps, pointers, methods, and JSON tags | 2–3 h |
| 03 | `03-errors-and-validation.md` | Design and handle errors instead of panicking; validate CLI input | 2 h |
| 04 | `04-json-and-filesystem.md` | Read, write, and safely replace JSON files using only `os` and `encoding/json` | 2–3 h |
| 05 | `05-cli-arguments-and-ux.md` | Parse positional commands, print helpful usage, choose exit codes | 1–2 h |
| 06 | `06-time-and-timestamps.md` | Generate and parse RFC3339 timestamps with `time.Time` | 1 h |
| 07 | `07-architecture-and-project-layout.md` | Organize `cmd/`, `internal/`, and packages so the design stays clean | 2 h |
| 08 | `08-feature-by-feature.md` | Implement the whole CLI in small, tested increments | 6–10 h (spread out) |
| 09 | `09-testing-and-debugging.md` | Write unit tests with `go test`, use temp dirs, debug systematically | 2–3 h |
| 10 | `10-professional-habits.md` | Apply naming, formatting, error, performance, and documentation standards | 1–2 h |
| 11 | `11-troubleshooting-and-cheatsheet.md` | Diagnose compiler/runtime errors and look up syntax quickly | reference |
| 12 | `12-http-api-with-chi.md` | Explain HTTP from first principles, then route and test JSON APIs with `net/http` and chi | 3–5 h |
| 13 | `13-api-feature-by-feature.md` | Add the API in small tested phases without rewriting the CLI core | 5–8 h (spread out) |

Total for the CLI track: roughly 25–35 focused hours, depending on how much Go you retain.
Add roughly 8–13 hours for the optional API track. That is normal. Re-read a chapter whenever
a concept feels fuzzy; repetition is how it sticks.

---

## 4. Suggested weekly rhythm

- **Session A (learn, ~90 min).** Read one chapter. Retype every snippet into a scratch file
  under `/tmp/go-scratch` (create it). Run it. Change one thing and predict the output.
- **Session B (apply, ~90 min).** Do the chapter's "Apply it to Task Tracker" exercise in the
  real project. Commit only when the checkpoint passes.
- **Session C (consolidate, ~30 min).** Re-read your notes, run `gofmt -l .`, `go vet ./...`,
  and `go test ./...`. Fix anything reported.

If you only have 30 minutes, do Session A anyway. Consistency beats intensity.

---

## 5. Definition of done

### CLI track

You are finished with the required project when all of the following are true:

- [ ] `go run ./cmd/cli add "Buy groceries"` prints `Task added successfully (ID: 1)`.
- [ ] `tasks.json` appears in the current directory with all five required properties.
- [ ] `update`, `delete`, `mark-in-progress`, and `mark-done` work and update `updatedAt`.
- [ ] `list`, `list todo`, `list in-progress`, and `list done` produce correct subsets.
- [ ] Missing files, corrupt JSON, unknown commands, missing/extra arguments, non-numeric
      IDs, unknown IDs, and invalid statuses produce clear errors and non-zero exit codes.
- [ ] Repeating an operation never corrupts the JSON file (atomic replace).
- [ ] `gofmt -l .` prints nothing, `go vet ./...` is clean, `go test ./...` passes.
- [ ] A `README.md` explains installation, every command, the JSON schema, and error cases.
- [ ] You can explain *why* each line of your code exists, out loud, without notes.

The last bullet is the real goal. Everything else is a side effect.

### Optional API track

After chapters `12`–`13`, add these checks:

- [ ] `go run ./cmd/api` starts a server on a known address and `GET /healthz` returns JSON.
- [ ] `POST`, `GET`, `PATCH`, and `DELETE` routes cover the same behavior as the CLI.
- [ ] `GET /tasks?status=todo` and the other status filters return correct subsets.
- [ ] Malformed JSON, unknown fields, invalid IDs, invalid statuses, unknown IDs, unknown
      routes, and wrong methods produce consistent JSON errors with correct status codes.
- [ ] Two concurrent requests cannot corrupt `tasks.json`; writes are serialized and atomic.
- [ ] Handler tests use `httptest` and do not require a live port.
- [ ] `gofmt -l .`, `go vet ./...`, and `go test ./...` still pass.
- [ ] The README documents how to run both binaries, the route map, and example requests.
- [ ] You can explain why the API handlers are thin and where shared rules still live.

---

## 6. Local reference map (no internet required)

```text
go help build          # how go build works
go help run            # how go run works
go help test           # test flags and behavior
go help modules        # module system
go doc fmt             # formatting functions
go doc os              # filesystem, args, exit
go doc os/exec         # not needed for this project
go doc encoding/json   # Marshal, Unmarshal, tags
go doc time            # Now, Format, Parse, RFC3339
go doc errors          # New, Is, As, Unwrap
go doc path/filepath   # Join, Abs, Ext
go doc testing         # testing.T helpers
go doc flag            # optional argument parsing
go doc net/http        # Request, ResponseWriter, Handler, Server
go doc net/http/httptest # recorders and in-process test servers
go doc encoding/json.Decoder # streaming decode and unknown-field rejection
go doc sync            # RWMutex for shared API state
go doc context         # request cancellation and deadlines
```

After adding chi, the dependency source is local too:

```bash
go get github.com/go-chi/chi/v5
go doc github.com/go-chi/chi/v5
go doc github.com/go-chi/chi/v5/middleware
```

Whenever a chapter mentions a function you have not used before, run its `go doc` entry
*before* reading on. That habit alone will make you fluent faster than any tutorial.

---

## 7. A note on the existing repository

The repository already contains this layout:

```text
go/task_tracker/
├── go.mod
├── cmd/
│   ├── cli/main.go      # entry point for the CLI binary
│   └── api/main.go      # thin HTTP entry point for the optional Phase 2 API
├── internal/
│   ├── task/            # model.go, repository.go, service.go (empty)
│   ├── storage/
│   │   ├── jsonstore/   # store.go (empty)
│   │   └── sqlitestore/ # store.go (empty; ignore for this project)
│   └── transport/
│       ├── cli/         # handler.go (CLI transport)
│       └── api/         # handler.go, middleware.go (HTTP transport; Phase 2)
└── docs/
    ├── Requirements.txt
    └── LEARNING_PATH.md + chapters 00–13
```

`internal/` means "only importable from within this module" — a Go visibility rule you will
learn in `07`. `cmd/` holds binaries. Empty files are intentional: they are slots for *you*
to fill. Do not delete them; grow into them as `08-feature-by-feature.md` directs.

For the CLI track you will use only the JSON store, and the SQLite folder stays empty:
`database/sql` needs a driver, which would be an external dependency beyond this project's
requirements. In Phase 2, chapters `12`–`13` grow the API folders into the HTTP transport.
The API still shares the same JSON store and service rules.

---

## 8. Final encouragement

You are not "bad at Go"; you are rebuilding a mental model. Every professional Go developer
was once confused by pointers, slices, and interfaces. The difference is that they kept
writing small programs and reading the docs locally. Do the same, and by the time you finish
the CLI and the optional API you will have two front ends over one clean core *and* a reusable
understanding of Go.

Open `00-environment-and-tooling.md` and begin. Come back to `12-http-api-with-chi.md`
after the CLI checkpoint passes.
