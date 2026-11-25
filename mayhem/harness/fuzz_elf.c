/*
 * ELF loader (kernel/elf.c) on a fuzzed program file: header validation, program-header
 * walk, size calculation, then loading at a heap base with PT_DYNAMIC / RELA relocation —
 * the path process_exec takes for a binary read from disk. elf_load() (absolute p_vaddr)
 * is not called: the kernel itself uses elf_load_at with a buffer of elf_calc_size bytes.
 */
#include "../../kernel/elf.h"
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    uint8_t *copy = malloc(size ? size : 1);
    __builtin_memcpy(copy, data, size);
    if (elf_validate(copy, size) == 0) {
        elf_entry(copy);
        uint64_t need = elf_calc_size(copy, size);
        /* The kernel allocates a process image of elf_calc_size bytes; bound it to what a
         * test host can allocate. */
        /* ET_EXEC segments go to their absolute p_vaddr (identity-mapped kernel memory),
         * which a host process cannot provide; only PIE images are loaded at a base. */
        if (((const Elf64_Ehdr *)copy)->e_type == ET_DYN && need > 0 && need <= (16u << 20)) {
            uint8_t *mem = calloc(1, need);
            elf_load_info_t info;
            if (mem) elf_load_at(copy, size, (uint64_t)(uintptr_t)mem, &info);
            free(mem);
        }
    }
    free(copy);
    return 0;
}
