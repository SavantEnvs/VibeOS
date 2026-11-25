/*
 * Functional self-test for mayhem/test.sh: known-answer checks of the kernel subsystems
 * the fuzz targets cover, on the committed seed inputs. Prints one "PASS <name>" /
 * "FAIL <name>" line per check.
 */
#include "../../kernel/elf.h"
#include "../../kernel/dtb.h"
#include "../../kernel/string.h"
#include "../../kernel/printf.h"
#include "selftest.h"
#include <stdlib.h>
int fprintf(void *, const char *, ...);
int puts(const char *);
void *fopen(const char *, const char *);
size_t fread(void *, size_t, size_t, void *);
int fseek(void *, long, int);
long ftell(void *);
int fclose(void *);

static int fails;
void st_check(const char *name, int ok) {
    char line[128];
    snprintf(line, sizeof(line), "%s %s", ok ? "PASS" : "FAIL", name);
    puts(line);
    if (!ok) fails++;
}

uint8_t *st_load(const char *path, size_t *len) {
    void *f = fopen(path, "rb");
    if (!f) { st_check(path, 0); exit(2); }
    fseek(f, 0, 2); long n = ftell(f); fseek(f, 0, 0);
    uint8_t *b = malloc(n ? n : 1);
    *len = fread(b, 1, n, f);
    fclose(f);
    return b;
}

static void selftest_elf(void) {
    size_t n; uint8_t *e = st_load("mayhem/fuzz_elf/testsuite/pie.elf", &n);
    st_check("elf_validate", elf_validate(e, n) == 0);
    st_check("elf_validate_bad", elf_validate(e, 10) != 0);
    st_check("elf_entry", elf_entry(e) == 0x100);
    uint64_t sz = elf_calc_size(e, n);
    st_check("elf_calc_size", sz == 0x1a8 + 0x40);
    uint8_t *mem = calloc(1, sz);
    elf_load_info_t info;
    int r = elf_load_at(e, n, (uint64_t)(uintptr_t)mem, &info);
    uint64_t slot; memcpy(&slot, mem + 0x1a0, 8);
    st_check("elf_load_at", r == 0 && info.entry == (uint64_t)(uintptr_t)mem + 0x100 && mem[0x100] == 0x1f);
    st_check("elf_relocate", slot == (uint64_t)(uintptr_t)mem + 0x100);
    free(mem); free(e);
}

static void selftest_dtb(void) {
    size_t n; uint8_t *d = st_load("mayhem/fuzz_dtb/testsuite/virt.dtb", &n);
    struct dtb_memory_info mi;
    st_check("dtb_parse", dtb_parse(d, &mi) == 0 && mi.base == 0x40000000ull && mi.size == 0x20000000ull);
    d[0] = 0;
    st_check("dtb_bad_magic", dtb_parse(d, &mi) == -1);
    free(d);
}

static void selftest_string(void) {
    char s[] = "/a//bin/hello.txt", *save, *t;
    char out[64] = "";
    for (t = strtok_r(s, "/", &save); t; t = strtok_r(NULL, "/", &save)) { strcat(out, t); strcat(out, "|"); }
    st_check("strtok_r", !strcmp(out, "a|bin|hello.txt|"));
    char b[16];
    snprintf(b, sizeof(b), "%d:%x:%s", -42, 255, "abcdefghijk");
    st_check("snprintf", !strcmp(b, "-42:ff:abcdefgh"));
    st_check("strcasecmp", strcasecmp("HeLLo", "hello") == 0 && strcasecmp("a", "b") < 0);
}

int main(void) {
    selftest_string();
    selftest_elf();
    selftest_dtb();
    selftest_fs();
    selftest_net();
    return fails ? 1 : 0;
}
