/* conv.c — what the converters share (issue #3): an output to the scratch
 * area, a walk over the document for export, a builder of the document for
 * import, and Unicode as the document's characters
 *
 * They are in the program, once: the converter modules (MDIMPORT.OVL,
 * RTF.OVL, ...) call them through mkmod.py's equates, as any program
 * function, and hold only their format's syntax. The text import and
 * export (doc.c) use them too.
 *
 * The walk is pulled, an event at a time (walk_next), rather than calling
 * the module back: a call through a pointer would reach every function,
 * and the linker would then keep every function's base-page variables
 * apart from the module's.
 */
#include "wp.h"

/* the document's character i, its attribute (with cc45 a far TXT[i] is
 * some 30 bytes of code, a call to these 10) */
u8 txt_at(u16 i) { return TXT[i]; }
u8 atr_at(u16 i) { return ATR[i]; }
void txt_put(u16 i, u8 c) { TXT[i] = c; }
void atr_put(u16 i, u8 a) { ATR[i] = a; }

/* ==== output: bytes to SCRATCH_LIN ==== */
u16 o_n;

void o_start(void) { o_n = 0; }

void o_put(u8 c) {
    if (o_n < SCRATCH_MAX) mem_put(SCRATCH_LIN + o_n, c);
    o_n++;
}

void o_puts(const char *s) {
    while (*s) o_put((u8)*s++);
}

u16 o_end(void) { return o_n <= SCRATCH_MAX ? o_n : 0; }

/* ==== export: the document's paragraphs and runs, in order ==== */
u16 w_pos, w_to;
u8 w_attr;
static u16 w_at, w_pe;              /* where the next event starts; the paragraph's CR / FF */
static u8 w_in;                     /* in a paragraph (its runs, then its end) */

void walk_start(void) {
    w_at = 0;
    w_in = 0;
}

u8 walk_next(void) {
    u16 i = w_at;
    u8 a;
    w_pos = i;
    if (!w_in) {
        if (i >= doc_len) return EV_END;
        w_pe = w_to = para_end(i);
        w_attr = atr_at(w_pe);
        w_in = 1;
        return EV_PARA;
    }
    if (i == w_pe) {
        w_attr = txt_at(i);
        w_in = 0;
        w_at = (u16)(i + 1);
        return EV_PEND;
    }
    a = atr_at(i) & A_STYLE;
    while (++i < w_pe && (atr_at(i) & A_STYLE) == a) ;
    w_attr = a;
    w_to = w_at = i;
    return EV_RUN;
}

/* paragraph p's number in its list: 1 + the numbered items before it at
 * its level (deeper ones passed over), back to anything else (here: the
 * Markdown and RTF modules call it, and the drawing) */
u16 item_num(u16 p) {
    u8 lv = atr_at(para_end(p)) & A_LEVEL, b;
    u16 n = 1;
    while (p) {
        b = atr_at(--p);                /* (the paragraph before: its end) */
        if ((b & A_KIND) < K_BULLET || (b & A_LEVEL) < lv) break;
        if ((b & A_LEVEL) == lv) {
            if ((b & A_KIND) != K_NUMBER) break;
            n++;
        }
        while (p && txt_at(p - 1) != CH_CR && txt_at(p - 1) != CH_FF) p--;
    }
    return n;
}

/* ==== import: characters appended to the document ==== */
u16 b_n;
u8 b_attr, b_done;

void build_start(void) {
    b_n = 0;
    b_attr = 0;
}

void build_put(u8 c) {
    if (b_n >= DOC_MAX - 1) return;     /* (room kept for the final CR) */
    txt_put(b_n, c);
    atr_put(b_n, b_attr);
    b_n++;
}

/* (doc_loaded is the program's, after the module has returned: doc_built;
 * called from here it would take in all the layout, whose base-page
 * variables the module's must then miss) */
u16 build_end(void) {
    if (!b_n || txt_at(b_n - 1) != CH_CR) {
        txt_put(b_n, CH_CR);
        atr_put(b_n, 0);
        b_n++;
    }
    doc_len = b_n;
    b_done = 1;
    return (u16)(b_n - 1);
}

/* Unicode as the document's characters (Latin-1: U+0020-U+007E and
 * U+00A0-U+00FF as they are; these others as ASCII; the rest "?"), in
 * b_attr */
static const u16 uni[] = { 0x2018, 0x2019, 0x201A, 0x201C, 0x201D, 0x201E, 0x2013, 0x2010, 0x2212,
                           0x2014, 0x2026, 0x2022, 0x2192, 0x2190, 0x20AC };
static const char *const uni_as[] = { "'", "'", "'", "\"", "\"", "\"", "-", "-", "-",
                                      "--", "...", "*", "->", "<-", "EUR" };

void build_uni(u16 u) {
    u8 k;
    const char *s;
    if (u < 128 || (u >= FONT_HI && u <= 0xFF)) {
        if (u >= 32 && u != 127) build_put((u8)u);
        return;
    }
    for (k = 0; k < sizeof uni / sizeof uni[0]; k++)
        if (uni[k] == u) {
            for (s = uni_as[k]; *s; s++) build_put((u8)*s);
            return;
        }
    build_put('?');
}

/* Latin-1's letters U+00C0-U+00FF without their accents */
static const char latin[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";

u8 as_ascii(u8 c) {
    if (c < 0x80) return c;
    if (c >= 0xC0) return (u8)latin[c - 0xC0];
    return c == FONT_HI ? ' ' : '?';
}
