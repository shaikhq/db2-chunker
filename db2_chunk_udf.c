/* Db2 external table-function adapter. Owns the lifecycle, scratchpad, null
 * indicators, and row emission; chunk_core does the window math.
 * Grounded against /opt/ibm/db2/V12.1/include/sqludf.h. */

#include <sqludf.h>
#include <string.h>

#include "chunk_core.h"

/* Lifecycle trace to stderr; build with -DCHUNK_DEBUG. */
#ifdef CHUNK_DEBUG
#include <stdio.h>
#define TRACE(phase) fprintf(stderr, "[chunk_udf] %s\n", (phase))
#else
#define TRACE(phase) ((void)0)
#endif

/* FETCH past the last chunk sets SQLSTATE 02000 (no data); no header constant. */
#define CHUNK_SQLSTATE_ENDDATA "02000"

/* Scan state stored DIRECTLY in the 100-byte scratchpad (no heap allocation).
 * Db2 zeroes the scratchpad before FIRST and preserves it across OPEN/FETCH/
 * CLOSE. Three numbers are enough: fixed windows are computed on demand. */
typedef struct {
    size_t len;         /* input length, captured on OPEN     */
    size_t chunk_size;  /* window size (0 => produce no rows)  */
    size_t next;        /* next 0-based chunk index to emit    */
} tf_state;

/* PARAMETER STYLE SQL order: inputs, result cols, input NULLs, result NULLs,
 * then SQLUDF_TRAIL_ARGS_ALL (sqlstate, names, msg, scratchpad, call type). */
#ifdef __cplusplus
extern "C"
#endif
void chunk_tf(SQLUDF_VARCHAR   *in_text,
              SQLUDF_INTEGER   *in_size,
              SQLUDF_INTEGER   *out_index,
              SQLUDF_VARCHAR   *out_text,
              SQLUDF_NULLIND   *in_text_ind,
              SQLUDF_NULLIND   *in_size_ind,
              SQLUDF_NULLIND   *out_index_ind,
              SQLUDF_NULLIND   *out_text_ind,
              SQLUDF_TRAIL_ARGS_ALL)
{
    tf_state s;

    switch (SQLUDF_CALLT) {

    /* FIRST: once per statement; nothing to set up. */
    case SQLUDF_TF_FIRST:
        TRACE("FIRST");
        break;

    /* OPEN: once per scan; record the three numbers in the scratchpad. */
    case SQLUDF_TF_OPEN:
        TRACE("OPEN");
        if (SQLUDF_NULL(in_text_ind) || SQLUDF_NULL(in_size_ind) || *in_size <= 0) {
            s.len = 0;
            s.chunk_size = 0;                 /* => no rows */
        } else {
            s.len = strlen(in_text);
            s.chunk_size = (size_t)*in_size;
        }
        s.next = 0;
        memcpy(SQLUDF_SCRAT->data, &s, sizeof s);
        break;

    /* FETCH: emit one window computed on demand; 02000 when past the end.
     * Reads from the per-call in_text (Db2 re-passes inputs each call). */
    case SQLUDF_TF_FETCH:
        TRACE("FETCH");
        memcpy(&s, SQLUDF_SCRAT->data, sizeof s);
        {
            size_t off, len;
            if (!chunk_window(s.len, s.chunk_size, s.next, &off, &len)) {
                strcpy(SQLUDF_STATE, CHUNK_SQLSTATE_ENDDATA);
                return;
            }
            *out_index = (SQLUDF_INTEGER)s.next;
            memcpy(out_text, in_text + off, len);
            out_text[len] = '\0';
            *out_index_ind = 0;
            *out_text_ind  = 0;
            s.next++;
            memcpy(SQLUDF_SCRAT->data, &s, sizeof s);
        }
        break;

    /* CLOSE / FINAL: no heap to free. */
    case SQLUDF_TF_CLOSE:
        TRACE("CLOSE");
        break;
    case SQLUDF_TF_FINAL:
        TRACE("FINAL");
        break;

    default:
        break;
    }
}
