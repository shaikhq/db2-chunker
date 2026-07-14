#include "chunk_core.h"

/* Cuts on raw byte offsets; multibyte (UTF-8/MBCS) boundaries are unsafe and
 * deferred. ASCII only. */

int chunk_window(size_t len, size_t chunk_size, size_t index,
                 size_t *offset, size_t *length)
{
    if (chunk_size == 0)
        return 0;

    size_t off = index * chunk_size;
    if (off >= len)
        return 0;

    size_t remaining = len - off;
    *offset = off;
    *length = remaining < chunk_size ? remaining : chunk_size; /* last is partial */
    return 1;
}
