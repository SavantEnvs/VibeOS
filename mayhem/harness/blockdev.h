/*
 * In-memory block device standing in for virtio-blk (kernel/hal/hal.h hal_blk_*).
 * The fuzz input is the leading part of the disk: sectors past its end read as zeros (an
 * unwritten region of a disk as large as the BPB claims), and writes there are dropped.
 * This keeps cluster allocation, which scans the FAT for a zero entry, from walking
 * billions of unreadable sectors on a BPB that claims a huge volume. A private writable
 * copy of the input is made per run.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
static uint8_t *disk;
static size_t disk_size;

int hal_blk_read(uint32_t sector, void *buf, uint32_t count) {
    uint64_t off = (uint64_t)sector * 512, len = (uint64_t)count * 512;
    uint64_t n = off < disk_size ? disk_size - off : 0;
    if (n > len) n = len;
    if (n) __builtin_memcpy(buf, disk + off, n);
    if (n < len) __builtin_memset((uint8_t *)buf + n, 0, len - n);
    return 0;
}

int hal_blk_write(uint32_t sector, const void *buf, uint32_t count) {
    uint64_t off = (uint64_t)sector * 512, len = (uint64_t)count * 512;
    uint64_t n = off < disk_size ? disk_size - off : 0;
    if (n > len) n = len;
    if (n) __builtin_memcpy(disk + off, buf, n);
    return 0;
}

static void blockdev_load(const uint8_t *data, size_t size) {
    free(disk);
    disk = malloc(size ? size : 1);
    __builtin_memcpy(disk, data, size);
    disk_size = size;
}
