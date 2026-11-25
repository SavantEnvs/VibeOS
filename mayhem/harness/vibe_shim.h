/*
 * Force-included (-include) into every VibeOS kernel TU built for the host fuzzers.
 * The kernel ships its own libc-named string/printf routines (kernel/string.c,
 * kernel/printf.c). Renaming them keeps the kernel's implementations (including its
 * strtok_r, which fat32/vfs path resolution relies on) in the fuzzed binary instead of
 * the host libc's, without clashing with the libc/ASan symbols of the same name.
 */
#ifndef VIBE_SHIM_H
#define VIBE_SHIM_H
#define memcpy     vibe_memcpy
#define memset     vibe_memset
#define memset32   vibe_memset32
#define memmove    vibe_memmove
#define memcmp     vibe_memcmp
#define memchr     vibe_memchr
#define strlen     vibe_strlen
#define strcpy     vibe_strcpy
#define strncpy    vibe_strncpy
#define strcmp     vibe_strcmp
#define strncmp    vibe_strncmp
#define strcat     vibe_strcat
#define strchr     vibe_strchr
#define strrchr    vibe_strrchr
#define strdup     vibe_strdup
#define strcasecmp vibe_strcasecmp
#define strtok_r   vibe_strtok_r
#define strstr     vibe_strstr
#define sprintf    vibe_sprintf
#define snprintf   vibe_snprintf
/* printf is console logging only; harness_stubs.c makes it a no-op (kernel/printf.c's
 * own printf is renamed away with -Dvibe_printf=vibe_printf_kernel). */
#define printf     vibe_printf
#endif
