/*
 * Device-tree parser (kernel/dtb.c) on a fuzzed flattened device tree blob: FDT header,
 * structure-block token walk, strings-block lookups and /memory reg decoding. The boot
 * loader hands the kernel a blob of the header's totalsize bytes, so inputs shorter than
 * the header or than their own totalsize are rejected (precondition of the FDT format).
 */
#include "../../kernel/dtb.h"
#include <stdlib.h>

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < sizeof(struct fdt_header)) return -1;
    if (be32(data + 4) > size) return -1;
    uint8_t *copy = malloc(size);
    __builtin_memcpy(copy, data, size);
    struct dtb_memory_info mi;
    dtb_parse(copy, &mi);
    dtb_get_error();
    free(copy);
    return 0;
}
