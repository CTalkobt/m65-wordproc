/* render.c — the screen drawn from the line table
 *
 * Each line is drawn into a band (BAND_H rows of 80 bytes, a bit a pixel,
 * in bank 0) and its rows copied to the screen's bitmap by DMA, so a line
 * is never seen half drawn. Bold is the glyph drawn twice a pixel apart,
 * italic each row shifted right by a quarter of its height above the
 * baseline, underline a row two below the baseline.
 */
#include "wp.h"
#include "msg.h"                /* MSG_MENU (made by tools/mkmsg.py) */
#ifdef __cc45__
#include "buildid.h"             /* BUILD_ID (made by the Makefile) */
#endif

char msg[81];
char fname[17];

static u8 inited;

/* What was drawn where (struct seg, wp.h): a segment the same as in the
 * last frame, at the same row, is not drawn again. */
#define SEG_DESK 1
#define SEG_PAGE 2
#define SEG_LINE 3
#define SEG_FOOT 4
#define SEG_RAW 5
static u8 nseg[2], set, prev_i;

union mod_area mod_area;
#define band mod_area.r.band
#define segs mod_area.r.segs
#define bar_text mod_area.r.bar_text
#define row_desk mod_area.r.row_desk
#define row_page mod_area.r.row_page
#define row_blank mod_area.r.row_blank
#define rowcol mod_area.r.rowcol
#define cellcol mod_area.r.cellcol
#define desk_row mod_area.r.desk_row
#define gbuf mod_area.r.gbuf
#define sb mod_area.r.sb

u8 panel;

/* LIST.OVL: at the end of mod_area (wp.h), past the drawing's buffers */
#define list_area (mod_area.m + LIST_OFS)
/* (the buffers' size, for tools/mkmod.py: a module placed at an offset
 * must be past them; cc45 takes a negative array size, so no check here) */
const u16 mod_bufsize = sizeof(mod_area.r);
u8 list_ok, list_on;
u16 hk_l;
const struct line *hk_L;

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
    inited = 1;
}

void render_invalidate(void) {
    inited = 0;                 /* (init_rows: the rows, rowcol unknown) */
    nseg[0] = nseg[1] = 0;
    bar_text[0][0] = bar_text[1][0] = bar_text[2][0] = bar_text[3][0] = 0;
}

/* a module's header: "WM" and the build's id (then a JMP to its entry) */
u8 mod_ok(const u8 *m) {
#ifdef __cc45__
    return m[0] == 'W' && m[1] == 'M' && m[2] == (u8)BUILD_ID && m[3] == (u8)(BUILD_ID >> 8);
#else
    (void)m;
    return 1;
#endif
}

u8 list_check(void) {
    list_ok = mod_ok(list_area);
    if (list_ok && list_on) {
        render_invalidate();
        render();
    }
    return list_ok;
}

/* LIST.OVL's markers over line l (*L), just drawn into the band */
static void list_marks(u16 l, const struct line *L) {
    hk_l = l;
    hk_L = L;
#ifdef __cc45__
    ((void (*)(void))&list_area[4])();
#else
    list_main();
#endif
}

/* Run the module BASIC loaded into mod_area (cmd: what it is to do; its
 * result, or 0xFFFF when there is none, or it is from another build). It
 * starts "WM", the build's id, then its entry. The header is cleared after
 * it (the drawing soon overwrites the rest), unless cmd says more calls
 * follow (MOD_MORE: BASIC draws nothing until the last). */
u16 mod_run(u8 cmd) {
    mod_ret = 0xFFFF;
#ifdef __cc45__
    if (mod_ok(mod_area.m)) {
        mod_cmd = cmd;
        ((void (*)(void))&mod_area.m[4])();
    }
    list_ok = 0;                /* (LIST.OVL overwritten: loaded again by BASIC) */
#else
    mod_cmd = cmd;
    if (cmd <= MOD_MD_IMPORT) md_main();
    else if (cmd == MOD_HTML_EXPORT) html_main();
    else if ((cmd & ~MOD_MORE) > MOD_HTML_EXPORT) htmlin_main();
    else rtf_main();
#endif
    doc_built();
    /* (not while more calls follow: the caches written here are in the
     * module's memory) */
    if (!(cmd & MOD_MORE)) {
        mod_area.m[0] = 0;
        render_invalidate();
    }
    return mod_ret;
}

static void new_frame(void) {
    set ^= 1;
    nseg[set] = 0;
    prev_i = 0;
}

/* the segment at screen row sy: 1 if the last frame drew the same there
 * (it is recorded either way) */
static u8 seg_same(u16 sy, u8 kind, u8 r0, u16 n, u32 hash) {
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

/* A line's hash: a Fletcher-style pair of 16-bit sums of its bytes (s1
 * their sum, s2 the sum of the running s1), two adds a byte. Any one byte
 * changed changes s1; two neighbours swapped change s2. 32 bits: with 16,
 * a stale line every few ten thousand changed lines (the host tests found
 * them, as they did for a CRC-16). */
static u16 hs1, hs2;

static void hmix(u16 v) {
    hs1 += (u8)v;
    hs2 += hs1;
    hs1 += (u8)(v >> 8);
    hs2 += hs1;
}

/* what line l shows: its characters, attributes, placement, and the cursor
 * and selection in it (raw view: also the style before it and its
 * paragraph's alignment, which its tags show) */
static u16 lnum;                /* a numbered item's number (line_hash, for draw_line) */

/* the first line of a list item (L_FIRST: its paragraph's first) */
#define list_first(L) (((L)->kind & (L_FIRST | A_KIND)) >= (L_FIRST | K_BULLET))

static u32 line_hash(u16 l, const struct line *L) {
    u16 i = L->start, e = line_end(l);
    hs1 = 0x17;
    hs2 = 0x5A;
    hmix((u16)(L->x | (L->kind << 9)));
    if (list_first(L) && (L->kind & A_KIND) == K_NUMBER) hmix(lnum = item_num(i));
    hmix((u16)(L->extra | (L->nsp << 8)));
    hmix((u16)(L->h | (L->asc << 8)));
    hmix((u16)(e - i));
    if (view_mode == VIEW_RAW) {
        /* (and whether it starts a paragraph: its alignment's tag) */
        hmix((u16)(prev_style(i) | ((i == 0 || txt_at(i - 1) == CH_CR || txt_at(i - 1) == CH_FF) << 8)));
        hmix(atr_at(para_end(i)));
    }
    for (; i < e; i++) {
        hs1 += txt_at(i);
        hs2 += hs1;
        hs1 += atr_at(i);
        hs2 += hs1;
    }
    i = L->start;
    if (cur >= i && cur < e) hmix((u16)(0x8000 | (cur - i)));
    if (sel_on) {
        u16 lo = sel_lo(), hi = sel_hi();
        if (lo < e && hi > i) {
            hmix((u16)(0x4000 | (lo > i ? lo - i : 0)));
            hmix((u16)(0x2000 | (hi < e ? hi - i : e - i)));
        }
    }
    return hs1 | ((u32)hs2 << 16);
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
void band_xor(u16 x0, u16 x1, u8 r0, u8 r1) {
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


/* glyph c of font f, its cell's top at band row top, at pixel x */
static void draw_glyph(const struct font *f, u8 c, u16 x, u8 top, u8 style) {
    const u8 *g = gbuf;
    u8 r, w, two, h = f->height, asc = f->ascent, e[3];
    if (NO_GLYPH(c)) c = '?';
    mem_read(e, f->offs + (u16)((c - FONT_FIRST) * 3), 3);
    w = e[0];
    two = w > 8;
    mem_read(gbuf, f->bits + (u16)(e[1] | (e[2] << 8)), two ? (u16)(h * 2) : h);
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

u32 raw_at(u8 c) {
    if (NO_GLYPH(c)) c = '?';
    return font_raw_at + (u16)((c - FONT_FIRST) * 13);
}

/* raw font text at cell col (an 8 x 16 cell row band); inv 0xFF: inverted */
static void raw_cell(u8 col, u8 c, u8 inv) {
    u8 r, *d = band + COLS + col;
    mem_read(gbuf, raw_at(c), 13);
    for (r = 0; r < 13; r++) {
        d[0] = (u8)(gbuf[r] ^ inv);
        d += COLS;
    }
}

static void raw_invert_cell(u8 col) {
    u8 r, *d = band + col;
    for (r = 0; r < RAW_H; r++) {
        *d ^= 0xFF;
        d += COLS;
    }
}

static void band_out(u16 sy, u8 r0, u8 n) {
    u8 i;
    for (i = 0; i < n; i++) blit_row((u16)(sy + i), band + (r0 + i) * COLS, COLS);
}

static void rows_out(u16 sy, u16 n, const u8 *pattern) {
    while (n--) blit_row(sy++, pattern, COLS);
}

/* ---- text for the bars and the page numbers: put into sb (sn long) ---- */
static u8 sn;

static void put_str(const char *s) {
    while (*s && sn < COLS) sb[sn++] = *s++;
}

static void put_num(u16 v) {
    char t[6];
    u8 k = 0;
    do {
        t[k++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (k && sn < COLS) sb[sn++] = t[--k];
}

/* ---- the bars ---- */
/* the top bar's style field: cells BOX_LO..BOX_HI-1, not inverted */
#define BOX_LO 1
#define BOX_HI 14

/* bar which from sb: its left part sb[0..ln-1], at its right end sb[ln..sn-1] */
static void bar(u8 which, u8 ln, u16 sy) {
    u8 i, rn = (u8)(sn - ln);
    char *t = mod_area.r.bar_t, *o = bar_text[which];
    for (i = 0; i < COLS; i++) t[i] = ' ';
    for (i = 0; i < ln; i++) t[i] = sb[i];
    for (i = 0; i < rn; i++) t[COLS - rn + i] = sb[ln + i];
    t[COLS] = '|';              /* (never "": a bar drawn) */
    t[COLS + 1] = 0;
    for (i = 0; i <= COLS && t[i] == o[i]; i++) ;
    if (i > COLS) return;
    band_fill(RAW_H, row_desk);
    for (i = 0; i < COLS; i++) {
        o[i] = t[i];
        if (!which && i >= BOX_LO && i < BOX_HI) raw_cell(i, (u8)t[i], 0);
        else if (t[i] != ' ') raw_cell(i, (u8)t[i], 0xFF);
    }
    o[COLS] = '|';
    o[COLS + 1] = 0;
    band_out(sy, 0, RAW_H);
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
    u8 ln, a = typing_attr;
    u16 li = nlines ? line_of(cur) : 0;
    struct line L;
    if (!inited) init_rows();
    /* the style field: the kind of the cursor's paragraph (CTRL+D: the drop-down) */
    sn = 0;
    put_str("  ");
    put_str(kind_names[cur_kind()]);
    while (sn < BOX_HI - 2) sb[sn++] = ' ';
    put_str("v  MEGA65 Writer  ");
    put_str(fname[0] ? fname : "untitled");
    if (dirty) put_str(" *");
    ln = sn;
    if (list_on && list_ok) put_str("LIST  ");
    put_str(view_mode == VIEW_RAW ? "RAW VIEW " : "PAGE LAYOUT ");
    bar(0, ln, 0);
    bar_colour(0);

    if (panel) {
        for (ln = 0; ln < PANEL_ROWS; ln++) {
            sn = 0;
            put_str(" ");
            put_str(msg_get((u8)(MSG_MENU + ln)));
            bar((u8)(2 + ln), sn, (u16)(VIEW_TOP + VIEW_H - (PANEL_ROWS - ln) * RAW_H));
            bar_colour((u8)((VIEW_TOP + VIEW_H - (PANEL_ROWS - ln) * RAW_H) >> 3));
        }
    } else {
        bar_text[2][0] = bar_text[3][0] = 0;
    }

    sn = 0;
    if (msg[0]) {
        put_str(" ");
        put_str(msg);
        ln = sn;
    } else {
        get_line(li, &L);
        if (view_mode == VIEW_PAGE) {
            put_str(" Pg ");
            put_num((u16)(L.page + 1));
            put_str("/");
            if (layout_done) put_num(npages);
            else put_str("?");
        }
        put_str(" Ln ");
        put_num((u16)(li + 1));
        put_str(" Col ");
        put_num((u16)(cur - L.start + 1));
        put_str("  ");
        put_str(font_names[a & A_FONT]);
        put_str((a & A_BOLD) ? " B" : " -");
        put_str((a & A_ITAL) ? "I" : "-");
        put_str((a & A_UNDER) ? "U " : "- ");
        put_str(align_names[para_al(cur)]);
        if (sel_on) {
            put_str("  SEL ");
            put_num((u16)(sel_hi() - sel_lo()));
        }
        ln = sn;
        put_num(doc_len - 1);
        put_str(" chars  F8 Help  ESC Menu ");
    }
    bar(1, ln, VIEW_TOP + VIEW_H);
    bar_colour(ROWS - 2);
}

/* ---- page view ---- */
static void mark_desk(u16 sy, u16 n) {
    while (n--) desk_row[sy++ >> 3] = 1;
}

/* sb[0..sn-1] drawn in Sans at band row top, ending at x (sh 0), or
 * centred on it (sh 1) */
static void sb_draw(u16 x, u8 sh, u8 top) {
    u8 n;
    u16 w = 0;
    for (n = 0; n < sn; n++) w += char_w((u8)sb[n], 0);
    x -= w >> sh;
    for (n = 0; n < sn; n++) {
        draw_glyph(&font_sans, (u8)sb[n], x, top, 0);
        x += char_w((u8)sb[n], 0);
    }
}

/* line l (*L) into the band (rows 0..h-1) */
static void draw_line(u16 l, const struct line *L) {
    u16 i = L->start, e = line_end(l), x, lo = 0, hi = 0, cx = 0xFFFF;
    u8 h = L->h, asc = L->asc;
    struct just j;
    just_init(&j, L);
    if (h > BAND_H) h = BAND_H;
    band_fill(h, row_page);
    if (sel_on) {
        lo = sel_lo();
        hi = sel_hi();
    }
    {
        u8 r, k = L->kind & A_KIND;
        if (k == K_QUOTE || k == K_CODE) {
            /* a quote's bar, a code block's dotted line, in its indent */
            k = k == K_CODE;
            for (r = 0; r < h; r = (u8)(r + 1 + k)) band_xor(TEXT_X + 6, TEXT_X + 9 - 2 * k, r, (u8)(r + 1));
        } else if (list_first(L)) {
            /* a list item's marker, before its text: a square (hollow at
             * levels 2 and 4), or its number */
            x = (u16)(TEXT_X + para_indent(L->kind) - 6);
            if (k == K_BULLET) {
                band_xor(x - 5, x, (u8)(asc - 7), (u8)(asc - 2));
                if (L->kind & (1 << A_LEVEL_SHIFT)) band_xor(x - 4, x - 1, (u8)(asc - 6), (u8)(asc - 3));
            } else {
                sn = 0;
                put_num(lnum);
                sb[sn++] = '.';
                sb_draw(x, 0, (u8)(asc - font_sans.ascent));
            }
        }
    }
    x = (u16)(TEXT_X + L->x);
    for (; i < e; i++) {
        u8 c = txt_at(i), a = atr_at(i), w;
        const struct font *f = fonts[a & A_FONT];
        if (i == cur) cx = x;
        if (c == CH_CR || c == CH_FF) {
            if (sel_on && i >= lo && i < hi) band_xor(x, x + 4, 0, h);
            continue;
        }
        w = just_w(c, a, &j);
        if (c != ' ') draw_glyph(f, c, x, (u8)(asc - f->ascent), a);
        if (a & A_UNDER) band_hline(x, x + w, (u8)(asc + 2));
        if (sel_on && i >= lo && i < hi) band_xor(x, x + w, 0, h);
        x += w;
    }
    if (list_on && list_ok) list_marks(l, L);
    if (cx != 0xFFFF) band_xor(cx ? cx - 1 : 0, cx + 1, 0, h);
}

/* the page number in the bottom margin */
#define FOOT_H 15
#define FOOT_Y (PAGE_H - 34)
static void draw_footer(u8 page) {
    band_fill(FOOT_H, row_page);
    sn = 0;
    put_str("- ");
    put_num((u16)(page + 1));
    put_str(" -");
    sb_draw(PAGE_X + PAGE_W / 2, 1, 0);
}

static void render_page(void) {
    u16 sy = VIEW_TOP, dy = view_y, l = 0, lo, hi, mid, vb = VIEW_TOP + VIEW_H - (panel ? PANEL_ROWS * RAW_H : 0);
    u8 r;
    struct line L;
    new_frame();
    for (r = 0; r < ROWS; r++) desk_row[r] = 0;
    /* the first line not above the view */
    if (nlines) {
        lo = 0;
        hi = nlines;
        while (lo < hi) {
            mid = (u16)((lo + hi) / 2);
            get_line(mid, &L);
            if (L.y + L.h <= dy) lo = (u16)(mid + 1);
            else hi = mid;
        }
        l = lo;
    }
    while (sy < vb) {
        u16 rem = vb - sy, n;
        u16 p = dy / PAGE_STRIDE, off = dy - p * PAGE_STRIDE, ptop = p * PAGE_STRIDE + PAGE_GAP;
        u8 more = l < nlines;
        if (more) get_line(l, &L);
        if (off < PAGE_GAP || p >= npages) {
            /* the desk: between pages, or below the last */
            n = off < PAGE_GAP ? PAGE_GAP - off : (p + 1) * PAGE_STRIDE - dy;
            if (n > rem) n = rem;
            if (!seg_same(sy, SEG_DESK, 0, n, 0)) rows_out(sy, n, row_desk);
            mark_desk(sy, n);
        } else if (more && L.y <= dy) {
            u8 r0 = (u8)(dy - L.y);
            n = L.h - r0;
            if (n > rem) n = rem;
            if (!seg_same(sy, SEG_LINE, r0, n, line_hash(l, &L))) {
                draw_line(l, &L);
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
            if (more && L.y < lim) lim = L.y;
            if (dy < ptop + FOOT_Y && ptop + FOOT_Y < lim) lim = ptop + FOOT_Y;
            n = lim - dy;
            if (n > rem) n = rem;
            if (!seg_same(sy, SEG_PAGE, 0, n, 0)) rows_out(sy, n, row_page);
        }
        sy += n;
        dy += n;
    }
    for (r = VIEW_TOP / 8; r < vb / 8; r++) {
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

/* ---- the style drop-down (BASIC's CTRL+D: wp_drop with the one lit,
 * as the cursor keys move it; then 0xFF, and the screen drawn again) ----
 * Each kind's name in its own font and style, in a box at the screen's
 * left under the top bar's style field. */
#define DROP_W 20               /* cells */
#define DROP_H 24               /* rows an item */
#define DROP_ASC 18             /* its baseline */
void wp_drop(u8 k) {
    u8 n, r, a, *d;
    u16 x, sy = BAR_H;
    const char *s;
    const struct font *f;
    if (k == 0xFF) {
        render_invalidate();
        render();
        return;
    }
    for (n = 0; n < NKINDS; n++) {
        a = kind_attr[n];
        f = fonts[a & A_FONT];
        band_fill(DROP_H, row_blank);
        x = 12;
        for (s = kind_names[n]; *s; s++) {
            draw_glyph(f, (u8)*s, x, (u8)(DROP_ASC - f->ascent), a);
            x += char_w((u8)*s, a);
        }
        if (n == k) band_xor(0, DROP_W * 8, 0, DROP_H);
        for (r = 0, d = band; r < DROP_H; r++, d += COLS) {
            d[0] |= 0x80;
            d[DROP_W - 1] |= 1;
            blit_row(sy++, d, DROP_W);
        }
    }
    blit_row(sy, row_desk, DROP_W);
    for (n = BAR_H / 8; n < sy / 8; n++) {
        color_fill(n, 0, DROP_W, C_BLACK);
        rowcol[n] = 0xFF;
    }
}

/* ---- raw view ---- */
static u8 tag_colour(const char *t) {
    /* t: the tag's text after '[' */
    if (*t == '/') t++;
    if ((t[0] == 'B' || t[0] == 'I' || t[0] == 'U') && t[1] == ']') return C_BLUE;
    if (t[0] == 'Q' || t[0] == 'L' || t[0] == 'N' || (t[0] == 'H' && t[1] < 'A') || (t[0] == 'C' && t[1] == 'o'))
        return C_GREEN;                 /* a kind */
    if (t[0] == 'C' || t[0] == 'R' || t[0] == 'J' || t[0] == 'P') return C_RED;
    return C_PURPLE;
}

static void render_raw(void) {
    u8 vr, col, r;
    u16 l = view_y / RAW_H, lo = 0, hi = 0;
    char tags[48];
    struct line L;
    new_frame();
    if (sel_on) {
        lo = sel_lo();
        hi = sel_hi();
    }
    for (vr = 0; vr < VIEW_H / RAW_H - (panel ? PANEL_ROWS : 0); vr++, l++) {
        u16 sy = (u16)(VIEW_TOP + vr * RAW_H);
        u8 crow = (u8)(sy >> 3);
        if (l < nlines) get_line(l, &L);
        if (seg_same(sy, SEG_RAW, 0, RAW_H, l < nlines ? line_hash(l, &L) : 0)) continue;
        for (col = 0; col < COLS; col++) cellcol[col] = C_BLACK;
        band_fill(RAW_H, row_blank);
        if (l < nlines) {
            u16 i = L.start, e = line_end(l);
            col = 0;
            for (; i < e && col < COLS; i++) {
                u8 c = txt_at(i), n = raw_tag(i, tags), k, tc = C_BLUE;
                for (k = 0; k < n && col < COLS; k++) {
                    if (tags[k] == '[') tc = tag_colour(tags + k + 1);
                    raw_cell(col, (u8)tags[k], 0);
                    cellcol[col] = tc;
                    if (c == CH_FF && i == cur && k == n - 1) raw_invert_cell(col);
                    col++;
                }
                if (c == CH_FF) continue;
                if (col >= COLS) break;
                if (c == CH_CR) {
                    raw_cell(col, G_PILCROW, 0);
                    cellcol[col] = C_GREY;
                } else {
                    raw_cell(col, c, 0);
                }
                if (sel_on && i >= lo && i < hi) raw_invert_cell(col);
                if (i == cur) raw_invert_cell(col);
                col++;
            }
        }
        if (list_on && list_ok) list_marks(l, &L);
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
    if (c == 0xBA) return '`';          /* SHIFT + @ */
    return 0;
}
