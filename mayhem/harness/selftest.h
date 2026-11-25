/* Known-answer self-test of VibeOS kernel subsystems (built with normal flags). */
#include <stdint.h>
#include <stddef.h>
void st_check(const char *name, int ok);
uint8_t *st_load(const char *path, size_t *len);
void selftest_fs(void);
void selftest_net(void);
