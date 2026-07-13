# db2-chunker

> A tiny **Db2 user-defined table function**, written in C, that splits text into
> fixed-size pieces ("chunks") and returns them as table rows.

```sql
SELECT * FROM TABLE(chunk('chunking is fun to build', 10));
```

| CHUNK_INDEX | CHUNK_TEXT   |
|-------------|--------------|
| 0           | `chunking i` |
| 1           | `s fun to b` |
| 2           | `uild`       |

One string went in; three rows came out. That's a **table function** — a function
you can `SELECT ... FROM` as if it were a table.

This is **Iteration 0**: the simplest possible version, built to prove the whole
Db2 → C pipeline works end to end. The *splitting* is deliberately dumb (fixed
windows); the *plumbing* is the real lesson.

---

## Contents

- [What you'll learn](#what-youll-learn)
- [Background: what is a table function?](#background-what-is-a-table-function)
- [Prerequisites](#prerequisites)
- [Quick start](#quick-start)
- [Using the function](#using-the-function)
- [How it works](#how-it-works)
- [Project structure](#project-structure)
- [Troubleshooting](#troubleshooting)
- [Limitations (read before using)](#limitations-read-before-using)
- [Roadmap](#roadmap)
- [Further reading](#further-reading)

---

## What you'll learn

If you're new to database extensions, this repo is a compact, working example of:

- How to write a **table function in C** for Db2.
- The **lifecycle** Db2 uses to drive your code (`OPEN → FETCH → CLOSE`).
- How to keep state across calls with a **scratchpad**.
- How to **compile, deploy, and register** a shared library so SQL can call it.
- A clean way to **structure** such a project so the hard parts stay isolated.

---

## Background: what is a table function?

Most SQL functions return **one value** (`UPPER('hi')` → `'HI'`). A **table
function** returns **many rows**, so you use it inside `TABLE( ... )` in a
`FROM` clause:

```sql
SELECT * FROM TABLE(chunk('hello world', 5));
```

Db2 lets you implement one in C when plain SQL isn't enough — for example, when
the logic needs a real programming language. That power comes with a cost: Db2
calls your C function in a specific **sequence of steps**, and you have to handle
each one. Learning that sequence is the point of this project. Don't worry, it's
only five steps and we walk through them [below](#how-it-works).

---

## Prerequisites

You need a working Db2 environment. This project was built and tested on:

| Requirement | Version used here |
|-------------|-------------------|
| Db2 (LUW)   | 12.1 (11.5+ should work) |
| OS / arch   | Linux x86-64 |
| Compiler    | gcc |
| Access      | a Db2 instance you can connect to (e.g. the `db2inst1` user) |

Check your setup:

```bash
. ~/sqllib/db2profile     # load Db2 into your shell (do this in every new terminal)
db2level                  # prints your Db2 version
gcc --version             # confirms the compiler is installed
```

> 💡 **New to Db2?** `~/sqllib` is your Db2 instance's home directory.
> `db2profile` sets environment variables (like `PATH`) so the `db2` command
> works. You must "source" it (`. ~/sqllib/db2profile`) once per terminal.

---

## Quick start

From the repo directory, with the Db2 environment sourced:

```bash
# 1. Test the core logic (no database needed) — fast feedback
make test
#    -> PASS: 3 chunks

# 2. Build the shared library (compiles the C into db2chunk.so)
make lib

# 3. Copy the library into Db2's function directory
make deploy

# 4. Register the function inside a database (default: SAMPLE)
make register

# 5. Verify it returns exactly 3 rows
make check
#    -> CHECK PASS: 3 rows
```

Point it at a different database with `make register DBNAME=YOURDB`. All
environment-specific paths live at the top of the [`Makefile`](Makefile) — edit
them there if your setup differs.

That's it — the function is now live. Jump to [Using the function](#using-the-function).

---

## Using the function

Open a Db2 shell and call it:

```bash
. ~/sqllib/db2profile
db2 connect to SAMPLE
db2 "SELECT chunk_index, chunk_text
       FROM TABLE(chunk('chunking is fun to build', 10)) AS t
      ORDER BY chunk_index"
```

**Two rules for calling any table function:**

1. Wrap it in `TABLE( ... )`.
2. Give it a name with `AS` (here, `AS t`).

**It returns two columns:**

| Column        | Type    | Meaning                          |
|---------------|---------|----------------------------------|
| `chunk_index` | INTEGER | position of the chunk, 0-based   |
| `chunk_text`  | VARCHAR | the chunk's text                 |

### The useful pattern: chunk a whole column

The real power shows when you chunk text from an existing table. For **each row**
in your table, Db2 runs the function over that row's text and expands it into
many chunk-rows (this is called a *lateral join* — note the comma and how
`chunk` reads `d.body`):

```sql
SELECT d.id, c.chunk_index, c.chunk_text
  FROM mydocs AS d,
       TABLE(chunk(d.body, 100)) AS c;
```

One document per input row → many chunk rows out.

---

## How it works

Here's the key idea a beginner needs: **you write one function, but Db2 calls it
many times** for a single query — once for each step of a lifecycle.

Think of it like reading a file:

| Step      | Like a file... | Your function does...                         |
|-----------|----------------|-----------------------------------------------|
| **FIRST** | program starts | nothing (setup marker)                        |
| **OPEN**  | open the file  | split the text into all chunks, remember them |
| **FETCH** | read a line    | return the **next** chunk (called repeatedly) |
| **CLOSE** | close the file | free memory                                   |
| **FINAL** | program ends   | nothing (teardown marker)                      |

For our example query (24 characters, size 10 → 3 chunks), Db2 makes **8 calls**:

```
FIRST → OPEN → FETCH → FETCH → FETCH → FETCH → CLOSE → FINAL
               (row0)  (row1)  (row2)  (no more
                                        rows: stop)
```

Three things trip up beginners — internalize these:

1. **Do the work once, hand it out slowly.** All chunks are computed at `OPEN`.
   Each `FETCH` just returns one already-computed chunk.
2. **Your function forgets everything between calls.** Local variables vanish.
   To remember "which chunk is next," we use the **scratchpad** — a small memory
   area Db2 hands back to you on every call. (We store a pointer to our data in
   it.)
3. **There's always one extra FETCH.** Db2 keeps fetching until a `FETCH` says
   "no more rows" (by setting a special status code, `SQLSTATE 02000`). So N rows
   means N+1 fetches.

Want the deeper version? Every non-obvious design choice is explained in
[`DECISIONS.md`](DECISIONS.md).

---

## Project structure

Only six files, split into two clear halves plus the build glue:

```
The chunking logic (plain C — no database knowledge)
├── chunk_core.h      what the chunker offers (its interface)
├── chunk_core.c      the actual splitting math
└── test_core.c       a quick test you can run without Db2

The Db2 glue (all the database-specific machinery, in one place)
└── db2_chunk_udf.c   handles the OPEN/FETCH/CLOSE lifecycle

Setup
├── register.sql      tells Db2 the function exists
└── Makefile          build + deploy + register commands
```

**Why the split?** The chunking logic (`chunk_core.*`) knows nothing about Db2,
so you can test it instantly and reuse it anywhere. All the tricky database
plumbing is quarantined in `db2_chunk_udf.c`. When you improve the *chunking* in
future iterations, you touch the pure logic — not the database code.

```
chunk_core.*    = WHAT to chunk   (simple, testable, grows over time)
db2_chunk_udf.c = HOW Db2 calls it (write once, rarely reopen)
```

---

## Troubleshooting

| Symptom | Likely cause & fix |
|---------|--------------------|
| `db2: command not found` | You didn't source the environment: `. ~/sqllib/db2profile` |
| `SQL0444N` / library not found at query time | Run `make deploy` (the `.so` must be in `~/sqllib/function`) |
| Changed the C code but see old behavior | Reconnect: Db2 caches the library per connection. `db2 connect reset` then reconnect |
| `make register` says "connection does not exist" | Instance not started (`db2start`) or wrong `DBNAME` |
| Garbled output on non-English text | Expected — see [Limitations](#limitations-read-before-using). Use ASCII |

---

## Limitations (read before using)

This is a **learning spike**, not production code. On purpose, it does **not**
handle:

- ⚠️ **Non-ASCII / UTF-8 text.** Chunks are cut on raw bytes, so multi-byte
  characters can be split in half and corrupted. **Use ASCII only.**
- No overlap between chunks, no splitting on word/sentence boundaries.
- No tokenizer awareness, no very large text (CLOB) support.
- `chunk_text` is capped at `VARCHAR(4096)`, so keep `chunk_size` ≤ 4096.

These are intentional and will be addressed in later iterations.

---

## Roadmap

- **Iteration 0 (this repo):** prove the Db2 ↔ C table-function pipeline. ✅
- **Next:** smarter splitting — word/sentence boundaries, overlap, UTF-8 safety.

Because of the two-layer design, that future work lives almost entirely in
`chunk_core.c`; the Db2 lifecycle code stays as-is.

---

## Further reading

The C API here is grounded in IBM's official documentation and the Db2 header
`sqludf.h`, not guesswork:

- [Db2: External table functions](https://www.ibm.com/docs/en/db2/11.5.x?topic=features-external-table-functions)
- [Db2: PARAMETER STYLE SQL C/C++ functions](https://www.ibm.com/docs/en/db2/11.5.x?topic=functions-parameter-style-sql-c-c)

For the reasoning behind each design decision, see [`DECISIONS.md`](DECISIONS.md).
