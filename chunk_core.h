#ifndef CHUNK_CORE_H
#define CHUNK_CORE_H

#include <stddef.h>

/* Pure fixed-window chunker: no Db2, no I/O. */

typedef struct {
    size_t offset;
    size_t length;
} chunk_desc;

/* Fixed windows over `len` bytes. Returns a malloc'd array (caller frees) and
 * writes the count to *out_count. NULL/count 0 on bad input or len == 0. */
chunk_desc *chunk_fixed(const char *buf, size_t len, int chunk_size,
                        size_t *out_count);

#endif /* CHUNK_CORE_H */
