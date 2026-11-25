/*
 * VFS (kernel/vfs.c) on top of the FAT32 driver (kernel/fat32.c) over a fuzzed, writable
 * disk image: mount, then an operation script taken from the input's tail drives lookups,
 * cwd changes, readdir, reads and the write side (create, write, append, mkdir, delete,
 * recursive delete, rename) with paths from a small list plus names found on the disk.
 *
 * Input: [u16 LE image length][image][op script]. The image is the disk; the script is the
 * sequence of file operations a program performs on it.
 */
#include "../../kernel/fat32.c"
#include "../../kernel/vfs.c"
#include "blockdev.h"
#include "fat32_reset.h"

/* Free file data still reachable from the in-memory tree (the fallback when the image does
 * not mount). Deleted nodes are unlinked but keep their freed data pointer, so only walk
 * the live tree. */
static void free_tree(vfs_node_t *n, int depth) {
    if (!n || depth > VFS_MAX_INODES) return;
    if (n->type == VFS_FILE) { free(n->data); n->data = NULL; return; }
    for (int i = 0; i < n->child_count && i < VFS_MAX_CHILDREN; i++)
        if (n->children[i] != n) free_tree(n->children[i], depth + 1);
}

static void vfs_reset(void) {
    free_tree(mem_root, 0);
    __builtin_memset(inodes, 0, sizeof(inodes));
    inode_count = 0;
    mem_root = NULL;
    use_fat32 = 0;
    __builtin_strcpy(cwd_path, "/");
}

#define NPATH 24
static char paths[NPATH][VFS_MAX_PATH];
static int npaths;

static void add_path(const char *p) {
    if (npaths >= NPATH) return;
    size_t n = __builtin_strlen(p);
    if (n >= VFS_MAX_PATH) return;
    __builtin_memcpy(paths[npaths++], p, n + 1);
}

static const char *base_paths[] = { "/", "/tmp", "/home", "/home/user", "/bin", "/a.txt",
    "/tmp/new.txt", "/home/user/notes.txt", "..", ".", "sub", "sub/file", "/LongFileNameExample.txt",
    "//x", "" };

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2) return -1;
    size_t img = data[0] | (data[1] << 8);
    if (img > size - 2) img = size - 2;
    const uint8_t *ops = data + 2 + img;
    size_t nops = size - 2 - img;

    fat32_reset();
    vfs_reset();
    blockdev_load(data + 2, img);
    vfs_init();

    npaths = 0;
    for (size_t i = 0; i < sizeof(base_paths) / sizeof(base_paths[0]); i++) add_path(base_paths[i]);
    vfs_node_t *root = vfs_lookup("/");
    char name[VFS_MAX_NAME + 2];
    uint8_t type;
    for (int i = 0; root && i < 8; i++) {
        if (vfs_readdir(root, i, name, VFS_MAX_NAME, &type) != 0) break;
        char p[VFS_MAX_PATH];
        snprintf(p, sizeof(p), "/%s", name);
        add_path(p);
    }

    static char buf[4096];
    for (size_t i = 0; i + 2 <= nops; i += 3) {
        uint8_t op = ops[i] % 14;
        const char *p = paths[ops[i + 1] % npaths];
        uint8_t arg = (i + 2 < nops) ? ops[i + 2] : 0;
        switch (op) {
        case 0: { vfs_node_t *n = vfs_lookup(p); if (n) vfs_read(n, buf, sizeof(buf), arg * 16); break; }
        case 1: vfs_set_cwd(p); break;
        case 2: { vfs_node_t *n = vfs_lookup(p);
                  for (int k = 0; n && k < 40; k++) if (vfs_readdir(n, k, name, VFS_MAX_NAME, &type)) break;
                  break; }
        case 3: vfs_create(p); break;
        case 4: { vfs_node_t *n = vfs_lookup(p); if (n) vfs_write(n, (const char *)ops, arg * 8 < nops ? arg * 8 : nops); break; }
        case 5: { vfs_node_t *n = vfs_lookup(p); if (n) vfs_append(n, (const char *)ops, arg < nops ? arg : nops); break; }
        case 6: vfs_mkdir(p); break;
        case 7: vfs_delete(p); break;
        case 8: vfs_delete_dir(p); break;
        case 9: vfs_delete_recursive(p); break;
        case 10: { static const char *nn[] = { "renamed", "A Long Name With Spaces.txt", "x.y.z", "" };
                   vfs_rename(p, nn[arg % 4]); break; }
        case 11: vfs_get_cwd_path(buf, arg % 64 + 1); break;
        case 12: { vfs_node_t *n = vfs_open_handle(p);
                   if (n) { vfs_read(n, buf, sizeof(buf), 0); vfs_close_handle(n); } break; }
        case 13: vfs_is_dir(vfs_lookup(p)); vfs_is_file(vfs_lookup(p)); break;
        }
    }
    return 0;
}
