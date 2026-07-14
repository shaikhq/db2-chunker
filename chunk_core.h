#ifndef CHUNK_CORE_H
#define CHUNK_CORE_H

#include <stddef.h>

/* Pure fixed-window math: no Db2, no allocation. Computes one window on demand.
 * For 0-based `index`, writes that window's byte offset and length and returns
 * 1; returns 0 when `index` is past the end (or chunk_size is 0). */
int chunk_window(size_t len, size_t chunk_size, size_t index,
                 size_t *offset, size_t *length);

#endif /* CHUNK_CORE_H */
