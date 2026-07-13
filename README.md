# db2-chunker — Iteration 0

A Db2 external **table function** that splits VARCHAR text into fixed-size
character windows and returns them as rows.

```sql
SELECT * FROM TABLE(chunk('chunking is fun to build', 10))
-- 0 | chunking i
-- 1 | s fun to b
-- 2 | uild
```

The point of this iteration is **the pipe, not the chunker**: prove the Db2 C
table-function lifecycle works end to end with the dumbest possible splitter.
Chunk *quality* comes in later iterations; the architecture here is built so
that only the pure-algorithm file changes when it does.

Target: **Db2 v12.1 LUW, Linux x86-64, gcc.** The C API is grounded against the
installed header `/opt/ibm/db2/V12.1/include/sqludf.h`.

---

## Components

Six files, in two layers plus wiring:

```
THE ALGORITHM  (plain C, zero Db2 — the side that grows)
  chunk_core.h      interface: declares chunk_fixed()
  chunk_core.c      the math: text + size -> array of {offset, length}
  test_core.c       fast-loop test; calls chunk_fixed directly, no database

THE DB2 GLUE   (all the lifecycle mechanics, quarantined to one file)
  db2_chunk_udf.c   chunk_tf(): the entry point Db2 calls

THE WIRING
  register.sql      CREATE FUNCTION; maps the SQL name to chunk_tf
  Makefile          build / deploy / register / check
```

The one split to hold onto:

```
chunk_core.*     = WHAT to chunk       (pure, reusable, grows over time)
db2_chunk_udf.c  = HOW Db2 calls you   (write once, rarely reopen)
```

`chunk_core.c` includes only `<stdlib.h>`. `db2_chunk_udf.c` includes both
`<sqludf.h>` and `"chunk_core.h"` — it is the only file that spans both worlds.

### The runtime pieces inside `db2_chunk_udf.c`

| Piece | Role |
|-------|------|
| `chunk_tf` | the single function Db2 calls; a `switch` on the lifecycle phase |
| scratchpad | 100-byte buffer Db2 preserves between calls; holds a **pointer** |
| `tf_state` (heap) | the real data behind that pointer: text copy, chunk list, `next` |
| `sp_get` / `sp_put` | carry the `tf_state*` in and out of the scratchpad |
| `st_free` | free the heap state (alloc on OPEN, free on CLOSE) |

Db2 calls `chunk_tf` **many times per query** — once per lifecycle phase — and
the function has no memory between calls except the scratchpad. That single
fact explains every non-trivial line: the pointer-in-scratchpad, and the
allocate-on-OPEN / free-on-CLOSE discipline.

---

## Execution workflow (one query)

Db2 owns the loop. For `chunk('chunking is fun to build', 10)` (24 bytes,
window 10 => 3 chunks), it calls `chunk_tf` **8 times**:

```
PHASE     what chunk_tf does                  scratchpad -> heap (tf_state)
──────────────────────────────────────────────────────────────────────────
FIRST     nothing (statement start)           empty
OPEN      copy text; chunk_fixed() computes    ptr -> { text, descs:[{0,10}
          ALL chunks ONCE; store pointer                 {10,10}{20,4}],
                                                         count:3, next:0 }
FETCH     emit descs[next]; next++            next 0->1   row (0,"chunking i")
FETCH     emit descs[next]; next++            next 1->2   row (1,"s fun to b")
FETCH     emit descs[next]; next++            next 2->3   row (2,"uild")
FETCH     next(3) >= count(3) -> SQLSTATE      (unchanged) "no more rows"
          02000, no row
CLOSE     free heap; clear pointer            empty
FINAL     nothing (statement end)             empty
```

Three ideas make this click:

1. **Compute once, dole out many.** `chunk_fixed` runs a single time, on OPEN.
   Every FETCH just returns the next pre-computed descriptor.
2. **`next` is the machine's whole memory.** It survives across calls only
   because the scratchpad carries the pointer to the heap state.
3. **Ending is a signal, not a `return`.** FETCH stops the loop by setting
   SQLSTATE `02000`; Db2 keeps fetching until told to stop — hence one more
   FETCH than there are rows. General shape: **N rows => N + 5 calls.**

The lifecycle phases (`FIRST/OPEN/FETCH/CLOSE/FINAL`) and the `02000` end signal
are **Db2's contract**, not choices made here. FIRST/FINAL bracket the whole
statement; OPEN/CLOSE bracket one scan (a table function can be re-scanned,
e.g. inside a join) — which is why state is allocated per-OPEN and freed
per-CLOSE.

---

## Build & deploy workflow

Every environment-specific value lives at the top of the `Makefile`
(`DB2PATH`, `INSTHOME`, `FUNCDIR`, `DBNAME`, ...). Source the Db2 environment
first: `. ~/sqllib/db2profile`.

```
make test      # build + run the pure-core test (no database)      -> "PASS: 3 chunks"
make lib       # compile chunk_core.c + db2_chunk_udf.c -> db2chunk.so
make deploy    # copy db2chunk.so into $FUNCDIR/db2chunk
make register  # run register.sql against $DBNAME (creates the function)
make check     # assert the smoke test returns exactly 3 rows       -> "CHECK PASS: 3 rows"
```

```
write C ──► make lib ──► make deploy ──► make register ──► SELECT works
(core+      (db2chunk.so)  (into sqllib   (EXTERNAL NAME    (Db2 calls
 adapter)                   /function)     'db2chunk!chunk_tf') chunk_tf/query)
```

`make lib` compiles with `-DCHUNK_DEBUG` (via `DEBUG=1`), which emits a
per-phase trace to stderr. Note: under `FENCED`, the fenced process's stderr is
`/dev/null`, so the live trace is discarded by design — lifecycle order was
verified separately (see `DECISIONS.md`).

---

## Scope

**In:** VARCHAR input, INTEGER chunk size; fixed non-overlapping windows cut on
byte offsets; output `(chunk_index, chunk_text)`; FENCED, SCRATCHPAD, FINAL CALL.

**Out (deferred):** overlap, separators, recursion, tokenizers, UTF-8/MBCS
boundary safety, offsets, CLOB. **Cutting is on raw bytes — multibyte input is
unsafe here. Test with ASCII only.**

See `DECISIONS.md` for the reasoning behind each non-obvious choice.
