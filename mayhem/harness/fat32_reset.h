/* Reset kernel/fat32.c's file-static state so each input starts from a cold mount. */
static void fat32_reset(void) {
    free(cluster_buf);
    cluster_buf = NULL;
    cluster_buf_size = 0;
    __builtin_memset(&fs, 0, sizeof(fs));
    fs_initialized = 0;
    partition_offset = 0;
    __builtin_memset(fat_cache, 0, sizeof(fat_cache));
    fat_cache_counter = 0;
}
