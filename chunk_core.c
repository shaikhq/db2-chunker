#include "chunk_core.h"
#include <stdlib.h>

/* NOTE: cuts on raw byte offsets. Multibyte (UTF-8/MBCS) boundary safety is
 * NOT handled and is deliberately deferred; test with ASCII only. */

chunk_desc *chunk_fixed(const char *buf, size_t len, int chunk_size,
                        size_t *out_count)
{
    (void)buf; /* fixed windows depend only on len, not content */
    *out_count = 0;
    if (chunk_size <= 0)
        return NULL;
    if (len == 0)
        return NULL;

    size_t cs = (size_t)chunk_size;
    size_t count = (len + cs - 1) / cs; /* ceil-divide: last window is partial */

    chunk_desc *descs = malloc(count * sizeof(*descs));
    if (descs == NULL)
        return NULL;

    for (size_t i = 0; i < count; i++) {
        size_t off = i * cs;
        size_t remaining = len - off;
        descs[i].offset = off;
        descs[i].length = remaining < cs ? remaining : cs;
    }

    *out_count = count;
    return descs;
}
