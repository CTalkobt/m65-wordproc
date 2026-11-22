/* petfont.c — the C64 fonts made at the start (pet_fonts): on the MEGA65
 * a code overlay of its own, "fonts" (cc45 #pragma cc45 overlay), run once,
 * as cold.c's (whose rules it follows: called only from api.c's SYS entries
 * after ovl_fresh), which it would not fit beside in mod_area's buffers
 */
#include "wp.h"
#ifdef __cc45__
#pragma cc45 overlay(fonts)
#endif

/* ---- the C64 fonts (font_petl, font_petf; issues #5, #11) ----
 * Their glyphs are a C64 character set's (8 x 8), each row twice (8 x 16,
 * as the C128's 80 columns look): PETSCII.FNT's, which BASIC BLOADs to
 * the scratch area at PETFNT_OFS when it is on the disk (its upper-case /
 * graphics set, then perhaps its lower-case set: a set of 0s is none),
 * else the MEGA65's own, the character set in use (CHARROM_LIN). Kept at
 * PETG_LIN: the upper-case set's screen codes 0-127, then the lower-case
 * set's a-z; each font's records (as writer.fnt's: an advance, 8, and
 * the glyph's offset) at PET_LIN. */

/* a set's 2 KB (ofs: 0 the upper-case set, 2048 the lower-case one) */
static u32 pet_src(u16 ofs) {
    u32 a = SCRATCH_LIN + PETFNT_OFS + ofs;
    u16 i;
    for (i = 0; i < 2048; i++)
        if (mem_get(a + i)) return a;
    return CHARROM_LIN + ofs;
}

/* glyph g: the 8 rows at src, each twice */
static void pet_glyph(u8 g, u32 src) {
    u8 r[16], k;
    for (k = 0; k < 16; k++) r[k] = mem_get(src + (k >> 1));
    mem_write(PETG_LIN + (u16)g * 16, r, 16);
}

/* code c's record in the records at at: glyph g */
static void pet_rec(u32 at, u8 c, u8 g) {
    u8 e[3];
    u16 o = (u16)g * 16;
    e[0] = 8;
    e[1] = (u8)o;
    e[2] = (u8)(o >> 8);
    mem_write(at + (u16)(c - FONT_FIRST) * 3, e, 3);
}

void pet_fonts(void) {
    u32 up = pet_src(0), lo = pet_src(2048);
    u16 c;
    u8 s;
    for (c = 0; c < 128; c++) pet_glyph((u8)c, up + c * 8);
    for (c = 0; c < 26; c++) pet_glyph((u8)(128 + c), lo + (c + 1) * 8);
    for (c = FONT_FIRST; c < 256; c++) {
        /* font_petl: PETSCII code c's screen code (the control codes
         * $80-$9F: a space) */
        s = c < 0x40 ? (u8)c : c < 0x60 ? (u8)(c - 0x40) : c < 0x80 ? (u8)(c - 0x20) : c < 0xA0 ? 0x20
            : c < 0xC0 ? (u8)(c - 0x40) : c < 0xFF ? (u8)(c - 0x80) : 0x5E;
        pet_rec(PET_LIN, (u8)c, s);
        /* font_petf: Latin-1 c's glyph: ASCII as the C64 has it ([ \ ] ^ _
         * as its [ £ ] arrow up, arrow left: the keys that type them), a-z
         * the lower-case set's, | a line; what it lacks "?" */
        s = c < 0x40 ? (u8)c : c < 0x60 ? (u8)(c - 0x40) : (u8)(c - 'a') < 26 ? (u8)(c + 128 - 'a')
            : c == '|' ? 0x5D : c == 0xA0 ? ' ' : c == 0xA3 ? 0x1C : '?';
        pet_rec(PET_LIN + 672, (u8)c, s);
    }
}
