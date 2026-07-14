/* Db2 external table-function adapter. Owns the lifecycle, scratchpad, null
 * indicators, and row emission; chunk_core does the splitting.
 * Grounded against /opt/ibm/db2/V12.1/include/sqludf.h. */

#include <sqludf.h>
#include <string.h>
#include <stdlib.h>

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

/* Per-scan state; only a tf_state* lives in the 100-byte scratchpad. */
typedef struct {
    char       *text;
    chunk_desc *descs;
    size_t      count;
    size_t      next;
} tf_state;

/* Scratchpad contract: exactly one tf_state* in data[], preserved by Db2
 * across OPEN/FETCH/CLOSE, so the pointer survives the scan. */
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
    tf_state *st;

    switch (SQLUDF_CALLT) {

    /* FIRST: once per statement; nothing to allocate. */
    case SQLUDF_TF_FIRST:
        TRACE("FIRST");
        break;

    /* OPEN: once per scan; copy input, run core, stash pointer. */
    case SQLUDF_TF_OPEN:
        TRACE("OPEN");
        st = calloc(1, sizeof(*st));
        if (st == NULL) {
            strcpy(SQLUDF_STATE, "38901");
            return;
        }
        if (SQLUDF_NULL(in_text_ind) || SQLUDF_NULL(in_size_ind)) {
            st->count = 0;
        } else {
            size_t len = strlen(in_text);
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

    /* FETCH: emit one row; SQLSTATE 02000 when exhausted. */
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
            out_text[d.length] = '\0';
            *out_index_ind = 0;
            *out_text_ind  = 0;
            st->next++;
        }
        break;

    /* CLOSE: once per scan; free and clear the pointer. */
    case SQLUDF_TF_CLOSE:
        TRACE("CLOSE");
        st = sp_get(SQLUDF_SCRAT);
        st_free(st);
        sp_put(SQLUDF_SCRAT, NULL);
        break;

    /* FINAL: once per statement. Freeing happens in CLOSE; nothing to do here.
     * Trade-off: a scan torn down without CLOSE would leak until process exit. */
    case SQLUDF_TF_FINAL:
        TRACE("FINAL");
        break;

    default:
        break;
    }
}
