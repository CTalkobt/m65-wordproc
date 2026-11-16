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

/* the registers we change, saved for video_off */
static const u8 saved_regs[] = {
    0x31, 0x54, 0x58, 0x59, 0x5D, 0x5E, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65,
    0x68, 0x69, 0x6A, 0x7B, 0x20, 0x21,
};
#define NSAVED (sizeof saved_regs)
static u8 saved[NSAVED];
static u8 is_on, screen_made;

static void make_screen(void) {
    u16 i;
    u8 __far *p = (u8 __far *)SCREEN_LIN;
    /* cell n shows character n */
    for (i = 0; i < COLS * ROWS; i++) {
        *p++ = (u8)i;
        *p++ = (u8)(i >> 8);
    }
    /* colour RAM: the first byte of every cell 0, the colour black */
    dma_fill_far_step(COLRAM, 2, COLS * ROWS, 0);
    dma_fill_far_step(COLRAM + 1, 2, COLS * ROWS, C_BLACK);
    dma_fill_far(BITMAP_LIN, COLS * ROWS * 8, 0);
    screen_made = 1;
}

void video_on(void) {
    u8 i;
    VIC(0xD02F) = 0x47;         /* the VIC-IV's registers unlocked */
    VIC(0xD02F) = 0x53;
    if (!is_on) {
        for (i = 0; i < NSAVED; i++) saved[i] = VIC(0xD000 + saved_regs[i]);
        is_on = 1;
    }
    if (!screen_made) make_screen();
    VIC(0xD05D) &= 0x7F;        /* no hot registers: the KERNAL's writes leave ours */
    VIC(0xD031) |= 0x88;        /* H640, V400 (first: it resets others) */
    VIC(0xD031) &= (u8)~0x30;   /* no bitplanes, no VIC-III attributes */
    VIC(0xD011) &= (u8)~0x60;   /* text mode (no bitmap, no extended colour) */
    VIC(0xD016) &= (u8)~0x10;   /* no multicolour */
    VIC(0xD054) = (u8)((VIC(0xD054) & ~0x06) | 0x01);   /* 16-bit characters, none full-colour */
    VIC(0xD058) = COLS * 2;     /* bytes a row */
    VIC(0xD059) = 0;
    VIC(0xD05E) = COLS;         /* characters a row */
    VIC(0xD063) = 0;            /* (their high bits; the screen address's bits 24-27) */
    VIC(0xD060) = (u8)SCREEN_LIN;
    VIC(0xD061) = (u8)(SCREEN_LIN >> 8);
    VIC(0xD062) = (u8)(SCREEN_LIN >> 16);
    VIC(0xD064) = (u8)COLOR_OFS;
    VIC(0xD065) = (u8)(COLOR_OFS >> 8);
    VIC(0xD068) = (u8)BITMAP_LIN;
    VIC(0xD069) = (u8)(BITMAP_LIN >> 8);
    VIC(0xD06A) = (u8)(BITMAP_LIN >> 16);
    VIC(0xD07B) = ROWS - 1;
    VIC(0xD020) = C_DGREY;
    VIC(0xD021) = C_WHITE;
}

void video_off(void) {
    u8 i;
    if (!is_on) return;
    VIC(0xD02F) = 0x47;
    VIC(0xD02F) = 0x53;
    VIC(0xD031) = saved[0];     /* first: it resets others */
    for (i = 1; i < NSAVED; i++) VIC(0xD000 + saved_regs[i]) = saved[i];
    is_on = 0;
}

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
