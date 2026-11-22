/* list.c — LIST.OVL: list mode, as vi's :set list (GitHub issue #2)
 *
 * The invisible structure of the text marked over each line render.c
 * draws (list_marks: hk_l, hk_L), in either view, XORed into the band
 * (so a marker in the cursor or a selection shows inverted) and never
 * moving the text:
 *
 *   space                  a dot (raw view: the middle-dot cell)
 *   trailing spaces        (the spaces before a paragraph's end, on its last
 *                          line) a block: raw view, red reversed cells
 *   paragraph end          page view: the raw font's pilcrow (raw view
 *                          shows its own)
 *   soft wrap              (a line the paragraph goes on after) an arrow,
 *                          after its last cell, or in the right margin
 *   past the end           raw view: ~ in the rows below the last line
 *
 * A module, resident (wp.h): linked by tools/mkmod.py at list_area, it calls
 * into the program (the layout's walk along a line: just_w, raw_tag).
 */
#include "wp.h"

#define BAND mod_area.r.band
#define CELLCOL mod_area.r.cellcol

/* markers: 8 x 13, a byte a row, the raw font's cell */
static const u8 g_dot[13] = { 0, 0, 0, 0, 0, 0, 0x18, 0x18, 0, 0, 0, 0, 0 };
static const u8 g_wrap[13] = { 0, 0, 0, 0x02, 0x02, 0x02, 0x22, 0x62, 0xFE, 0x60, 0x20, 0, 0 };
static u8 gb[13];

/* the raw font's glyph c into gb */
static const u8 *raw_glyph(u8 c) {
    mem_read(gb, font_raw_at + (u16)((c - FONT_FIRST) * 13), 13);
    return gb;
}

/* g's 13 rows XORed into the band at pixel x, from band row top (above
 * the band: those rows left out) */
static void blit(const u8 *g, u16 x, int16_t top) {
    u8 r, sh = (u8)(x & 7), col = (u8)(x >> 3), *d;
    int16_t row;
    if (col >= COLS) return;
    for (r = 0; r < 13; r++) {
        row = (int16_t)(top + r);
        if (row < 0 || row >= BAND_H || !g[r]) continue;
        d = BAND + (u8)row * COLS + col;
        d[0] ^= (u8)(g[r] >> sh);
        if (sh && col + 1 < COLS) d[1] ^= (u8)(g[r] << (8 - sh));
    }
}

static u8 is_end(u8 c) { return c == CH_CR || c == CH_FF; }

/* the first of the spaces before the paragraph's end on line [i, e)
 * (0xFFFF: the line does not end the paragraph) */
static u16 trailing(u16 i, u16 e) {
    u16 t = (u16)(e - 1);
    if (!is_end(txt_at(t))) return 0xFFFF;
    while (t > i && txt_at(t - 1) == ' ') t--;
    return t;
}

static void raw_marks(void) {
    u16 i, e, tr;
    u8 col = 0, c, r, *d;
    if (hk_l >= nlines) {
        blit(raw_glyph('~'), 0, 1);
        CELLCOL[0] = C_LBLUE;
        return;
    }
    i = hk_L->start;
    e = line_end(hk_l);
    tr = trailing(i, e);
    for (; i < e && col < COLS; i++) {
        c = txt_at(i);
        col = (u8)(col + raw_tag(i, 0));
        if (c == CH_FF) continue;
        if (col >= COLS) break;
        if (c == ' ') {
            blit(g_dot, (u16)col * 8, 1);
            if (i >= tr) {
                d = BAND + col;
                for (r = 0; r < RAW_H; r++, d += COLS) *d ^= 0xFF;
                CELLCOL[col] = C_RED;
            }
        }
        col++;
    }
    if (col < COLS && !is_end(txt_at(e - 1))) {
        blit(g_wrap, (u16)col * 8, 1);
        CELLCOL[col] = C_GREY;
    }
}

static void page_marks(void) {
    const struct line *L = hk_L;
    u16 i = L->start, e = line_end(hk_l), x = (u16)(TEXT_X + L->x), tr, m;
    u8 asc = L->asc, c, a, w, fa;
    struct just j;
    just_init(&j, L);
    tr = trailing(i, e);
    for (; i < e; i++) {
        c = txt_at(i);
        a = sty(atr_at(i));
        if (is_end(c)) {
            blit(raw_glyph(G_PILCROW), x, (int16_t)(asc - 10));
            continue;
        }
        w = just_w(c, a, &j);
        if (c == ' ') {
            fa = font_of(a)->ascent;
            if (i >= tr) {
                band_xor(x, (u16)(x + w), (u8)(asc - (fa >> 1)), (u8)(asc + 1));
            } else {
                m = (u16)(x + (w >> 1));
                c = (u8)(asc - (fa >> 2));
                band_xor((u16)(m - 1), (u16)(m + 1), (u8)(c - 1), (u8)(c + 1));
            }
        }
        x = (u16)(x + w);
    }
    if (!is_end(txt_at(e - 1))) blit(g_wrap, TEXT_X + TEXT_W + 4, (int16_t)(asc - 10));
}

void list_main(void) {
    if (view_mode == VIEW_RAW) raw_marks();
    else page_marks();
}
