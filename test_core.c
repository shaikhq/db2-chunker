#include "chunk_core.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Fast-loop test for the pure core; no Db2. */
int main(void)
{
    const char *s = "chunking is fun to build"; /* 24 bytes / 10 => 3 chunks */
    size_t n = 0;
    chunk_desc *d = chunk_fixed(s, strlen(s), 10, &n);

    if (n != 3) {
        fprintf(stderr, "FAIL: expected 3 chunks, got %zu\n", n);
        free(d);
        return 1;
    }
    for (size_t i = 0; i < n; i++)
        printf("chunk %zu: off=%zu len=%zu \"%.*s\"\n",
               i, d[i].offset, d[i].length, (int)d[i].length, s + d[i].offset);
    free(d);
    printf("PASS: 3 chunks\n");
    return 0;
}
