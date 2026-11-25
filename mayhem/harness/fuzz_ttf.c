/*
 * TrueType renderer (kernel/ttf.c + vendored stb_truetype) on a fuzzed font file: ttf_init
 * loads the font through the VFS, then glyph requests render, cache, embolden and slant
 * (apply_bold / apply_italic) glyphs and query metrics, advances and kerning.
 *
 * Input: [n][n x (codepoint lo, codepoint hi, size, style)][font file bytes]. The font is
 * what vfs_lookup/vfs_read of /fonts/Roboto/Roboto-Regular.ttf return (the file on disk).
 * ttf.c's one-shot static state (font, caches, temp bitmap) is reset per input.
 */
#include "../../kernel/ttf.c"

static const uint8_t *font_bytes;
static size_t font_len;
static vfs_node_t font_node;

vfs_node_t *vfs_lookup(const char *path) {
    if (__builtin_strcmp(path, FONT_PATH) != 0) return NULL;
    __builtin_memset(&font_node, 0, sizeof(font_node));
    font_node.type = VFS_FILE;
    font_node.size = font_len;
    return &font_node;
}

int vfs_read(vfs_node_t *f, char *buf, size_t size, size_t offset) {
    (void)f;
    if (offset >= font_len) return 0;
    size_t n = font_len - offset < size ? font_len - offset : size;
    __builtin_memcpy(buf, font_bytes + offset, n);
    return (int)n;
}

static void ttf_reset(void) {
    for (int i = 0; i < NUM_SIZE_CACHES; i++) {
        for (int j = 0; j < MAX_CACHED_GLYPHS; j++) free(size_caches[i].entries[j].glyph.bitmap);
    }
    __builtin_memset(size_caches, 0, sizeof(size_caches));
    free(font_data); font_data = NULL; font_data_size = 0;
    free(temp_bitmap); temp_bitmap = NULL; temp_bitmap_size = 0;
    __builtin_memset(&font_info, 0, sizeof(font_info));
    ttf_ready = 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return -1;
    size_t n = data[0] % 33;
    if (size < 1 + n * 4) return -1;
    const uint8_t *req = data + 1;
    font_bytes = data + 1 + n * 4;
    font_len = size - 1 - n * 4;

    ttf_reset();
    if (ttf_init() != 0) return 0;
    int a, d, g;
    for (size_t i = 0; i < n; i++) {
        int cp = req[i * 4] | (req[i * 4 + 1] << 8);
        int sz = 8 + req[i * 4 + 2] % 40;
        int style = req[i * 4 + 3] & 3;
        ttf_get_glyph(cp, sz, style);
        ttf_get_advance(cp, sz);
        if (i) ttf_get_kerning(req[(i - 1) * 4] | (req[(i - 1) * 4 + 1] << 8), cp, sz);
        ttf_get_metrics(sz, &a, &d, &g);
    }
    /* Cache eviction: more distinct glyphs than MAX_CACHED_GLYPHS in one size. */
    if (n && (req[3] & 0x80)) for (int cp = 32; cp < 32 + MAX_CACHED_GLYPHS + 4; cp++) ttf_get_glyph(cp, 16, req[3] & 3);
    return 0;
}
