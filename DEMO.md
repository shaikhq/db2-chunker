# Demo Script — Building & Using the `chunk` UDF

A ~15-minute live walkthrough for **application developers** who want to build
and use this Db2 table function. Each segment lists the **goal**, **key points**
to say, and the **commands** to run live.

> **Before you start:** source the Db2 environment (`. ~/sqllib/db2profile`) in
> your terminal, and pre-run `make lib` once off-camera so the first compile is
> warm. The #1 live-demo failure is `db2: command not found`.

| # | Segment | Time |
|---|---------|------|
| 1 | [Hook](#1--hook) | 1 min |
| 2 | [What we're building](#2--what-were-building--the-ground-rules) | 1 min |
| 3 | [Repo tour](#3--repo-tour) | 2 min |
| 4 | [The fast loop: test the logic](#4--the-fast-loop-test-the-logic) | 1.5 min |
| 5 | [Build → deploy → register](#5--build--deploy--register) | 3 min |
| 6 | [Use it for real](#6--use-it-for-real) | 2.5 min |
| 7 | [Peek under the hood](#7--peek-under-the-hood-optional) | 2 min |
| 8 | [Limits, reset, what's next](#8--limits-reset-and-whats-next) | 1.5 min |

---

## 1 — Hook

**Goal:** show the payoff before any theory.

Run the money shot first, no setup talk:

```sql
SELECT * FROM TABLE(chunk('chunking is fun to build', 10));
```

- *"One string in, three rows out. That's a **table function** — you
  `SELECT ... FROM` it like a table."*
- **Why care:** chunking text is step one of every RAG / search-indexing
  pipeline — and here it runs **inside the database**, with no data export.

---

## 2 — What we're building & the ground rules

**Goal:** set expectations honestly.

- This is **Iteration 0** — the **plumbing**, not smart chunking. The splitter
  is deliberately dumb (fixed-size windows).
- It's written in **C** because real chunking will need a real language later.
- Prereqs in one breath: Db2 11.5+, Linux, gcc, and
  `. ~/sqllib/db2profile` in every terminal.

---

## 3 — Repo tour

**Goal:** the mental model that makes everything else click.

Show the file list and land the **two-layer split**:

```
chunk_core.*     = WHAT to chunk    (pure C, zero Db2, testable in ms)
db2_chunk_udf.c  = HOW Db2 calls it (all the database machinery, quarantined)
register.sql + Makefile = the wiring
```

- *"The interesting work grows on the pure side; the scary Db2 side you write
  once and rarely reopen."*

---

## 4 — The fast loop: test the logic

**Goal:** prove you can iterate without a database.

```bash
make test        # -> PASS: 3 chunks
```

- This compiles and calls the **real** `chunk_fixed` — no copy, no Db2.
- This is where you'd develop chunk *quality* later: seconds, not minutes.

---

## 5 — Build → deploy → register

**Goal:** the three commands that turn C into a callable SQL function.

```bash
make lib         # compile -> db2chunk.so
make deploy      # copy into ~/sqllib/function
make register    # run register.sql -> CREATE FUNCTION
make check       # -> CHECK PASS: 3 rows
```

- **lib** — compile the shared library.
- **deploy** — Db2 loads routines from a specific directory; that's all this is.
- **register** — the bridge: `EXTERNAL NAME 'db2chunk!chunk_tf'` maps the SQL
  name to the C function.
- All environment paths live at the **top of the `Makefile`** — the only thing a
  new machine needs to edit.

---

## 6 — Use it for real

**Goal:** move from a toy string to the pattern developers actually need.

**Basic call** — two rules: wrap in `TABLE(...)`, alias with `AS`:

```sql
SELECT chunk_index, chunk_text
  FROM TABLE(chunk('chunking is fun to build', 10)) AS t
 ORDER BY chunk_index;
```

**The real pattern** — chunk a whole column via a lateral join:

```sql
SELECT d.id, c.chunk_index, c.chunk_text
  FROM mydocs AS d,
       TABLE(chunk(d.body, 100)) AS c;
```

- *"One document per input row explodes into many chunk rows — the exact shape
  you feed into an embedding/index pipeline."*
- Returns `chunk_index` (INTEGER) and `chunk_text` (VARCHAR).

---

## 7 — Peek under the hood (optional)

**Goal:** demystify "how does Db2 drive my C?" *(Skip this segment if short on time.)*

*"You write one function; Db2 calls it many times":*

```
FIRST → OPEN → FETCH×N → FETCH(stop) → CLOSE → FINAL
```

Three takeaways:

1. Compute all chunks **once** at OPEN; each FETCH hands back one.
2. The function has **no memory** between calls → the **scratchpad** carries a
   pointer to the saved state.
3. Ending is a **signal** (`SQLSTATE 02000`), not a `return` — hence one extra
   FETCH beyond the row count.

Point to [`DECISIONS.md`](DECISIONS.md) for the "why" behind every choice.

---

## 8 — Limits, reset, and what's next

**Goal:** send them off knowing the guardrails.

- ⚠️ **ASCII only** — cuts on raw bytes, so UTF-8 can be split mid-character.
- Other scope-outs: no overlap, no word boundaries, `chunk_text` capped at
  `VARCHAR(4096)`.
- **Reset demo** (they'll ask):
  ```bash
  db2 "DROP FUNCTION chunk"     # then: make register
  ```
  `CREATE OR REPLACE` means `make register` alone also refreshes it. **Reconnect
  after a redeploy** — the library is cached per connection.
- **Roadmap:** smarter splitting lands in `chunk_core.c`; the Db2 layer won't
  change.
- **Close:** *"Clone it, `make test`, and you're iterating in under a minute."*

---

## Presenter cheat-sheet

```bash
. ~/sqllib/db2profile                 # do this first, always
make test                             # PASS: 3 chunks
make lib && make deploy && make register && make check
db2 connect to SAMPLE
db2 "SELECT * FROM TABLE(chunk('chunking is fun to build', 10)) AS t"
```

**If short on time:** cut Segment 7. Segments 3–6 (tour → build → use) are the core.
