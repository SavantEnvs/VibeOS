#include "../../kernel/fat32.c"
#include "../../kernel/vfs.c"
#include "blockdev.h"
#include "fat32_reset.h"
#include "selftest.h"

static int seen;
static void cb(const char *name, int is_dir, uint32_t size, void *ud) {
    (void)ud;
    if (!strcmp(name, "hello.txt") || !strcmp(name, "HELLO.TXT")) seen |= (!is_dir && size == 25) ? 1 : 0;
    if (!strcmp(name, "LongFileNameExample.txt")) seen |= (size == 24) ? 2 : 0;
    if (!strcasecmp(name, "home")) seen |= is_dir ? 4 : 0;
}

static void mount(const char *img) {
    size_t n; uint8_t *d = st_load(img, &n);
    fat32_reset();
    blockdev_load(d, n);
    free(d);
}

void selftest_fs(void) {
    static char buf[2048];
    char nm[48];
#define CK(n, ok) (snprintf(nm, sizeof(nm), "%s_%s", n, v ? "mbr" : "raw"), st_check(nm, ok))
    for (int v = 0; v < 2; v++) {
        mount(v ? "mayhem/fuzz_fat32/testsuite/mbr.img" : "mayhem/fuzz_fat32/testsuite/raw.img");
        CK("fat32_mount", fat32_init() == 0);
        seen = 0;
        CK("fat32_list_root", fat32_list_dir("/", cb, NULL) == 0 && seen == 7);
        int r = fat32_read_file("/hello.txt", buf, sizeof(buf));
        CK("fat32_read_8_3", r == 25 && !memcmp(buf, "Hello from VibeOS FAT32!\n", 25));
        r = fat32_read_file("/LongFileNameExample.txt", buf, sizeof(buf));
        CK("fat32_read_lfn", r == 24 && !memcmp(buf, "long file name contents\n", 24));
        r = fat32_read_file("/home/user/notes.txt", buf, sizeof(buf));
        CK("fat32_read_nested", r == 20 && !memcmp(buf, "notes in /home/user\n", 20));
        r = fat32_read_file("/big.bin", buf, sizeof(buf));
        int ok = r == 1300;
        for (int i = 0; ok && i < 1300; i++) ok = (uint8_t)buf[i] == (uint8_t)(i * 7);
        CK("fat32_read_chain", ok);
        r = fat32_read_file_offset("/big.bin", buf, 10, 1020);
        CK("fat32_read_offset", r == 10 && (uint8_t)buf[0] == (uint8_t)(1020 * 7));
        CK("fat32_file_size", fat32_file_size("/big.bin") == 1300 && fat32_file_size("/nope") == -1);
        CK("fat32_is_dir", fat32_is_dir("/home/user") == 1 && fat32_is_dir("/hello.txt") == 0);
    }
    /* VFS write path on the raw image */
    mount("mayhem/fuzz_fat32/testsuite/raw.img");
    vfs_init();
    char cwd[64];
    st_check("vfs_init_cwd", vfs_get_cwd_path(cwd, sizeof(cwd)) == 0 && !strcmp(cwd, "/home/user"));
    vfs_node_t *n = vfs_create("/home/user/new.txt");
    st_check("vfs_create", n != NULL);
    if (n) st_check("vfs_write", vfs_write(n, "written by selftest", 19) == 19);
    st_check("vfs_write_readback", fat32_read_file("/home/user/new.txt", buf, sizeof(buf)) == 19 &&
             !memcmp(buf, "written by selftest", 19));
    st_check("vfs_mkdir", vfs_mkdir("/home/user/sub") != NULL && fat32_is_dir("/home/user/sub") == 1);
    st_check("vfs_rename", vfs_rename("/home/user/new.txt", "renamed.txt") == 0 &&
             fat32_file_size("/home/user/renamed.txt") == 19 && fat32_file_size("/home/user/new.txt") == -1);
    st_check("vfs_delete", vfs_delete("/home/user/renamed.txt") == 0 && fat32_file_size("/home/user/renamed.txt") == -1);
    st_check("vfs_set_cwd", vfs_set_cwd("..") == 0 && vfs_get_cwd_path(cwd, sizeof(cwd)) == 0 && !strcmp(cwd, "/home"));
}
