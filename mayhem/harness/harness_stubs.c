/* Console/log sinks the kernel's printf.c writes to, and a silent printf. */
int vibe_printf(const char *fmt, ...) { (void)fmt; return 0; }
void console_putc(char c) { (void)c; }
void klog_putc(char c) { (void)c; }
