/* render.c — the screen drawn from the line table
 *
 * Each line is drawn into a band (BAND_H rows of 80 bytes, a bit a pixel,
 * in bank 0) and its rows copied to the screen's bitmap by DMA, so a line
 * is never seen half drawn. Bold is the glyph drawn twice a pixel apart,
 * italic each row shifted right by a quarter of its height above the
 * baseline, underline a row two below the baseline.
 */
#include "wp.h"

char msg[81];
char fname[17];

static u8 band[BAND_H * COLS];
static u8 row_desk[COLS];       /* the desk between pages */
static u8 row_page[COLS];       /* a page's blank row: desk at the sides */
static u8 row_blank[COLS];
static u8 rowcol[ROWS];         /* each cell row's colour scheme (0xFF: unknown) */
static u8 cellcol[COLS];
static u8 desk_row[ROWS];
static u8 inited;

/* What was drawn where: a segment of the view (a line's band, blank page,
 * desk) starting at a screen row, with a hash of what it shows. A segment
 * the same as in the last frame, at the same row, is not drawn again. */
#define SEG_DESK 1
#define SEG_PAGE 2
#define SEG_LINE 3
#define SEG_FOOT 4
#define SEG_RAW 5
#define MAXSEG 64
struct seg {
    u16 sy, n, hash;
    u8 kind, r0;
};
static struct seg segs[2][MAXSEG];     /* this frame's and the last one's, in screen order */
static u8 nseg[2], set, prev_i;
static char bar_text[2][COLS + 2];     /* what each bar shows ("" : unknown) */
static u16 crc_tab[256];
static void crc_init(void);

extern u8 npages;

#define SCHEME_PAGE 1           /* black text, grey desk at the sides */
#define SCHEME_DESK 2           /* all grey */
#define SCHEME_RAW 3            /* per cell (always rewritten) */
#define SCHEME_BAR 4

static void init_rows(void) {
    u8 i;
    for (i = 0; i < COLS; i++) {
        row_desk[i] = 0xFF;
        row_blank[i] = 0;
        row_page[i] = (i < PAGE_X / 8 || i >= (PAGE_X + PAGE_W) / 8) ? 0xFF : 0;
    }
    for (i = 0; i < ROWS; i++) rowcol[i] = 0xFF;
    crc_init();
    inited = 1;
}

void render_invalidate(void) {
    u16 i;
    for (i = 0; i < ROWS; i++) rowcol[i] = 0xFF;
    nseg[0] = nseg[1] = 0;
    bar_text[0][0] = bar_text[1][0] = 0;
}

static void new_frame(void) {
    set ^= 1;
    nseg[set] = 0;
    prev_i = 0;
}

/* the segment at screen row sy: 1 if the last frame drew the same there
 * (it is recorded either way) */
static u8 seg_same(u16 sy, u8 kind, u8 r0, u16 n, u16 hash) {
    struct seg *p = segs[set ^ 1], *c;
    u8 np = nseg[set ^ 1], same;
    while (prev_i < np && p[prev_i].sy < sy) prev_i++;
    same = prev_i < np && p[prev_i].sy == sy && p[prev_i].kind == kind && p[prev_i].r0 == r0 && p[prev_i].n == n &&
           p[prev_i].hash == hash;
    if (nseg[set] < MAXSEG) {
        c = &segs[set][nseg[set]++];
        c->sy = sy;
        c->kind = kind;
        c->r0 = r0;
        c->n = n;
        c->hash = hash;
    } else {
        same = 0;
    }
    return same;
}

/* CRC-16-CCITT of the bytes of v (two characters' changes never cancel) */
static u16 hmix(u16 h, u16 v) {
    h = (u16)((h << 8) ^ crc_tab[(u8)((h >> 8) ^ (u8)v)]);
    return (u16)((h << 8) ^ crc_tab[(u8)((h >> 8) ^ (u8)(v >> 8))]);
}

static void crc_init(void) {
    u16 i, c;
    u8 k;
    for (i = 0; i < 256; i++) {
        c = (u16)(i << 8);
        for (k = 0; k < 8; k++) c = (c & 0x8000) ? (u16)((c << 1) ^ 0x1021) : (u16)(c << 1);
        crc_tab[i] = c;
    }
}

/* what line l shows: its characters, attributes, placement, and the cursor
 * and selection in it (raw view: also the style before it and its
 * paragraph's alignment, which its tags show) */
static u16 line_hash(u16 l) {
    u16 i = LINES[l].start, e = line_end(l), h = 0x5A17;
    h = hmix(h, LINES[l].x);
    h = hmix(h, (u16)(LINES[l].extra | (LINES[l].nsp << 8)));
    h = hmix(h, (u16)(LINES[l].h | (LINES[l].asc << 8)));
    h = hmix(h, (u16)(e - i));
    if (view_mode == VIEW_RAW) {
        h = hmix(h, prev_style(i));
        h = hmix(h, ATR[para_end(i)]);
    }
    for (; i < e; i++) h = hmix(h, (u16)(TXT[i] | (ATR[i] << 8)));
    i = LINES[l].start;
    if (cur >= i && cur < e) h = hmix(h, (u16)(0x8000 | (cur - i)));
    if (sel_on) {
        u16 lo = sel_lo(), hi = sel_hi();
        if (lo < e && hi > i) {
            h = hmix(h, (u16)(0x4000 | (lo > i ? lo - i : 0)));
            h = hmix(h, (u16)(0x2000 | (hi < e ? hi - i : e - i)));
        }
    }
    return h;
}


/* ---- drawing into the band ---- */
static void band_fill(u8 rows, const u8 *pattern) {
    u8 r, i;
    u8 *d = band;
    for (r = 0; r < rows; r++) {
        for (i = 0; i < COLS; i++) d[i] = pattern[i];
        d += COLS;
    }
}

/* the pixels x0..x1-1 of rows r0..r1-1 inverted */
static void band_xor(u16 x0, u16 x1, u8 r0, u8 r1) {
    u8 r;
    u16 x;
    if (x1 > SCR_W) x1 = SCR_W;
    for (r = r0; r < r1 && r < BAND_H; r++) {
        u8 *d = band + r * COLS;
        for (x = x0; x < x1;) {
            if ((x & 7) == 0 && x + 8 <= x1) {
                d[x >> 3] ^= 0xFF;
                x += 8;
            } else {
                d[x >> 3] ^= (u8)(0x80 >> (x & 7));
                x++;
            }
        }
    }
}

static void band_hline(u16 x0, u16 x1, u8 r) {
    u16 x;
    u8 *d;
    if (r >= BAND_H) return;
    d = band + r * COLS;
    if (x1 > SCR_W) x1 = SCR_W;
    for (x = x0; x < x1; x++) d[x >> 3] |= (u8)(0x80 >> (x & 7));
}

static u8 gbuf[48];             /* a glyph's rows, copied from bank 4 */

/* glyph c of font f, its cell's top at band row top, at pixel x */
static void draw_glyph(const struct font *f, u8 c, u16 x, u8 top, u8 style) {
    const u8 *g = gbuf;
    u8 r, w, two, h = f->height, asc = f->ascent;
    if (c < FONT_FIRST || c > FONT_LAST) c = '?';
    c -= FONT_FIRST;
    w = f->w[c];
    two = w > 8;
    mem_read(gbuf, f->bits + f->o[c], two ? (u16)(h * 2) : h);
    for (r = 0; r < h; r++) {
        u16 px = x;
        u8 b0 = *g++, b1 = 0, b2, sh, row = (u8)(top + r);
        u8 *d;
        if (two) b1 = *g++;
        if (row >= BAND_H) continue;
        if (!(b0 | b1)) continue;
        if (style & A_ITAL) {
            if (r < asc) px += (u16)((asc - r) >> 2);
            else if (px) px -= (u16)((r - asc + 3) >> 2);
        }
        b2 = 0;
        if (style & A_BOLD) {
            b2 = (u8)(b1 << 7);
            b1 = (u8)(b1 | (b1 >> 1) | (b0 << 7));
            b0 = (u8)(b0 | (b0 >> 1));
        }
        sh = (u8)(px & 7);
        d = band + row * COLS + (px >> 3);
        if (sh) {
            b2 = (u8)((b2 >> sh) | (b1 << (8 - sh)));
            b1 = (u8)((b1 >> sh) | (b0 << (8 - sh)));
            b0 = (u8)(b0 >> sh);
        }
        px >>= 3;
        if (px < COLS) d[0] |= b0;
        if (px + 1 < COLS) d[1] |= b1;
        if (px + 2 < COLS) d[2] |= b2;
    }
}

/* raw font text at cell col (an 8 x 16 cell row band); inv: inverted */
static void raw_cell(u8 col, u8 c) {
    const u8 *g;
    u8 r, *d = band + COLS + col;
    if (c < FONT_FIRST || c > FONT_LAST) c = '?';
    g = font_raw_rows + (u16)(c - FONT_FIRST) * 13;
    for (r = 0; r < 13; r++) {
        d[0] = g[r];
        d += COLS;
    }
}

static void raw_cell_inv(u8 col, u8 c) {
    const u8 *g;
    u8 r, *d = band + COLS + col;
    if (c < FONT_FIRST || c > FONT_LAST) c = '?';
    g = font_raw_rows + (u16)(c - FONT_FIRST) * 13;
    for (r = 0; r < 13; r++) {
        d[0] = (u8)~g[r];
        d += COLS;
    }
}

static void raw_invert_cell(u8 col) {
    u8 r, *d = band + col;
    for (r = 0; r < RAW_H; r++) {
        d[0] ^= 0xFF;   /* (not *d ^= 0xFF: cc45 miscompiles a bitwise compound assignment of a constant through *p) */
        d += COLS;
    }
}

static void band_out(u16 sy, u8 r0, u8 n) {
    u8 i;
    for (i = 0; i < n; i++) blit_row((u16)(sy + i), band + (r0 + i) * COLS);
}

static void rows_out(u16 sy, u16 n, const u8 *pattern) {
    while (n--) blit_row(sy++, pattern);
}

/* ---- numbers and strings ---- */
static u8 put_str(char *d, u8 n, const char *s) {
    while (*s && n < 80) d[n++] = *s++;
    return n;
}

static u8 put_num(char *d, u8 n, u16 v) {
    char t[6];
    u8 k = 0;
    do {
        t[k++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (k && n < 80) d[n++] = t[--k];
    return n;
}

/* ---- the bars ---- */
static void bar(u8 which, const char *left, const char *right) {
    u8 i, n = 0, rn = 0;
    char t[COLS + 2], *o = bar_text[which];
    /* the bar as text: the left part, the right part at the end */
    for (i = 0; i < COLS; i++) t[i] = ' ';
    while (left[n] && n < COLS) {
        t[n] = left[n];
        n++;
    }
    while (right[rn]) rn++;
    for (i = 0; i < rn && i < COLS; i++) t[COLS - rn + i] = right[i];
    t[COLS] = '|';              /* (never "": a bar drawn) */
    t[COLS + 1] = 0;
    for (i = 0; i <= COLS && t[i] == o[i]; i++) ;
    if (i > COLS) return;
    band_fill(RAW_H, row_desk);
    for (i = 0; i < COLS; i++) {
        o[i] = t[i];
        if (t[i] != ' ') raw_cell_inv(i, (u8)t[i]);
    }
    o[COLS] = '|';
    o[COLS + 1] = 0;
    band_out(which ? VIEW_TOP + VIEW_H : 0, 0, RAW_H);
}

static void bar_colour(u8 row) {
    if (rowcol[row] != SCHEME_BAR) {
        color_fill(row, 0, COLS, C_BLUE);
        color_fill((u8)(row + 1), 0, COLS, C_BLUE);
        rowcol[row] = rowcol[row + 1] = SCHEME_BAR;
    }
}

static const char *const align_names[4] = { "Left", "Centre", "Right", "Justify" };

void render_bars(void) {
    char l[81], r[41];
    u8 n = 0, a = typing_attr;
    u16 li = nlines ? line_of(cur) : 0;
    if (!inited) init_rows();
    n = put_str(l, n, " MEGA65 Writer  ");
    n = put_str(l, n, fname);
    if (dirty) n = put_str(l, n, " *");
    l[n] = 0;
    n = 0;
    n = put_str(r, n, view_mode == VIEW_RAW ? "RAW VIEW " : "PAGE LAYOUT ");
    r[n] = 0;
    bar(0, l, r);
    bar_colour(0);

    if (msg[0]) {
        n = put_str(l, 0, " ");
        n = put_str(l, n, msg);
        l[n] = 0;
        r[0] = 0;
    } else {
        n = 0;
        if (view_mode == VIEW_PAGE) {
            n = put_str(l, n, " Pg ");
            n = put_num(l, n, (u16)(LINES[li].page + 1));
            n = put_str(l, n, "/");
            if (layout_done) n = put_num(l, n, npages);
            else n = put_str(l, n, "?");
        }
        n = put_str(l, n, " Ln ");
        n = put_num(l, n, (u16)(li + 1));
        n = put_str(l, n, " Col ");
        n = put_num(l, n, (u16)(cur - LINES[li].start + 1));
        n = put_str(l, n, "  ");
        n = put_str(l, n, font_names[a & A_FONT]);
        n = put_str(l, n, (a & A_BOLD) ? " B" : " -");
        n = put_str(l, n, (a & A_ITAL) ? "I" : "-");
        n = put_str(l, n, (a & A_UNDER) ? "U " : "- ");
        n = put_str(l, n, align_names[(ATR[para_end(cur)] & A_ALIGN) >> A_ALIGN_SHIFT]);
        if (sel_on) {
            n = put_str(l, n, "  SEL ");
            n = put_num(l, n, (u16)(sel_hi() - sel_lo()));
        }
        l[n] = 0;
        n = 0;
        n = put_num(r, n, doc_len - 1);
        n = put_str(r, n, " chars  F8 Help  ESC Menu ");
        r[n] = 0;
    }
    bar(1, l, r);
    bar_colour(ROWS - 2);
}

/* ---- page view ---- */
static void mark_desk(u16 sy, u16 n) {
    while (n--) desk_row[sy++ >> 3] = 1;
}

/* line l into the band (rows 0..h-1) */
static void draw_line(u16 l) {
    u16 i = LINES[l].start, e = line_end(l), x, lo = 0, hi = 0, cx = 0xFFFF;
    u8 h = LINES[l].h, asc = LINES[l].asc, extra = LINES[l].extra, nsp = LINES[l].nsp, k = 0;
    if (h > BAND_H) h = BAND_H;
    band_fill(h, row_page);
    if (sel_on) {
        lo = sel_lo();
        hi = sel_hi();
    }
    x = (u16)(TEXT_X + LINES[l].x);
    for (; i < e; i++) {
        u8 c = TXT[i], a = ATR[i], w;
        const struct font *f = fonts[a & A_FONT];
        if (i == cur) cx = x;
        if (c == CH_CR || c == CH_FF) {
            if (sel_on && i >= lo && i < hi) band_xor(x, x + 4, 0, h);
            continue;
        }
        w = char_w(c, a);
        if (c == ' ' && k < nsp) {
            u8 s = (u8)(((u16)extra * (k + 1)) / nsp - ((u16)extra * k) / nsp);
            k++;
            w += s;
        } else if (c != ' ') {
            draw_glyph(f, c, x, (u8)(asc - f->ascent), a);
        }
        if (a & A_UNDER) band_hline(x, x + w, (u8)(asc + 2));
        if (sel_on && i >= lo && i < hi) band_xor(x, x + w, 0, h);
        x += w;
    }
    if (cx != 0xFFFF) band_xor(cx ? cx - 1 : 0, cx + 1, 0, h);
}

/* the page number in the bottom margin */
#define FOOT_H 15
#define FOOT_Y (PAGE_H - 34)
static void draw_footer(u8 page) {
    char s[12];
    u8 n = 0;
    u16 w = 0, x;
    const struct font *f = &font_sans;
    band_fill(FOOT_H, row_page);
    n = put_str(s, n, "- ");
    n = put_num(s, n, (u16)(page + 1));
    n = put_str(s, n, " -");
    s[n] = 0;
    for (n = 0; s[n]; n++) w += char_w((u8)s[n], 0);
    x = (u16)(PAGE_X + (PAGE_W - w) / 2);
    for (n = 0; s[n]; n++) {
        draw_glyph(f, (u8)s[n], x, 0, 0);
        x += char_w((u8)s[n], 0);
    }
}

static void render_page(void) {
    u16 sy = VIEW_TOP, dy = view_y, l = 0, lo, hi, mid;
    u8 r;
    new_frame();
    for (r = 0; r < ROWS; r++) desk_row[r] = 0;
    /* the first line not above the view */
    if (nlines) {
        lo = 0;
        hi = nlines;
        while (lo < hi) {
            mid = (u16)((lo + hi) / 2);
            if (LINES[mid].y + LINES[mid].h <= dy) lo = (u16)(mid + 1);
            else hi = mid;
        }
        l = lo;
    }
    while (sy < VIEW_TOP + VIEW_H) {
        u16 rem = VIEW_TOP + VIEW_H - sy, n;
        u16 p = dy / PAGE_STRIDE, off = dy - p * PAGE_STRIDE, ptop = p * PAGE_STRIDE + PAGE_GAP;
        if (off < PAGE_GAP || p >= npages) {
            /* the desk: between pages, or below the last */
            n = off < PAGE_GAP ? PAGE_GAP - off : (p + 1) * PAGE_STRIDE - dy;
            if (n > rem) n = rem;
            if (!seg_same(sy, SEG_DESK, 0, n, 0)) rows_out(sy, n, row_desk);
            mark_desk(sy, n);
        } else if (l < nlines && LINES[l].y <= dy) {
            u8 r0 = (u8)(dy - LINES[l].y);
            n = LINES[l].h - r0;
            if (n > rem) n = rem;
            if (!seg_same(sy, SEG_LINE, r0, n, line_hash(l))) {
                draw_line(l);
                band_out(sy, r0, (u8)n);
            }
            l++;
        } else if (dy >= ptop + FOOT_Y && dy < ptop + FOOT_Y + FOOT_H) {
            u8 r0 = (u8)(dy - ptop - FOOT_Y);
            n = FOOT_H - r0;
            if (n > rem) n = rem;
            if (!seg_same(sy, SEG_FOOT, r0, n, p)) {
                draw_footer((u8)p);
                band_out(sy, r0, (u8)n);
            }
        } else {
            u16 lim = ptop + PAGE_H;
            if (l < nlines && LINES[l].y < lim) lim = LINES[l].y;
            if (dy < ptop + FOOT_Y && ptop + FOOT_Y < lim) lim = ptop + FOOT_Y;
            n = lim - dy;
            if (n > rem) n = rem;
            if (!seg_same(sy, SEG_PAGE, 0, n, 0)) rows_out(sy, n, row_page);
        }
        sy += n;
        dy += n;
    }
    for (r = VIEW_TOP / 8; r < (VIEW_TOP + VIEW_H) / 8; r++) {
        u8 s = desk_row[r] ? SCHEME_DESK : SCHEME_PAGE;
        if (rowcol[r] == s) continue;
        if (rowcol[r] != SCHEME_DESK && rowcol[r] != SCHEME_PAGE) {
            color_fill(r, 0, PAGE_X / 8, C_GREY);
            color_fill(r, (PAGE_X + PAGE_W) / 8, COLS - (PAGE_X + PAGE_W) / 8, C_GREY);
        }
        color_fill(r, PAGE_X / 8, PAGE_W / 8, s == SCHEME_DESK ? C_GREY : C_BLACK);
        rowcol[r] = s;
    }
}

/* ---- raw view ---- */
static u8 tag_colour(const char *t) {
    /* t: the tag's text after '[' */
    if (*t == '/') t++;
    if ((t[0] == 'B' || t[0] == 'I' || t[0] == 'U') && t[1] == ']') return C_BLUE;
    if (t[0] == 'C' || t[0] == 'R' || t[0] == 'J' || t[0] == 'P') return C_RED;
    return C_PURPLE;
}

static void render_raw(void) {
    u8 vr, col, r;
    u16 l = view_y / RAW_H, lo = 0, hi = 0;
    char tags[48];
    new_frame();
    if (sel_on) {
        lo = sel_lo();
        hi = sel_hi();
    }
    for (vr = 0; vr < VIEW_H / RAW_H; vr++, l++) {
        u16 sy = (u16)(VIEW_TOP + vr * RAW_H);
        u8 crow = (u8)(sy >> 3);
        if (seg_same(sy, SEG_RAW, 0, RAW_H, l < nlines ? line_hash(l) : 0)) continue;
        for (col = 0; col < COLS; col++) cellcol[col] = C_BLACK;
        band_fill(RAW_H, row_blank);
        if (l < nlines) {
            u16 i = LINES[l].start, e = line_end(l);
            col = 0;
            for (; i < e && col < COLS; i++) {
                u8 c = TXT[i], n = raw_tag(i, tags), k, tc = C_BLUE;
                for (k = 0; k < n && col < COLS; k++) {
                    if (tags[k] == '[') tc = tag_colour(tags + k + 1);
                    raw_cell(col, (u8)tags[k]);
                    cellcol[col] = tc;
                    if (c == CH_FF && i == cur && k == n - 1) raw_invert_cell(col);
                    col++;
                }
                if (c == CH_FF) continue;
                if (col >= COLS) break;
                if (c == CH_CR) {
                    raw_cell(col, G_PILCROW);
                    cellcol[col] = C_GREY;
                } else {
                    raw_cell(col, c);
                }
                if (sel_on && i >= lo && i < hi) raw_invert_cell(col);
                if (i == cur) raw_invert_cell(col);
                col++;
            }
        }
        band_out(sy, 0, RAW_H);
        for (r = 0; r < 2; r++) {
            color_row((u8)(crow + r), cellcol);
            rowcol[crow + r] = SCHEME_RAW;
        }
    }
}

void render(void) {
    if (!inited) init_rows();
    if (view_mode == VIEW_RAW) render_raw();
    else render_page();
    render_bars();
}

/* PETSCII (a GET's code, the lower-case set) as ASCII; 0 if none */
u8 petscii_to_ascii(u8 c) {
    if (c >= 0x41 && c <= 0x5A) return (u8)(c + 32);
    if (c >= 0xC1 && c <= 0xDA) return (u8)(c - 128);
    if (c >= 0x61 && c <= 0x7A) return (u8)(c - 32);
    if (c >= 0x20 && c <= 0x40) return c;
    if (c == 0x5B || c == 0x5D) return c;
    if (c == 0x5C) return '\\';         /* the pound sign's key */
    if (c == 0x5E) return '^';
    if (c == 0x5F) return '_';          /* the left arrow */
    if (c == 0xA4) return '_';
    if (c == 0xDB) return '{';
    if (c == 0xDD) return '}';
    if (c == 0xDC) return '|';
    if (c == 0xDE) return '~';
    return 0;
}
