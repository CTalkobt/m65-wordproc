/* host.c — the platform layer on the host: banks 4 and 5 an array, the
 * colour RAM another; screen_ppm() writes what the VIC-IV would show */
#include <stdio.h>
#include <string.h>
#include "wp.h"

u8 MEM[0x60000];
static u8 colram[COLS * ROWS * 2];
const u8 *host_colram(void) { return colram; }

/* a 28-bit address: banks 4-5 in MEM; bank 0 (the C program's own buffers) is the host's memory */
u8 mem_get(u32 a) { return MEM[a]; }
void mem_put(u32 a, u8 v) { MEM[a] = v; }
void mem_copy(u32 dst, u32 src, u16 n) {
    /* as the DMA: forwards, byte by byte (so only dst < src may overlap) */
    u16 i;
    if (dst > src && dst < src + n) {
        fprintf(stderr, "mem_copy: overlapping upwards copy %lx <- %lx (%u)\n", (unsigned long)dst,
                (unsigned long)src, n);
    }
    for (i = 0; i < n; i++) MEM[dst + i] = MEM[src + i];
}
void mem_read(u8 *dst, u32 src, u16 n) { memcpy(dst, MEM + src, n); }
void mem_write(u32 dst, const u8 *src, u16 n) { memcpy(MEM + dst, src, n); }
void mem_fill(u32 dst, u16 n, u8 v) { memset(MEM + dst, v, n); }

void blit_row(u16 sy, const u8 *src, u8 n) {
    u32 dst = BITMAP_LIN + (u32)(sy >> 3) * (COLS * 8) + (sy & 7);
    u8 i;
    if (sy >= SCR_H) {
        fprintf(stderr, "blit_row: row %u off the screen\n", sy);
        return;
    }
    for (i = 0; i < n; i++) MEM[dst + i * 8] = src[i];
}

void color_fill(u8 row, u8 col, u8 n, u8 c) {
    u8 i;
    for (i = 0; i < n; i++) colram[((u16)row * COLS + col + i) * 2 + 1] = c;
}

void color_row(u8 row, const u8 *c) {
    u8 i;
    for (i = 0; i < COLS; i++) colram[((u16)row * COLS + i) * 2 + 1] = c[i];
}

/* the fonts' glyphs, as BASIC's BLOAD puts them (the file's 2-byte load
 * address skipped) to the scratch area, then installed as BASIC has them */
void video_on(void) {
    static int loaded;
    FILE *f;
    if (loaded) return;
    f = fopen("src/writer.fnt", "rb");
    if (!f) {
        perror("src/writer.fnt");
        return;
    }
    fgetc(f);
    fgetc(f);
    if (fread(MEM + SCRATCH_LIN, 1, SCRATCH_MAX, f) == 0) perror("src/writer.fnt");
    fclose(f);
    font_install();
    loaded = 1;
}
void video_off(void) {}

/* the keyboard: what the tests say is held */
u8 host_mods, host_ctrl_crsr, host_frames;
u8 kbd_mods(void) { return host_mods; }
u8 kbd_ctrl_crsr(void) { return host_ctrl_crsr; }
u8 frames(void) { return host_frames; }

static const unsigned char pal[16][3] = {
    {0, 0, 0}, {255, 255, 255}, {170, 50, 40}, {110, 210, 220}, {140, 60, 160}, {90, 170, 70},
    {50, 40, 170}, {210, 220, 110}, {150, 90, 30}, {100, 70, 0}, {200, 110, 100}, {68, 68, 68},
    {120, 120, 120}, {150, 230, 140}, {110, 100, 230}, {170, 170, 170},
};

void screen_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    int x, y;
    fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
    for (y = 0; y < SCR_H; y++)
        for (x = 0; x < SCR_W; x++) {
            int cell = (y >> 3) * COLS + (x >> 3);
            u8 b = MEM[BITMAP_LIN + cell * 8 + (y & 7)];
            int on = (b >> (7 - (x & 7))) & 1;
            const unsigned char *c = on ? pal[colram[cell * 2 + 1] & 15] : pal[1];
            fwrite(c, 1, 3, f);
        }
    fclose(f);
}
