/* Db2 external table-function adapter for the fixed-window chunker.
 *
 * Owns the table-function lifecycle (FIRST, OPEN, FETCH*, CLOSE, FINAL),
 * the scratchpad, null indicators, and row emission. The pure core
 * (chunk_core) does the splitting and knows nothing about Db2.
 *
 * Grounded against the installed header /opt/ibm/db2/V12.1/include/sqludf.h
 * (Db2 v12.1). Call-type constants, the scratchpad struct, and the
 * SQLUDF_TRAIL_ARGS_ALL argument list all come from that header.
 */

#include <sqludf.h>   /* SQLUDF_* macros, sqludf_scratchpad, call-type enum */
#include <string.h>
#include <stdlib.h>

#include "chunk_core.h"

/* Compile-time lifecycle trace to stderr: build with -DCHUNK_DEBUG. */
#ifdef CHUNK_DEBUG
#include <stdio.h>
#define TRACE(phase) fprintf(stderr, "[chunk_udf] %s\n", (phase))
#else
#define TRACE(phase) ((void)0)
#endif

/* End-of-data SQLSTATE for table functions: a FETCH that produces no row
 * sets SQLSTATE '02000' (no data). Db2 has no header constant for this, so
 * it is defined here per the external-table-function protocol. */
#define CHUNK_SQLSTATE_ENDDATA "02000"

/* Per-scan state, heap-allocated on OPEN and freed on CLOSE. Only a pointer
 * to it lives in the scratchpad (data[] is 100 bytes; the pointer fits). */
typedef struct {
    char       *text;   /* private copy of the input text                 */
    chunk_desc *descs;  /* fixed-window descriptors over text             */
    size_t      count;  /* number of descriptors                          */
    size_t      next;   /* index of the next chunk to emit on FETCH       */
} tf_state;

/* Scratchpad contract: we store exactly one tf_state* in data[]. Db2 zeroes
 * the scratchpad before the FIRST call and preserves data[] byte-for-byte
 * across OPEN/FETCH/CLOSE, so the pointer survives the whole scan. */
static tf_state *sp_get(SQLUDF_SCRATCHPAD *sp)
{
    tf_state *st = NULL;
    memcpy(&st, sp->data, sizeof(st));
    return st;
}
static void sp_put(SQLUDF_SCRATCHPAD *sp, tf_state *st)
{
    memcpy(sp->data, &st, sizeof(st));
}
static void st_free(tf_state *st)
{
    if (!st) return;
    free(st->text);
    free(st->descs);
    free(st);
}

/* CREATE FUNCTION ... PARAMETER STYLE SQL passes, in order:
 *   inputs        : text, chunk_size
 *   result cols   : out_index, out_text
 *   input NULLs   : text_ind, size_ind
 *   result NULLs  : out_index_ind, out_text_ind
 *   SQLUDF_TRAIL_ARGS_ALL : sqlstate, fname, fspecname, msgtext,
 *                           scratchpad, call type
 */
#ifdef __cplusplus
extern "C"
#endif
void chunk_tf(SQLUDF_VARCHAR   *in_text,       /* IN  VARCHAR text        */
              SQLUDF_INTEGER   *in_size,       /* IN  INTEGER chunk_size  */
              SQLUDF_INTEGER   *out_index,     /* OUT INTEGER chunk_index */
              SQLUDF_VARCHAR   *out_text,      /* OUT VARCHAR chunk_text  */
              SQLUDF_NULLIND   *in_text_ind,
              SQLUDF_NULLIND   *in_size_ind,
              SQLUDF_NULLIND   *out_index_ind,
              SQLUDF_NULLIND   *out_text_ind,
              SQLUDF_TRAIL_ARGS_ALL)
{
    tf_state *st;

    switch (SQLUDF_CALLT) {

    /* FIRST (-2): once per statement, scratchpad freshly zeroed. Nothing to
     * allocate here; all per-scan state is built on OPEN. */
    case SQLUDF_TF_FIRST:
        TRACE("FIRST");
        break;

    /* OPEN (-1): once per scan. Copy the input, run the core, stash the
     * state pointer in the scratchpad. Inputs are guaranteed present here. */
    case SQLUDF_TF_OPEN:
        TRACE("OPEN");
        st = calloc(1, sizeof(*st));
        if (st == NULL) {
            strcpy(SQLUDF_STATE, "38901");           /* out of memory */
            return;
        }
        if (SQLUDF_NULL(in_text_ind) || SQLUDF_NULL(in_size_ind)) {
            st->count = 0;                            /* NULL input => 0 rows */
        } else {
            size_t len = strlen(in_text);             /* VARCHAR SBCS is C-string */
            st->text = malloc(len + 1);
            if (st->text == NULL) {
                free(st);
                strcpy(SQLUDF_STATE, "38901");
                return;
            }
            memcpy(st->text, in_text, len + 1);
            st->descs = chunk_fixed(st->text, len, (int)*in_size, &st->count);
        }
        st->next = 0;
        sp_put(SQLUDF_SCRAT, st);
        break;

    /* FETCH (0): emit one row per call. When exhausted, set SQLSTATE 02000
     * so Db2 stops the scan. The scratchpad pointer persists across calls. */
    case SQLUDF_TF_FETCH:
        TRACE("FETCH");
        st = sp_get(SQLUDF_SCRAT);
        if (st == NULL || st->next >= st->count) {
            strcpy(SQLUDF_STATE, CHUNK_SQLSTATE_ENDDATA);
            return;
        }
        {
            chunk_desc d = st->descs[st->next];
            *out_index = (SQLUDF_INTEGER)st->next;
            memcpy(out_text, st->text + d.offset, d.length);
            out_text[d.length] = '\0';                /* VARCHAR SBCS: null-terminated */
            *out_index_ind = 0;                       /* result not null */
            *out_text_ind  = 0;
            st->next++;
        }
        break;

    /* CLOSE (1): once per scan. Free the state and clear the pointer so a
     * later OPEN (e.g. a re-scan in a join) starts clean. */
    case SQLUDF_TF_CLOSE:
        TRACE("CLOSE");
        st = sp_get(SQLUDF_SCRAT);
        st_free(st);
        sp_put(SQLUDF_SCRAT, NULL);
        break;

    /* FINAL (2): once per statement. Defensive backstop; CLOSE already freed
     * on the normal path, so this only fires if a scan was torn down early. */
    case SQLUDF_TF_FINAL:
        TRACE("FINAL");
        st = sp_get(SQLUDF_SCRAT);
        st_free(st);
        sp_put(SQLUDF_SCRAT, NULL);
        break;

    default:
        break;
    }
}
