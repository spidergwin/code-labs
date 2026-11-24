# 07 — Filesystem and CLI Boundary

This chapter covers the outer edge of the program: the arguments the shell hands us, the messages
we send back, and the file we persist to. Everything here uses the standard library only.

## Why this matters for Task Tracker

The roadmap.sh spec is a command-line contract: `add`, `update`, `delete`, `mark-in-progress`,
`mark-done`, and `list`, with positional arguments and clear errors. It also says the task data
lives in `tasks.json` in the **current directory** and that the file should be created when
missing. Those are three Rust boundaries at once:

1. **OS strings → text** (`argv` is not guaranteed to be UTF-8).
2. **Text → commands and IDs** (parsing, arity checks, usage text, exit codes).
3. **Commands → durable bytes** (paths, `fs`, `OpenOptions`, atomic replacement).

Get these right and the inner logic from `05-collections-and-strings.md` has a clean place to sit.
Get them wrong and you get a program that works in your crate directory but fails in a scratch
directory, prints errors to the wrong stream, or corrupts `tasks.json` on an interrupted write.

## What you will learn

- The difference between `env::args()` and `env::args_os()`, and how to collect into `Vec<String>`
- Why `argv[0]` is skipped and how to dispatch with `args.get(1).map(String::as_str)`
- How to check arity, write usage/help text, and parse IDs with `str::parse::<i32>()`
- How shell quoting and empty strings affect what the program actually receives
- When to use `println!` versus `eprintln!`, and why stdout/stderr are separate channels
- Why `main() -> ExitCode` is safer than `process::exit` (destructors and drop)
- How `current_dir()` differs from the binary's directory, and how to reason about relative paths
- How to use `Path`, `PathBuf`, `join`, `fs::read_to_string`, `fs::write`, `File`, and `OpenOptions`
- How to inspect `metadata`, match `io::ErrorKind::NotFound`, and create a missing file
- How to implement an atomic file replacement with **no tempfile crate**, plus `#[cfg(unix)]` permissions
- How to run repeatable smoke tests in a scratch directory

---

## 1. Arguments: what the shell actually gives you

**`argv`** is the array of command-line argument strings passed by the operating system to the
program. In Rust, `std::env::args()` is an iterator over those strings as `String`s.

> **Jargon.** An **iterator** is a value that yields items one at a time when you call `next()`
> or use a loop. **UTF-8** is the text encoding Rust `String` requires; arbitrary OS bytes may not
> be valid UTF-8.

```rust
use std::env;

fn main() {
    let args: Vec<String> = env::args().collect();
    println!("argv[0] = {}", args.first().map(String::as_str).unwrap_or("<none>"));
    println!("user args = {}", args.len().saturating_sub(1));
}
```

`argv[0]` is conventionally the program name. It is metadata, not a command. The user's input
starts at index 1, which is why we **skip argv[0]** and look at `args.get(1)`.

### `args()` vs `args_os()`

| API | Item type | Behavior on non-UTF-8 input | Use when |
|---|---|---|---|
| `env::args()` | `String` | **Panics** | You accept only text arguments |
| `env::args_os()` | `OsString` | Preserves raw bytes | File paths from arbitrary sources |

`OsString::to_string_lossy()` substitutes a replacement character for invalid bytes;
`OsString::into_string()` instead returns `Result<String, OsString>`, so you can reject bad input
deliberately. Our CLI deals in human-written command words and descriptions, so collecting
`env::args()` into `Vec<String>` is a reasonable, documented choice — just know that it is a choice.

### Positional dispatch with a `match` on `Option<&str>`

```rust
use std::env;

fn main() {
    let args: Vec<String> = env::args().collect();

    match args.get(1).map(String::as_str) {
        Some("hello") if args.len() == 2 => println!("Hello!"),
        Some("hello") => eprintln!("usage: demo hello"),
        Some("echo") if args.len() == 3 => println!("you said: {}", args[2]),
        Some("echo") => eprintln!("usage: demo echo <text>"),
        Some(other) => eprintln!("unknown command: {other}"),
        None => eprintln!("usage: demo <hello|echo> [text]"),
    }
}
```

`args.get(1)` returns `Option<&String>`; `.map(String::as_str)` turns that into `Option<&str>` so
the `match` arms compare string slices without cloning. The **match guards** (`if args.len() == 2`)
perform the **arity check** — the check that the number of arguments is correct. Note that we
distinguish "nothing supplied" (`None`) from "something unrecognised" (`Some(other)`).

For `demo echo "Buy groceries"` it prints `you said: Buy groceries`; `demo hello extra` prints
`usage: demo hello`, and `demo frobnicate` prints `unknown command: frobnicate`.

> **Professional habit.** Write the usage text *before* you write the handler. It forces you to
> decide the arity and the failure messages as part of the design, not as an afterthought.

### Parsing IDs with `str::parse::<i32>()`

**Parsing** means converting text into a typed value. `str::parse::<T>()` works for any `T` that
implements `FromStr` (for example integers, `bool`, and `PathBuf`).

```rust
fn check_id(raw: &str) {
    match raw.parse::<i32>() {
        Ok(id) if id >= 1 => println!("id = {id}"),
        Ok(_) => eprintln!("id must be a positive integer"),
        Err(err) => eprintln!("not a number: {err}"),
    }
}
```

Calling `check_id("42")` prints `id = 42`; `"0"` reports a range error; `"4.2"` reports an
invalid digit. `"42 ".parse::<i32>()` fails: leading or trailing whitespace is not ignored. Decide whether to
`trim()` before parsing and apply that rule consistently. A parse success is not the same as a
valid domain value — `0` and negative numbers parse fine but are not useful IDs, so add the range
check yourself.

### Shell quoting and empty strings

The shell removes quote characters **before** the program sees anything. Quotes group arguments;
they are not delivered to `argv`.

| What you type | `argv` the program sees |
|---|---|
| `demo echo Buy groceries` | `["demo", "echo", "Buy", "groceries"]` |
| `demo echo "Buy groceries"` | `["demo", "echo", "Buy groceries"]` |
| `demo echo ""` | `["demo", "echo", ""]` |
| `demo echo 'a b'` | `["demo", "echo", "a b"]` |

An empty string is still an argument: `args.get(2)` is `Some("")`, **not** `None`. If an empty
description should be rejected, say so explicitly in validation. Single versus double quotes also
change how the shell expands `$variables`; that is shell behavior, not Rust behavior.

---

## 2. Output: stdout, stderr, and exit codes

**stdout** (standard output) is the channel for the program's useful results; **stderr** (standard
error) is the channel for diagnostics. `println!` writes to stdout, `eprintln!` writes to stderr.
Because they are separate file descriptors, a caller can redirect one without the other:

```bash
demo list > out.txt          # results to a file; errors still appear in the terminal
demo list 2> err.txt         # errors to a file; results still appear in the terminal
demo list > out.txt 2> err.txt
```

Error messages on stderr mean `demo list > tasks.txt` does not silently write "error: corrupt file"
into your data file. Keep stdout machine-friendly and stderr human-friendly.

### Returning `ExitCode` instead of calling `process::exit`

**Exit code** is the small integer a process returns to its parent: `0` means success by
convention, `1` is a generic failure, and `2` is commonly used for usage errors.

```rust
use std::process::ExitCode;

fn run() -> Result<(), String> {
    Err("bad argument".to_string())
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(message) => {
            eprintln!("error: {message}");
            ExitCode::from(2)
        }
    }
}
```

`ExitCode::from` takes a `u8`, and Rust runs all destructors as `main` returns. By contrast,
`std::process::exit(2)` terminates immediately:

```rust
struct Loud;
impl Drop for Loud {
    fn drop(&mut self) { eprintln!("dropped"); }
}
fn main() {
    let _guard = Loud;
    std::process::exit(2); // "dropped" is never printed
}
```

With `main() -> ExitCode`, `"dropped"` would print; with `process::exit`, it does not. Skipped
destructors mean unflushed buffers, unclosed files, and skipped cleanup guards. Use `process::exit`
only for unrecoverable situations where you intentionally want no unwinding — never as ordinary
error handling. See `04-errors-and-validation.md` for designing the error values themselves.

Decide your exit-code policy once and write it in the README: `0` success, `2` bad arguments,
`1` everything else.

---

## 3. Paths and the current working directory

**cwd** (current working directory) is the directory the shell is "in" when it launches the
process. **Relative paths** such as `tasks.json` are resolved against the cwd, not against the
directory containing the compiled binary and not against `src/`.

```rust
use std::env;

match env::current_dir() {
    Ok(dir) => println!("cwd: {}", dir.display()),
    Err(err) => eprintln!("cannot read cwd: {err}"),
}
```

`current_dir()` returns `io::Result<PathBuf>` because the cwd can have been deleted. `display()`
is for human-readable output; it may replace non-UTF-8 bytes with replacements.

`env::current_exe()` returns the path to the running binary. Do **not** use it to locate task data:
the requirement says the file belongs in the *current directory*. Cargo makes this easy to forget,
because `cargo run` from the crate root uses that root as cwd. Test from a scratch directory
instead:

```bash
mkdir -p /tmp/opencode/task-tracker-smoke
cd /tmp/opencode/task-tracker-smoke
cargo run --manifest-path /home/eliot/projects/code-labs/rust/starter-project/Cargo.toml -- add "Buy groceries"
ls -l tasks.json
```

### `Path` and `PathBuf`

`Path` is a borrowed path slice (like `&str` for text); `PathBuf` is its owned counterpart (like
`String`). You generally pass `&Path` to functions and store `PathBuf`.

```rust
use std::path::{Path, PathBuf};

let dir = Path::new("/tmp/opencode/smoke");
let file: PathBuf = dir.join("notes.json");
println!("{}", file.display()); // /tmp/opencode/smoke/notes.json
```

`join` appends a component and inserts a separator as needed. If the argument is **absolute**, it
replaces the whole path: `Path::new("/a/b").join("/etc/x")` is `/etc/x`. Useful accessors include
`parent()`, `file_name()`, `extension()`, and `exists()`. Prefer handling the error from an
operation over calling `exists()` first: a file can disappear between the check and the open
(a **race condition**, sometimes called TOCTOU — "time of check to time of use").

---

## 4. Reading and writing files

```rust
use std::fs::{self, File, OpenOptions};
use std::io::{self, Read, Write};

fn load() -> io::Result<String> {
    match fs::read_to_string("notes.txt") {
        Ok(text) => Ok(text),
        Err(err) if err.kind() == io::ErrorKind::NotFound => Ok(String::new()),
        Err(err) => Err(err),
    }
}

fn save_quickly(text: &str) -> io::Result<()> {
    fs::write("notes.txt", text) // creates or truncates: simple, but not atomic
}
```

`fs::read_to_string` fails with `ErrorKind::NotFound` when the path is absent, and with
`ErrorKind::InvalidData` when the bytes are not valid UTF-8. **Never collapse those two into one
case**: a corrupt file deserves a different message from a first run. `fs::write` creates or
truncates in one call; it is convenient for scratch files but unsafe for your only copy of data.

**`File`** is an open file handle. Use `OpenOptions` when you need precise flags:

```rust
fn read_with_flags() -> io::Result<String> {
    let mut file: File = OpenOptions::new().read(true).open("notes.txt")?;
    let mut buffer = String::new();
    file.read_to_string(&mut buffer)?;
    Ok(buffer)
}
```

Flags you will meet: `read(true)`, `write(true)`, `create(true)`, `create_new(true)` (fails if the
file exists), `truncate(true)`, and `append(true)`. `create_new` is important for temp files
because it guarantees you are the creator.

### `metadata` and "is this a file?"

`fs::metadata("notes.txt")` returns a value with `is_file()` (false for directories) and `len()`;
it follows symlinks, while `fs::symlink_metadata` describes the link itself. Use metadata for
diagnostics and validation, not as a substitute for handling `NotFound` from the read itself.

### Creating a missing data file

Try to read the target path; on `NotFound`, start from an empty collection and let the first save
create the file; on any other error, stop and report it. If you create eagerly, use
`OpenOptions::new().write(true).create_new(true)` so you do not overwrite a file that appeared in
the meantime.

> **Professional habit.** Treat "file missing" as an expected state, not an error path you
> discover by accident. Write down the state machine ("missing → empty list → first save creates")
> before coding it.

---

## 5. Atomic replacement without a tempfile crate

**Atomic** means an observer sees either the complete old file or the complete new file, never a
half-written one. Writing directly to `tasks.json` risks leaving truncated JSON if the process is
killed. The std-only pattern is: write a differently named file **in the same directory**, flush
it, close it, then rename it over the target. A rename within one directory is an atomic operation
on the same filesystem.

We need a unique temp name because tests and processes can run concurrently. Combine the process
ID, a monotonic atomic counter, and nanoseconds since the Unix epoch. **Atomic counter** here means
an integer whose `fetch_add` is synchronised across threads (`std::sync::atomic::AtomicU64`).

```rust
use std::fs::{self, OpenOptions};
use std::io::{self, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

static NONCE: AtomicU64 = AtomicU64::new(0);

fn temp_path(target: &Path) -> PathBuf {
    let stem = target.file_name().map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_else(|| "data".to_string());
    let nanos = SystemTime::now().duration_since(UNIX_EPOCH)
        .map(|d| d.as_nanos()).unwrap_or(0);
    let sequence = NONCE.fetch_add(1, Ordering::Relaxed);
    let name = format!(".{stem}.{}.{nanos}.{sequence}.tmp", std::process::id());

    target.parent().unwrap_or_else(|| Path::new(".")).join(name)
}

fn write_atomically(target: &Path, bytes: &[u8]) -> io::Result<()> {
    let temp = temp_path(target);
    let written = (|| -> io::Result<()> {
        let mut file = OpenOptions::new().write(true).create_new(true).open(&temp)?;
        file.write_all(bytes)?;
        file.sync_all()?; // push bytes to disk before the rename
        Ok(())
    })(); // `file` is dropped here, closing it before the rename
    if let Err(err) = written {
        let _ = fs::remove_file(&temp); // best effort; temp is ours
        return Err(err);
    }

    if let Err(err) = fs::rename(&temp, target) {
        let _ = fs::remove_file(&temp);
        return Err(err);
    }

    Ok(())
}
```

Why each piece matters:

| Step | Reason |
|---|---|
| Same parent directory | Guarantees the same filesystem, so `rename` is atomic and fast |
| `create_new(true)` | Never clobbers another process's temp file |
| `write_all` | `write` may write fewer bytes than requested |
| `sync_all` | Flushes OS buffers before the durable rename |
| Scope/drop | Some platforms dislike renaming an open file |
| `fs::rename` | Replaces the target in one indivisible step on Unix |
| Cleanup on failure | Avoids leaving debris after an error |

If the process dies before the rename, the original file is intact and a stray `.tmp` remains —
harmless, and you may choose to ignore or sweep such files on startup. Replacement semantics are
platform-specific: this project targets Linux, where `rename` replaces an existing file as
expected. Check the local Rust docs with `rustup doc --std` under `std::fs::rename` if you later
target Windows, because its behavior depends on the Windows version and filesystem. For maximum durability you can additionally sync the parent directory,
but that needs platform-specific code and is beyond the minimum requirement.

### Unix permissions (optional)

```rust
#[cfg(unix)]
fn open_temp_private(path: &Path) -> io::Result<fs::File> {
    use std::os::unix::fs::OpenOptionsExt;
    OpenOptions::new().write(true).create_new(true).mode(0o600).open(path)
}
```

**`cfg(unix)`** conditionally compiles the item only on Unix-like systems. `OpenOptionsExt` is a
Unix-specific extension trait; `mode(0o600)` requests owner-only access, still masked by the
process's **umask**. If you use it, provide a `#[cfg(not(unix))]` counterpart so the crate still
compiles everywhere.

---

## 6. Common mistakes

| Mistake | Symptom | Fix |
|---|---|---|
| Using `args[1]` without checking | Panic on a bare `task-cli` invocation | `args.get(1).map(String::as_str)` |
| Treating `""` as missing | Empty description accepted silently | Validate empty strings explicitly |
| Printing errors with `println!` | Errors vanish into redirected stdout | Use `eprintln!` |
| `process::exit(1)` in every error branch | Cleanup guards never run | Return `ExitCode` from `main` |
| Assuming `tasks.json` sits next to the binary | Works under `cargo run`, fails elsewhere | Use paths relative to cwd; test in a scratch dir |
| `fs::write` directly to the data file | Truncated JSON after interruption | Temp file in same dir + `rename` |
| Collapsing `NotFound` and `InvalidData` | "Corrupt file" reported on first run | Match `io::ErrorKind` precisely |
| Temp file in `/tmp` renamed onto a cwd file | "Cross-device link" error | Create temp in the target's parent |

> **Professional habit.** Smoke-test from a scratch directory with an empty `tasks.json` scenario,
> a corrupt file scenario, and a bad-argument scenario. Three manual runs catch most boundary bugs
> before any unit test is written. See `10-testing-and-debugging.md` for automating them.

---

## 7. Apply it to Task Tracker

Answer these on paper or in your build journal. Do **not** copy code from this chapter into the
real crate; write your own.

- [ ] Draw the arity table: for each command, how many `argv` entries are required and optional?
- [ ] How will your `match` distinguish "no command", "unknown command", and "known command with
      wrong arity"?
- [ ] What should `task-cli update abc` report? Which stream, and which exit code?
- [ ] What happens in your design when the user passes `task-cli add ""`, or when you launch from
      `/tmp/opencode/task-tracker-smoke`? How will you prove the path behavior experimentally?
- [ ] Which `io::ErrorKind` values can a read of `tasks.json` produce? Which one means "first run"?
- [ ] Sketch the full atomic-write sequence, including cleanup at each failure point, and note
      where you will document the exit-code policy.

---

## Checkpoint

- [ ] I can explain why `argv[0]` is skipped and what `args.get(1).map(String::as_str)` returns.
- [ ] I can distinguish `args()` from `args_os()` and say when each would panic or preserve bytes.
- [ ] I can write a `match` with arity guards and two different usage messages.
- [ ] I can parse `"42"`, explain why `"0"` needs a separate check, and predict `argv` for quoted
      and empty arguments.
- [ ] I can explain why errors go to stderr and why `ExitCode` is safer than `process::exit`.
- [ ] I can state the cwd-vs-binary-directory rule and verify it in a scratch directory.
- [ ] I can build a `PathBuf` with `join`, read a file, and match `ErrorKind::NotFound`.
- [ ] I can list the steps of the atomic write pattern and justify each one.

**Next:** `08-time-without-chrono.md` — produce the `createdAt` and `updatedAt` strings using only
`std::time`.
