#include "chunk_core.h"
#include <stdio.h>
#include <string.h>

/* Fast-loop test for the pure core; no Db2. */
int main(void)
{
    const char *s = "chunking is fun to build"; /* 24 bytes / 10 => 3 windows */
    size_t len = strlen(s), off, wlen, n = 0;

    for (size_t i = 0; chunk_window(len, 10, i, &off, &wlen); i++, n++)
        printf("chunk %zu: off=%zu len=%zu \"%.*s\"\n",
               i, off, wlen, (int)wlen, s + off);

    if (n != 3) {
        fprintf(stderr, "FAIL: expected 3 chunks, got %zu\n", n);
        return 1;
    }
    printf("PASS: 3 chunks\n");
    return 0;
}
