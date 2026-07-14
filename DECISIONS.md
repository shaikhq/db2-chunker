# Decisions — Iteration 0 (fixed-window chunking table function)

Target: Db2 v12.1.5.0 LUW, Linux x86-64, gcc. API grounded against the
installed header `/opt/ibm/db2/V12.1/include/sqludf.h`, not memory.

## Core / adapter split
- **Decision:** `chunk_core.{h,c}` is plain C with zero Db2 headers; the adapter
  `db2_chunk_udf.c` owns all lifecycle/scratchpad/SQL concerns.
- **Alternatives:** one file that does splitting and row emission together.
- **Reason:** the core is unit-testable in the fast loop (`make test`) without a
  database; the pipe and the algorithm evolve independently.

## Compute windows on demand; store three numbers in the scratchpad
- **Decision:** no descriptor array and no heap. OPEN records
  `{len, chunk_size, next}` directly in `scratchpad.data[]`; each FETCH computes
  the next window with `chunk_window()` and increments `next`.
- **Alternatives:** precompute an array of `{offset, length}` on OPEN, stash a
  heap pointer in the scratchpad, free on CLOSE (the original design).
- **Reason:** for fixed windows the offset is `index * chunk_size` — O(1) to
  compute, nothing to store but the cursor. Three `size_t` (24 bytes) fit in the
  100-byte scratchpad, so there is no allocation, no pointer, no free, and no
  leak surface. (Review feedback: Indrigo.)
- **Forward note:** a future data-dependent splitter (separators, tokenizers)
  whose boundaries are *not* a closed-form function of the index will need to
  compute a variable-length list once and store it — reintroducing the
  heap-pointer-in-scratchpad + free-on-CLOSE pattern removed here as YAGNI.

## Read the input directly at FETCH (no copy, no stored pointer)
- **Decision:** store `len` (not a text pointer); FETCH emits from the per-call
  `in_text`: `memcpy(out_text, in_text + off, len)`.
- **Alternatives:** (a) copy the whole VARCHAR on OPEN (original approach);
  (b) cache the OPEN-time `in_text` pointer in the scratchpad (reviewer's
  proposed struct).
- **Reason:** Db2 re-passes SQL inputs on every table-function call — verified:
  a fresh `in_text` at FETCH yields byte-identical rows. Storing `len` + reading
  fresh `in_text` relies only on that documented behavior. Caching the OPEN-time
  pointer additionally assumes the fenced argument buffer keeps the same address
  across calls — observed true on 12.1 (a probe showed `same=1` every FETCH) but
  not documented; one extra `size_t` buys independence from that assumption.

## End-of-data uses SQLSTATE '02000', defined locally
- **Decision:** when `chunk_window()` returns 0 (past the end),
  `strcpy(SQLUDF_STATE, "02000")`.
- **Alternatives:** none valid — this is the protocol.
- **Reason:** `sqludf.h` has constants for OK/error/warn states but none for
  end-of-data; the external-table-function protocol requires the function to
  return SQLSTATE 02000 to signal "no more rows".

## SQLUDF_TRAIL_ARGS_ALL in the signature
- **Decision:** the entry point ends with the `SQLUDF_TRAIL_ARGS_ALL` macro.
- **Alternatives:** `SQLUDF_TRAIL_ARGS` (+ manual scratchpad arg).
- **Reason:** the header states TRAIL_ARGS_ALL is exactly the case when both
  SCRATCHPAD and FINAL CALL are defined — which is how we register the function.

## PARAMETER STYLE SQL + argument order
- **Decision:** inputs, then result columns, then input null-indicators, then
  result null-indicators, then the trailing args.
- **Reason:** this is the PARAMETER STYLE SQL contract; the null indicators are
  `SQLUDF_NULLIND` (`short`) and tested with the `SQLUDF_NULL()` macro.

## FENCED, DETERMINISTIC registration
- **Decision:** register `FENCED`, `DETERMINISTIC`, `NO SQL`, `DISALLOW PARALLEL`.
- **Alternatives:** `NOT FENCED` for lower call overhead; `NOT DETERMINISTIC`.
- **Reason:** FENCED keeps a C bug out of `db2sysc`'s address space. The function
  is genuinely `DETERMINISTIC` — the same `(text, chunk_size)` always yields the
  same rows — and saying so lets Db2 cache/reuse results. (Review feedback:
  Indrigo / Quader.)

## EXTERNAL NAME 'db2chunk!chunk_tf', deployed without .so
- **Decision:** deploy the shared object to `sqllib/function/db2chunk` (no
  extension) and reference the bare library name in EXTERNAL NAME.
- **Alternatives:** keep `.so` and reference `db2chunk.so`.
- **Reason:** Db2 resolves a bare EXTERNAL NAME library against the function
  directory; matching the on-disk name exactly (no extension) is the least
  ambiguous. Verified: the function loads and returns rows.

## Byte-offset cutting; multibyte deferred
- **Decision:** cut on raw byte offsets; treat VARCHAR as a null-terminated
  SBCS C string (`SQLUDF_VARCHAR` == `char`).
- **Reason:** iteration 0 is ASCII-only by scope. UTF-8/MBCS boundary safety is
  explicitly out of scope and flagged in a code comment as unsafe/deferred.

## Window count = ceil(len / chunk_size)
- **Decision:** the last window is partial (`len % chunk_size`); an exact fit
  yields no trailing empty window. `chunk_window()` returns 0 once
  `index * chunk_size >= len`.
- **Reason:** matches the "3 rows for 24 bytes / window 10" acceptance target
  (10 + 10 + 4); verified with edge cases (`abcd`/4 → 1 row, NULL → 0, size≤0 → 0).

## Debug trace to stderr + how it was verified
- **Decision:** `-DCHUNK_DEBUG` compiles a per-phase `fprintf(stderr,...)` trace.
- **Note:** under FENCED, db2fmp's stderr is `/dev/null`, so the live trace is
  discarded by design. Lifecycle order, scratchpad-state survival across FETCH,
  the 02000 end-of-data signal, and zero leaks (valgrind) were verified by
  driving the real `chunk_tf` through a mock Db2 call sequence in a throwaway
  harness. The correct live 3-row result independently confirms the in-engine
  lifecycle.
