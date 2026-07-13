# Decisions — Iteration 0 (fixed-window chunking table function)

Target: Db2 v12.1.5.0 LUW, Linux x86-64, gcc. API grounded against the
installed header `/opt/ibm/db2/V12.1/include/sqludf.h`, not memory.

## Core / adapter split
- **Decision:** `chunk_core.{h,c}` is plain C with zero Db2 headers; the adapter
  `db2_chunk_udf.c` owns all lifecycle/scratchpad/SQL concerns.
- **Alternatives:** one file that does splitting and row emission together.
- **Reason:** the core is unit-testable in the fast loop (`make test`) without a
  database; the pipe and the algorithm evolve independently.

## Scratchpad holds a pointer, not the data
- **Decision:** stash a single `tf_state*` in `scratchpad.data[]`; the array and
  text copy live on the heap.
- **Alternatives:** pack descriptors directly into the 100-byte scratchpad.
- **Reason:** 100 bytes (`SQLUDF_SCRATCHPAD_LEN`) can't hold arbitrary input;
  a pointer is 8 bytes and fits with room to spare. Db2 preserves `data[]`
  byte-for-byte across OPEN/FETCH/CLOSE, so the pointer survives the scan.

## Allocate on OPEN, free on CLOSE (FIRST/FINAL as brackets)
- **Decision:** build state in OPEN, free in CLOSE; FIRST only traces, FINAL is a
  defensive free of any leftover state.
- **Alternatives:** allocate in FIRST / free in FINAL.
- **Reason:** OPEN..CLOSE brackets a single scan and can repeat (e.g. a table
  function re-scanned in a join); per-scan alloc/free is the leak-free match.
  FIRST/FINAL fire once per statement — wrong granularity for scan state.

## Copy the input text on OPEN
- **Decision:** `malloc`+`memcpy` the input VARCHAR into `tf_state.text`.
- **Alternatives:** keep the descriptors pointing into Db2's argument buffer.
- **Reason:** the input buffer's lifetime across FETCH calls isn't guaranteed;
  owning a private copy makes FETCH's substring emission safe.

## End-of-data uses SQLSTATE '02000', defined locally
- **Decision:** on a FETCH past the last chunk, `strcpy(SQLUDF_STATE, "02000")`.
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

## FENCED registration
- **Decision:** register `FENCED` (also `NOT DETERMINISTIC`, `NO SQL`,
  `DISALLOW PARALLEL`).
- **Alternatives:** `NOT FENCED` for lower call overhead.
- **Reason:** a spike shouldn't share the engine's address space — a bug in C
  can't corrupt db2sysc. Speed is irrelevant at this stage.

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

## Chunk count = ceil(len / chunk_size)
- **Decision:** last window is a partial chunk (`len % chunk_size`).
- **Reason:** matches the "3 rows for 24 bytes / window 10" acceptance target
  (10 + 10 + 4).

## Debug trace to stderr + how it was verified
- **Decision:** `-DCHUNK_DEBUG` compiles a per-phase `fprintf(stderr,...)` trace.
- **Note:** under FENCED, db2fmp's stderr is `/dev/null`, so the live trace is
  discarded by design. Lifecycle order, scratchpad-pointer survival, the 02000
  end-of-data signal, and zero leaks (valgrind) were verified by driving the
  real `chunk_tf` through a mock Db2 call sequence in a throwaway harness. The
  correct live 3-row result independently confirms the in-engine lifecycle.
