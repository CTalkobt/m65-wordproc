/* m65.c — the MEGA65 side of the platform layer (wp.h): far memory, the
 * DMA, and the VIC-IV's 640 x 400 screen of 16-bit characters
 *
 * The screen: H640 and V400 ($D031), 16-bit character numbers ($D054 bit
 * 0; full-colour characters off), 80 characters a row (160 bytes), 50 rows.
 * The screen RAM at SCREEN_LIN numbers the cells 0..3999 along the rows, and
 * the character set pointer ($D068-$D06A) is BITMAP_LIN, so cell n shows the
 * 8 bytes at BITMAP_LIN + 8n. The cells' colours are in colour RAM from
 * COLOR_OFS (2 bytes a cell: 0, then the foreground colour), clear of the
 * KERNAL's own screen's. The background ($D021) is white.
 */
#include <dma.h>
#include "wp.h"

#define VIC(r) (*(volatile u8 *)(r))
#define COLRAM (0xFF80000UL + COLOR_OFS)


u8 mem_get(u32 a) { return *(u8 __far *)a; }
void mem_put(u32 a, u8 v) { *(u8 __far *)a = v; }
void mem_copy(u32 dst, u32 src, u16 n) {
    if (n) dma_copy_far(dst, src, n);
}
void mem_read(u8 *dst, u32 src, u16 n) {
    dma_copy_far((u32)(u16)dst, src, n);
}
void mem_write(u32 dst, const u8 *src, u16 n) {
    dma_copy_far(dst, (u32)(u16)src, n);
}
void mem_fill(u32 dst, u16 n, u8 v) {
    if (n) dma_fill_far(dst, n, v);
}

void blit_row(u16 sy, const u8 *src, u8 n) {
    u32 dst = BITMAP_LIN + (u32)(sy >> 3) * (COLS * 8) + (sy & 7);
    dma_copy_far_step(dst, 8, (u32)(u16)src, 1, n);
}

void color_fill(u8 row, u8 col, u8 n, u8 c) {
    dma_fill_far_step(COLRAM + ((u16)row * COLS + col) * 2 + 1, 2, n, c);
}

void color_row(u8 row, const u8 *c) {
    dma_copy_far_step(COLRAM + (u16)row * COLS * 2 + 1, 2, (u32)(u16)c, 1, COLS);
}

/* (video_on, video_off: cold.c, an overlay) */

/* ---- the keyboard ---- */
u8 kbd_mods(void) { return VIC(0xD611); }

/* CTRL with a cursor key, held: its code. The matrix has one key for
 * right / left and one for down / up; left and up are those with SHIFT, or
 * the MEGA65's own left and up keys ($D60F bits 0, 1).
 *
 * $D60F is checked first, on its own: the hardware sends the own left / up
 * keys as SHIFT + CRSR-right / down, but whether the direct matrix scan
 * ($D613) shows that virtual press is not confirmed on a real MEGA65 (xemu
 * tests press SHIFT + the cursor key, not these keys). Were it not shown,
 * the matrix test below would miss CTRL + left / up, and the plain left / up
 * the KERNAL queues for them is ignored while CTRL is held (wp_key), so they
 * would do nothing at all. */
u8 kbd_ctrl_crsr(void) {
    u8 m = VIC(0xD611), lu = VIC(0xD60F), v;
    if (!(m & KM_CTRL)) return 0;
    if (lu & 1) return 157;
    if (lu & 2) return 145;
    VIC(0xD614) = 0;            /* the matrix's first 8 keys: DEL RETURN CRSR-H F7 F1 F3 F5 CRSR-V */
    v = VIC(0xD613);            /* (0: pressed) */
    if (!(v & 0x04)) return m & KM_SHIFT ? 157 : 29;
    if (!(v & 0x80)) return m & KM_SHIFT ? 145 : 17;
    return 0;
}

u8 frames(void) { return VIC(0xD7FA); }
