# Code Review #1 — Response (before / after)

Reviewers: **Robert Indrigo**, **Shaikh Quader**. Each item below records the
feedback, the code **before**, the code **after**, and how it was verified.

All changes verified together: `make test` green, live `SELECT` returns 3 rows
(lengths 10/10/4), edge cases correct (`abcd`/4 → 1, NULL → 0, size≤0 → 0), and
valgrind reports **0 errors / 0 bytes in use at exit** (the UDF now allocates
nothing on the heap).

---

## 1. `st_free(st)` was called twice

**Feedback:** *"in db2_chunk_udf.c, you are calling this twice: st_free(st)."*

**Before** — freed in both CLOSE and FINAL branches:
```c
case SQLUDF_TF_CLOSE: st = sp_get(...); st_free(st); sp_put(..., NULL); break;
case SQLUDF_TF_FINAL: st = sp_get(...); st_free(st); sp_put(..., NULL); break;
```

**After** — there is no heap allocation at all, so `st_free` was **deleted**
entirely; CLOSE and FINAL only trace:
```c
case SQLUDF_TF_CLOSE: TRACE("CLOSE"); break;
case SQLUDF_TF_FINAL: TRACE("FINAL"); break;
```

**Note:** superseded by items 5–8 — removing the heap removed the free.

---

## 2. Purpose of `(void)buf;`

**Feedback:** *"what is the point of this line in chunk_core.c: (void)buf;"*

It silenced an unused-parameter warning: the old core took the buffer but, for
fixed windows, only used `len`.

**Before:**
```c
chunk_desc *chunk_fixed(const char *buf, size_t len, int chunk_size, size_t *out_count) {
    (void)buf; /* fixed windows depend only on len, not content */
    ...
}
```

**After** — the core no longer takes a buffer at all, so the cast is gone:
```c
int chunk_window(size_t len, size_t chunk_size, size_t index,
                 size_t *offset, size_t *length);
```

---

## 3. Don't copy the entire input string; store an offset

**Feedback:** *"it makes a copy of the entire string ... you are not modifying
the input string, why not just store an offset."*

**Before** — `malloc`+`memcpy` the whole VARCHAR on OPEN:
```c
size_t len = strlen(in_text);
st->text = malloc(len + 1);
memcpy(st->text, in_text, len + 1);
...
memcpy(out_text, st->text + d.offset, d.length);   /* FETCH read the copy */
```

**After** — no copy; FETCH reads the per-call `in_text` directly:
```c
memcpy(out_text, in_text + off, len);   /* in_text is re-passed every call */
```

**Verified:** Db2 re-passes SQL inputs on every table-function call — a probe
that read the fresh `in_text` at FETCH returned byte-identical rows to the copy.

---

## 4. `NOT DETERMINISTIC` → `DETERMINISTIC`

**Feedback:** *"In register.sql change NOT DETERMINISTIC to DETERMINISTIC."*

**Before:** `NOT DETERMINISTIC`
**After:** `DETERMINISTIC`

Correct — the same `(text, chunk_size)` always yields the same rows — and it
lets Db2 cache/reuse results.

---

## 5. Don't pre-compute/store all offsets; store three numbers

**Feedback (Indrigo):** *"I don't see any value to pre-computing and storing all
the offsets, why not just store three numbers: the length, chunk size and
current offset?"*

**Before** — OPEN pre-computed a heap array of every `{offset, length}`:
```c
st->descs = chunk_fixed(st->text, len, (int)*in_size, &st->count);  /* malloc'd array */
...
chunk_desc d = st->descs[st->next];   /* FETCH indexed the array */
```

**After** — store three numbers; compute each window on demand:
```c
typedef struct { size_t len; size_t chunk_size; size_t next; } tf_state;
...
if (!chunk_window(s.len, s.chunk_size, s.next, &off, &len)) { /* 02000 */ }
```

(`next` is the 0-based index; `offset = next * chunk_size`, so it is equivalent
to storing the offset.)

---

## 6. Store `tf_state` directly in the scratchpad — no separate allocation

**Feedback (Indrigo):** *"tf_state can be stored directly in the scratchpad, no
need to do a separate allocation."*

**Before** — a `tf_state*` pointer in the scratchpad; the struct lived on the heap:
```c
st = calloc(1, sizeof(*st));   /* heap */
sp_put(SQLUDF_SCRAT, st);       /* store the POINTER */
```

**After** — the 24-byte struct lives **in** the scratchpad (100 bytes); no heap:
```c
memcpy(SQLUDF_SCRAT->data, &s, sizeof s);   /* OPEN writes the struct */
memcpy(&s, SQLUDF_SCRAT->data, sizeof s);   /* FETCH reads it back */
```

This removed `calloc`/`free`, the `sp_get`/`sp_put` helpers, and `st_free`.

---

## 7. `chunk_desc` isn't needed

**Feedback (Indrigo):** *"I don't think you need chunk_desc at all."*

**Before** — a struct returned as a heap array by the core:
```c
typedef struct { size_t offset; size_t length; } chunk_desc;
chunk_desc *chunk_fixed(const char *buf, size_t len, int chunk_size, size_t *out_count);
```

**After** — deleted; the core writes offset/length through out-params:
```c
int chunk_window(size_t len, size_t chunk_size, size_t index,
                 size_t *offset, size_t *length);
```

---

## 8. Proposed `tf_state` shape

**Feedback (Indrigo):**
```c
typedef struct {
    const char *text;  /* points to original string, no copy */
    size_t      chunk_size;
    size_t      offset;
} tf_state;
```

**Adopted the spirit, with one evidence-based tweak.** We store `len` instead of
a captured `text` pointer:

```c
typedef struct { size_t len; size_t chunk_size; size_t next; } tf_state;
```

**Why the tweak:** caching the OPEN-time `in_text` pointer assumes the fenced
argument buffer keeps the **same address** on later FETCH calls. A probe on Db2
12.1 showed that assumption *does* hold here (OPEN address == FETCH address,
`same=1` every call) — so the reviewer's struct would work. But that address
stability isn't documented, whereas "inputs are re-passed each call" is. Storing
`len` and reading the fresh `in_text` costs one extra `size_t` and depends only
on the documented behavior. (Storing `len` also lets FETCH detect end-of-data
without re-scanning for the string terminator.)
