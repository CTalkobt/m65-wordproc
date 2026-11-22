/* pdf.c — PDF export: the modules PDF.OVL (PDF_PART 1: the header, the
 * pages) and PDF2.OVL (PDF_PART 2: the catalog, the fonts, the C64 glyphs
 * used, their Type 3 font, the cross-reference table), run one after the other into
 * the same output (apart, as together they are more than MOD_SIZE)
 *
 * The document as the page view lays it out (BASIC has it laid out to its
 * end first: GET_PDFPREP), one PDF page (US Letter, the shape of Writer's
 * page: a pixel is 1.125 points) for each of Writer's, to SCRATCH_LIN:
 *   - each page's content in Writer's pixels (1.125 0 0 1.125 0 0 cm, y
 *     up from the page's bottom), so its positions are whole numbers;
 *   - the text in PDF's standard fonts (Sans and Head: Helvetica, Serif:
 *     Times, Mono: Courier; bold, italic: their variants), each word where
 *     Writer puts it (its widths, the justification), so the lines and
 *     pages are Writer's; superscript and subscript smaller, raised or
 *     lowered; underline and strikethrough as rectangles;
 *   - the PETSCII font and PETSCII lines in a Type 3 font of the C64
 *     glyphs Writer draws (PETG_LIN: 8 x 16, those used), a PETSCII
 *     line's reverse video a black box under white glyphs;
 *   - a quote's bar, a code block's line, list bullets and numbers, the
 *     page number at the foot of each page.
 * Its objects: 1 the catalog, 2 the pages, 3 the resources, 4-15 the
 * standard fonts, 16 the Type 3 font, then each page's dictionary, content
 * and content's length, then a glyph for each C64 glyph used. Their
 * offsets (for the cross-reference table) and the glyphs used are kept in
 * the line table's bounce buffer (attic RAM, idle while a module runs)
 * from one part to the next.
 */
#include "wp.h"

#define out o_put
#define outs o_puts

#define OFS_LIN LBOUNCE_LIN             /* each object's offset (2 bytes) */
#define USED_LIN (LBOUNCE_LIN + 0x1000) /* the C64 glyphs used (1 a glyph) */
#define OBJ_T3 16
#define OBJ_PAGE0 17
#define NGLYPH 154              /* (PETG_LIN's) */

static void unum(u16 u) {
    char t[6];
    u8 k = 0;
    do {
        t[k++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    while (k) out((u8)t[--k]);
}

/* object n starts here */
static void obj(u16 n) {
    mem_put(OFS_LIN + n * 2, (u8)o_n);
    mem_put(OFS_LIN + n * 2 + 1, (u8)(o_n >> 8));
    unum(n);
    outs(" 0 obj\n");
}

static void endobj(void) { outs("\nendobj\n"); }

static void ref(u16 n) {
    unum(n);
    outs(" 0 R ");
}

#if !defined(PDF_PART) || PDF_PART == 1     /* (the host: both) */
static void num(int16_t v) {
    if (v < 0) {
        out('-');
        v = (int16_t)-v;
    }
    unum((u16)v);
    out(' ');
}

/* a rectangle x, y (its bottom, from the page's bottom), w, h, filled */
static void rect(int16_t x, int16_t y, int16_t w, int16_t h) {
    num(x);
    num(y);
    num(w);
    num(h);
    outs("re f\n");
}

static char dbuf[8];
/* v in decimal (dbuf), then s */
static char *dec(u16 v, const char *s) {
    char t[6];
    u8 k = 0, m = 0;
    do {
        t[k++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (k) dbuf[m++] = t[--k];
    while (*s) dbuf[m++] = *s++;
    dbuf[m] = 0;
    return dbuf;
}

/* t in Sans at x, y (its baseline); its width (Writer's) */
static u16 label(int16_t x, int16_t y, const char *t) {
    u16 w = 0;
    outs("BT /F0 12 Tf 100 Tz 0 Ts ");
    num(x);
    num(y);
    outs("Td(");
    outs(t);
    outs(")Tj ET\n");
    while (*t) w += char_w((u8)*t++, 0);
    return w;
}

/* ---- the text: words, each at its place ---- */
/* A font key: 0-11 the standard fonts, 12 the C64 glyphs, + 16
 * superscript, + 32 subscript, + 64 Head. Their size is set by height (12
 * pixels, Head 18; a script 70%); each word's width by horizontal scaling
 * (Tz, percent: word_tz), to be Writer's exactly (its fonts are wide for
 * their height), from the fonts' widths (codes 32-126, in 8/1000 of the
 * size; Helvetica, Helvetica-Bold, Times-Roman, Times-Bold: the obliques
 * and italics as these; Courier's all 600) */
static const u8 afm_w[4][95] = {
    { 35, 35, 44, 70, 70, 111, 83, 28, 42, 42, 49, 73, 35, 42, 35, 35, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 35, 35, 73, 73, 73, 70, 127, 83, 83, 90, 90, 83, 76, 97, 90, 35, 62, 83, 70, 104, 90, 97, 83, 97, 90, 83, 76, 90, 83, 118, 83, 83, 76, 35, 35, 35, 59, 70, 28, 70, 70, 62, 70, 70, 35, 70, 70, 28, 28, 62, 28, 104, 70, 70, 70, 70, 42, 62, 35, 70, 62, 90, 62, 62, 62, 42, 32, 42, 73 },
    { 35, 42, 59, 70, 70, 111, 90, 35, 42, 42, 49, 73, 35, 42, 35, 35, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 42, 42, 73, 73, 73, 76, 122, 90, 90, 90, 90, 83, 76, 97, 90, 35, 70, 90, 76, 104, 90, 97, 83, 97, 90, 83, 76, 90, 83, 118, 83, 83, 76, 42, 35, 42, 73, 70, 35, 70, 76, 70, 76, 70, 42, 76, 76, 35, 35, 70, 35, 111, 76, 76, 76, 76, 49, 70, 42, 76, 70, 97, 70, 70, 62, 49, 35, 49, 73 },
    { 31, 42, 51, 62, 62, 104, 97, 42, 42, 42, 62, 70, 31, 42, 31, 35, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 35, 35, 70, 70, 70, 56, 115, 90, 83, 83, 90, 76, 70, 90, 90, 42, 49, 90, 76, 111, 90, 90, 70, 90, 83, 70, 76, 90, 90, 118, 90, 90, 76, 42, 35, 42, 59, 62, 42, 56, 62, 56, 62, 56, 42, 62, 62, 35, 35, 62, 35, 97, 62, 62, 62, 62, 42, 49, 35, 62, 62, 90, 62, 62, 56, 60, 25, 60, 68 },
    { 31, 42, 69, 62, 62, 125, 104, 42, 42, 42, 62, 71, 31, 42, 31, 35, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 42, 42, 71, 71, 71, 62, 116, 90, 83, 90, 90, 83, 76, 97, 97, 49, 62, 97, 83, 118, 90, 97, 76, 97, 90, 70, 83, 90, 90, 125, 90, 90, 83, 42, 35, 42, 73, 62, 42, 62, 70, 56, 70, 56, 42, 62, 70, 35, 42, 70, 35, 104, 70, 62, 70, 70, 56, 49, 42, 70, 62, 90, 62, 62, 56, 49, 28, 49, 65 },
};
static u8 cur_tz;
static u8 key(u8 a);

/* the word from i (its characters to a space, a paragraph's end or
 * another font key) in font key k: its scaling, Writer's width over the
 * font's (at its size) */
static u8 word_tz(u16 i, u16 e, u8 k) {
    u32 ww = 0, tw = 0;
    u8 c, a, f = k & 15;
    u16 sz = k & 64 ? (k & 48 ? 126 : 180) : (k & 48 ? 84 : 120);
    for (; i < e; i++) {
        c = txt_at(i);
        a = sty(atr_at(i));
        if (c == ' ' || c == CH_CR || c == CH_FF || key(a) != k) break;
        ww += char_w(c, a);
        if (c < 32 || c > 126) c = 'n';
        tw += f >= 8 ? 75 : afm_w[(f >> 2) * 2 + (f & 1)][c - 32];
    }
    if (!tw) return 100;
    ww = ww * (f & 2 ? 117500UL : 125000UL) / (tw * sz);    /* (an italic 6% less: its slant leans past its width) */
    return (u8)(ww > 250 ? 250 : ww < 40 ? 40 : ww);
}
static u8 cur_f, open_w, white;         /* the font set (0xFF: none), a string open, the fill white */
static int16_t tx;                      /* the last word's x (Td's) */

/* style a's font key (wp: f_of's) */
static u8 key(u8 a) {
    u8 f = a & A_FONT;
    if (IS_C64(a)) return 12;
    return (u8)((f == F_SERIF ? 4 : f == F_MONO ? 8 : 0) + (a & A_BOLD ? 1 : 0) + (a & A_ITAL ? 2 : 0) +
                (SCRIPT(a) >> 2) + (f == F_HEAD ? 64 : 0));
}

static void word_end(void) {
    if (open_w) outs(")Tj\n");
    open_w = 0;
}

/* byte c in the font of key k, at x (a new word: placed there) */
static void put_ch(u8 c, int16_t x, u8 k, u8 newword) {
    if (k != cur_f || newword) {
        word_end();
        if (k != cur_f) {
            if (k == 12) {
                outs("/P 8 Tf 0 Ts\n");
            } else {
                outs("/F");
                unum(k & 15);
                outs(k & 64 ? (k & 48 ? " 12.6 Tf " : " 18 Tf ") : k & 48 ? " 8.4 Tf " : " 12 Tf ");
                outs(k & 16 ? (k & 64 ? "6 Ts\n" : "4 Ts\n") : k & 32 ? "-3 Ts\n" : "0 Ts\n");
            }
            cur_f = k;
        }
        num((int16_t)(x - tx));
        outs("0 Td(");
        tx = x;
        open_w = 1;
    }
    if (c == '(' || c == ')' || c == '\\') out('\\');
    if (c < 32 || c >= 127) {
        out('\\');
        out((u8)('0' + (c >> 6)));
        out((u8)('0' + ((c >> 3) & 7)));
        c = (u8)('0' + (c & 7));
    }
    out(c);
}

/* a C64 font's character c: its glyph's number (marked used) */
static u8 glyph(const struct font *f, u8 c) {
    u32 a;
    u8 g;
    if (c < FONT_FIRST) c = '?';
    a = f->offs + (u16)(c - FONT_FIRST) * 3;
    g = (u8)(mem_get(a + 1) >> 4 | mem_get(a + 2) << 4);
    mem_put(USED_LIN + g, 1);
    return g;
}

/* underline, strikethrough: the line's runs, drawn after its text */
#define NSEG 8
static int16_t sg_x[NSEG], sg_w[NSEG], sg_y[NSEG];
static u8 nsg;
static int16_t run_x[2];        /* (a run's start, 0x7FFF: none: underline, strike) */

static void run_to(u8 k, u8 on, int16_t x, int16_t y) {
    if (on && run_x[k] == 0x7FFF) run_x[k] = x;
    if (!on && run_x[k] != 0x7FFF) {
        if (nsg < NSEG) {
            sg_x[nsg] = run_x[k];
            sg_w[nsg] = (int16_t)(x - run_x[k]);
            sg_y[nsg++] = y;
        }
        run_x[k] = 0x7FFF;
    }
}

/* line l (*L) of a page whose top is ptop (document pixels) */
static void put_line(u16 l, struct line *L, u16 ptop) {
    u16 i = L->start, e = line_end(l);
    int16_t top = (int16_t)(L->y - ptop), y = (int16_t)(PAGE_H - top - L->asc), x = (int16_t)(MARGIN_X + L->x), x0;
    u8 c, a, w, pet = L->kind & L_PET, rvs, k = L->kind & A_KIND, gap = 1, f;
    struct just j;
    just_init(&j, L);
    /* the paragraph's marks: a quote's bar, a code block's line, a list
     * item's bullet or number */
    if (k == K_QUOTE) rect(MARGIN_X + 6, (int16_t)(PAGE_H - top - L->h), 3, L->h);
    if (k == K_CODE) {
        outs("0.6 g ");
        rect(MARGIN_X + 6, (int16_t)(PAGE_H - top - L->h), 1, L->h);
        outs("0 g\n");
    }
    if ((L->kind & L_FIRST) && k >= K_BULLET) {
        x0 = (int16_t)(MARGIN_X + para_indent(L->kind) - 6);
        if (k == K_BULLET) rect((int16_t)(x0 - 5), (int16_t)(y + 2), 5, 5);
        else {
            char *t = dec(item_num(i), ".");
            u16 n = 0;
            for (a = 0; t[a]; a++) n += char_w((u8)t[a], 0);
            label((int16_t)(x0 - n), y, t);
        }
    }
    /* a PETSCII line's reverse video: black boxes (its characters 8 wide) */
    rvs = pet ? rvs_at(i) : 0;
    for (x0 = x; pet && i < e; i++) {
        c = txt_at(i);
        if (c == CH_CR || c == CH_FF) continue;
        if ((c & 0x7F) == RVS_ON) {
            rvs = c == RVS_ON;
            continue;
        }
        if (rvs) rect(x0, (int16_t)(y - 2), 8, 16);
        x0 += 8;
    }
    i = L->start;
    rvs = pet ? rvs_at(i) : 0;
    /* the text */
    outs("BT ");
    num(0);
    num(y);
    outs("Td\n");
    cur_f = 0xFF;
    tx = 0;
    open_w = white = nsg = cur_tz = 0;
    run_x[0] = run_x[1] = 0x7FFF;
    for (; i < e; i++) {
        c = txt_at(i);
        a = sty(atr_at(i));
        if (pet) a = PET_STYLE(a);
        if (c == CH_CR || c == CH_FF) continue;
        if (pet && (c & 0x7F) == RVS_ON) {
            rvs = c == RVS_ON;
            continue;
        }
        w = just_w(c, a, &j);
        run_to(0, a & A_UNDER, x, (int16_t)(y - 3));
        run_to(1, a & A_STRIKE, x, (int16_t)(y + fx_third[a & A_FONT]));
        if (c == ' ' && !IS_C64(a)) {
            gap = 1;            /* (a space: the next word placed anew) */
            x += w;
            continue;
        }
        if (rvs != white) {
            word_end();
            outs(rvs ? "1 g " : "0 g ");
            white = rvs;
            gap = 1;
        }
        f = key(a);
        if (gap || f != cur_f) {
            /* (a word starts: its scaling) */
            u8 t = f == 12 ? 100 : word_tz(i, e, f);
            if (t != cur_tz) {
                word_end();
                unum(t);
                outs(" Tz ");
                cur_tz = t;
            }
        }
        if (f == 12) c = glyph(pet ? &font_petl : &font_petf, c);
        put_ch(c, x, f, gap);
        gap = 0;
        x += w;
    }
    word_end();
    outs(white ? "0 g ET\n" : "ET\n");
    run_to(0, 0, x, (int16_t)(y - 3));
    run_to(1, 0, x, (int16_t)(y + 4));
    for (a = 0; a < nsg; a++) rect(sg_x[a], sg_y[a], sg_w[a], 1);
}

static u16 pdf_export(void) {
    u16 l = 0, p, n, s, k;
    struct line L;
    o_start();
    for (k = 0; k < NGLYPH; k++) mem_put(USED_LIN + k, 0);
    outs("%PDF-1.4\n%\xe2\xe3\xcf\xd3\n");
    /* (objects 1-15: PDF2.OVL's) each page: its dictionary, content, content's length */
    for (p = 0; p < npages; p++) {
        n = (u16)(OBJ_PAGE0 + 3 * p);
        obj(n);
        outs("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources 3 0 R /Contents ");
        ref((u16)(n + 1));
        outs(">>");
        endobj();
        obj((u16)(n + 1));
        outs("<< /Length ");
        ref((u16)(n + 2));
        outs(">>\nstream\n");
        s = o_n;
        outs("q 1.125 0 0 1.125 0 0 cm\n");
        for (; l < nlines; l++) {
            get_line(l, &L);
            if (L.page != p) break;
            put_line(l, &L, (u16)(PAGE_GAP + p * PAGE_STRIDE));
        }
        /* the page's number, at its foot (as the page view draws it) */
        {
            char t[12] = "- ";
            u8 m = 2;
            char *d = dec((u16)(p + 1), " -");
            u16 w = 0;
            while (*d) t[m++] = *d++;
            t[m] = 0;
            for (m = 0; t[m]; m++) w += char_w((u8)t[m], 0);
            label((int16_t)(PAGE_W / 2 - w / 2), 22, t);
        }
        outs("Q");
        s = (u16)(o_n - s);
        outs("\nendstream");
        endobj();
        obj((u16)(n + 2));
        unum(s);
        endobj();
    }
    /* (PDF2.OVL goes on: o_n kept) */
    return o_n > SCRATCH_MAX ? 0 : o_n;
}

/* PDF.OVL's entry (mod_run) */
void pdf_main(void) {
    mod_ret = pdf_export();
}
#endif

#if !defined(PDF_PART) || PDF_PART == 2
static const char gl_head[] = "8 0 0 -2 8 14 d1\n8 0 0 16 0 -2 cm\nBI /W 8 /H 16 /IM true /BPC 1 /D [1 0] ID ";

static const char *const base14[12] = {
    "Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique",
    "Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic",
    "Courier", "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique",
};

static u16 pdf_end(void) {
    u16 n = (u16)(OBJ_PAGE0 + 3 * npages), g, k, s, u = 0;
    u8 b[16];
    /* the catalog, the pages, the resources, the standard fonts */
    obj(1);
    outs("<< /Type /Catalog /Pages 2 0 R >>");
    endobj();
    obj(2);
    outs("<< /Type /Pages /Count ");
    unum(npages);
    outs(" /Kids [");
    for (k = 0; k < npages; k++) ref((u16)(OBJ_PAGE0 + 3 * k));
    outs("] >>");
    endobj();
    obj(3);
    outs("<< /Font << /P 16 0 R");
    for (k = 0; k < 12; k++) {
        outs(" /F");
        unum(k);
        out(' ');
        ref((u16)(4 + k));
    }
    outs(">> >>");
    endobj();
    for (k = 0; k < 12; k++) {
        obj((u16)(4 + k));
        outs("<< /Type /Font /Subtype /Type1 /BaseFont /");
        outs(base14[k]);
        outs(" /Encoding /WinAnsiEncoding >>");
        endobj();
    }
    /* the C64 glyphs used: each a stream (an image mask) */
    for (g = 0; g < NGLYPH; g++) {
        if (!mem_get(USED_LIN + g)) continue;
        mem_put(USED_LIN + g, (u8)(1 + u));     /* (its place among them, + 1) */
        obj((u16)(n + u++));
        outs("<< /Length ");
        unum(sizeof gl_head - 1 + 16 + 3);
        outs(">>\nstream\n");
        outs(gl_head);
        mem_read(b, PETG_LIN + g * 16, 16);
        for (k = 0; k < 16; k++) out(b[k]);
        outs("\nEI\nendstream");
        endobj();
    }
    /* the Type 3 font of them (each code its glyph's number) */
    obj(OBJ_T3);
    outs("<< /Type /Font /Subtype /Type3 /FontBBox [0 -2 8 14] /FontMatrix [0.125 0 0 0.125 0 0]"
         " /Resources << >> /FirstChar 0 /LastChar 153\n/Widths [");
    for (g = 0; g < NGLYPH; g++) outs("8 ");
    outs("]\n/Encoding << /Type /Encoding /Differences [");
    for (g = 0; g < NGLYPH; g++)
        if (mem_get(USED_LIN + g)) {
            unum(g);
            outs(" /g");
            unum(g);
            out(' ');
        }
    outs("] >>\n/CharProcs <<");
    for (g = 0; g < NGLYPH; g++)
        if ((k = mem_get(USED_LIN + g)) != 0) {
            outs(" /g");
            unum(g);
            out(' ');
            ref((u16)(n + k - 1));
        }
    outs(">> >>");
    endobj();
    /* the cross-reference table */
    n = (u16)(n + u);
    s = o_n;
    outs("xref\n0 ");
    unum(n);
    outs("\n0000000000 65535 f \n");
    for (k = 1; k < n; k++) {
        u16 o = (u16)(mem_get(OFS_LIN + k * 2) | mem_get(OFS_LIN + k * 2 + 1) << 8);
        char t[10];
        u8 m;
        for (m = 10; m--;) {
            t[m] = (char)('0' + o % 10);
            o /= 10;
        }
        for (m = 0; m < 10; m++) out((u8)t[m]);
        outs(" 00000 n \n");
    }
    outs("trailer\n<< /Size ");
    unum(n);
    outs(" /Root 1 0 R >>\nstartxref\n");
    unum(s);
    outs("\n%%EOF\n");
    return o_end();
}

/* PDF2.OVL's entry (mod_run) */
void pdf2_main(void) {
    mod_ret = pdf_end();
}
#endif
