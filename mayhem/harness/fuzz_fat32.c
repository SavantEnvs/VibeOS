/*
 * FAT32 driver (kernel/fat32.c) over a fuzzed disk image: MBR/partition lookup, BPB parse,
 * cluster-chain walking, LFN/8.3 directory entries and path resolution (strtok_r-based) —
 * the read side the VFS uses when the OS mounts and browses the disk.
 */
#include "../../kernel/fat32.c"
#include "blockdev.h"
#include "fat32_reset.h"

#define MAXN 24
typedef struct { int n; char path[MAXN][160]; uint8_t is_dir[MAXN]; } listing_t;
static const char *cur_dir;

static void collect(const char *name, int is_dir, uint32_t size, void *ud) {
    (void)size;
    listing_t *l = (listing_t *)ud;
    if (l->n >= MAXN) return;
    size_t dl = __builtin_strlen(cur_dir), nl = __builtin_strlen(name);
    if (dl + nl + 2 > sizeof(l->path[0])) return;
    char *p = l->path[l->n];
    __builtin_memcpy(p, cur_dir, dl);
    if (dl == 0 || p[dl - 1] != '/') p[dl++] = '/';
    __builtin_memcpy(p + dl, name, nl + 1);
    l->is_dir[l->n] = (uint8_t)is_dir;
    l->n++;
}

static uint8_t rbuf[8192];

static void walk(const char *dir, int depth) {
    listing_t l;
    l.n = 0;
    cur_dir = dir;
    if (fat32_list_dir(dir, collect, &l) < 0) return;
    for (int i = 0; i < l.n; i++) {
        const char *p = l.path[i];
        fat32_is_dir(p);
        if (l.is_dir[i]) {
            if (depth < 3) walk(p, depth + 1);
        } else {
            fat32_file_size(p);
            fat32_read_file(p, rbuf, sizeof(rbuf));
            fat32_read_file_offset(p, rbuf, 700, 1000);
        }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    fat32_reset();
    blockdev_load(data, size);
    if (fat32_init() != 0) return 0;
    walk("/", 0);
    static const char *fixed[] = { "/bin/a", "//", "", "/home/user/../x", "/a/b/c/d/e/f",
                                   "/fonts/Roboto/Roboto-Regular.ttf", "/boot.cfg" };
    for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++) {
        fat32_is_dir(fixed[i]);
        fat32_file_size(fixed[i]);
        fat32_read_file(fixed[i], rbuf, sizeof(rbuf));
    }
    /* fat32_get_free_kb() is not called: it scans every cluster the BPB claims (up to 2^32),
     * which on a fuzzed BPB is minutes per input. */
    fat32_get_total_kb();
    return 0;
}
