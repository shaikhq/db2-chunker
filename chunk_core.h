#ifndef CHUNK_CORE_H
#define CHUNK_CORE_H

#include <stddef.h>

/* Pure chunking core: no Db2 headers, no I/O. Splits a byte buffer into
 * fixed-size, non-overlapping windows and returns {offset, length}
 * descriptors. Testable standalone. */

typedef struct {
    size_t offset;  /* byte offset of chunk start within the input buffer */
    size_t length;  /* byte length of chunk (<= chunk_size)               */
} chunk_desc;

/* Compute fixed-window descriptors over `len` bytes with the given
 * `chunk_size`. Allocates the returned array with malloc; caller frees.
 * On success returns the array and writes the count to *out_count.
 * Returns NULL and sets *out_count to 0 on invalid input (chunk_size <= 0)
 * or allocation failure. len == 0 yields zero chunks (NULL, count 0). */
chunk_desc *chunk_fixed(const char *buf, size_t len, int chunk_size,
                        size_t *out_count);

#endif /* CHUNK_CORE_H */
