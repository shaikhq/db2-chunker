-- Register the fixed-window chunking table function and smoke-test it.
-- Run with:  db2 -tvf register.sql   (after `make deploy`)

-- External table function. PARAMETER STYLE SQL + SCRATCHPAD + FINAL CALL means
-- the C entry point receives SQLUDF_TRAIL_ARGS_ALL (scratchpad + call type).
-- FENCED runs it out of the engine's address space (safe for a spike).
-- EXTERNAL NAME 'db2chunk!chunk_tf' => sqllib/function/db2chunk, symbol chunk_tf.
CREATE OR REPLACE FUNCTION chunk(text VARCHAR(4096), chunk_size INTEGER)
    RETURNS TABLE (chunk_index INTEGER, chunk_text VARCHAR(4096))
    LANGUAGE C
    PARAMETER STYLE SQL
    NO SQL
    FENCED
    NOT DETERMINISTIC
    NO EXTERNAL ACTION
    SCRATCHPAD 100
    FINAL CALL
    DISALLOW PARALLEL
    EXTERNAL NAME 'db2chunk!chunk_tf';

-- Smoke test: 24-byte input, window 10 => 3 chunks (10 + 10 + 4).
-- Expected rows:
--   0 | chunking i
--   1 | s fun to b
--   2 | uild
SELECT chunk_index, chunk_text
    FROM TABLE(chunk('chunking is fun to build', 10)) AS t
    ORDER BY chunk_index;

-- Expected count: 3
SELECT COUNT(*) AS chunk_count
    FROM TABLE(chunk('chunking is fun to build', 10)) AS t;
