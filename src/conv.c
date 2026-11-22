/* conv.c — what the converters share (issue #3): an output to the scratch
 * area, a walk over the document for export, a builder of the document for
 * import, and Unicode as the document's characters
 *
 * They are in the program, once: the converter modules (MARKDOWN.OVL,
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
#ifndef __cc45__
/* (round the gap: doc.c; on the MEGA65 gap.s45, a third of the size) */
static u16 gmap(u16 i) { return i >= g_at ? i + g_len : i; }
u8 txt_at(u16 i) { return TXT[gmap(i)]; }
u8 atr_at(u16 i) { return ATR[gmap(i)]; }
void txt_put(u16 i, u8 c) { TXT[gmap(i)] = c; }
void atr_put(u16 i, u8 a) { ATR[gmap(i)] = a; }
#endif

/* ==== the style table (issue #5): a character's attribute byte is an
 * index into it; an entry is the style (A_FONT, A_BOLD, A_ITAL, A_UNDER,
 * A_STRIKE, A_SCRIPT: ST_SIZE bytes). Entries 0-31 are the first 32 styles,
 * each at its own bits (so a document of before the table, or a style
 * built from those bits, is the same index); others are added as styles are used (sty_find)
 * and kept for the session: the clipboard, undo and each document loaded
 * use the same table (doc_unpack: a file's entries merged into it). The
 * host's tests with STY_SCRAMBLE put 0-31 in another order (index 0 still
 * the plain style), so that a raw bit read anywhere shows. ==== */
u16 st_n;
#ifndef __cc45__
/* (on the MEGA65 style.s45, in assembly: some 450 bytes smaller) */
static u8 sc_i, sc_e, sf_e, sf_i;   /* sty's last lookup, sty_find's */

void sty_init(void) {
    u16 k;
    for (k = 0; k < 256; k++) mem_put(ST_LIN + k, 0);
    for (k = 0; k < 32; k++)
#ifdef STY_SCRAMBLE
        mem_put(ST_LIN + ((k * 5) & 31), (u8)k);    /* (style k at 5k) */
#else
        mem_put(ST_LIN + k, (u8)k);
#endif
    st_n = 32;
    sc_i = sc_e = sf_e = sf_i = 0;
}

/* entry s's style */
u8 sty(u8 s) {
    if (s != sc_i) {
        sc_i = s;
        sc_e = mem_get(ST_LIN + s);
    }
    return sc_e;
}

void sty_flush(void) { sc_i = sc_e = sf_e = sf_i = 0; }

/* style e's entry (not a named style's): found, or added (the table full:
 * 0, the plain style) */
u8 sty_find(u8 e) {
    u16 k;
    if (e == sf_e) return sf_i;
    for (k = 0; k < st_n; k++)
        if (mem_get(ST_LIN + k) == e && !sty_named((u8)k)) goto found;
    if (st_n == 256) return 0;
    mem_put(ST_LIN + k, e);
    st_n++;
    sc_i = (u8)k;               /* (sty's last: this index, as it is now) */
    sc_e = e;
found:
    sf_e = e;
    sf_i = (u8)k;
    return (u8)k;
}

static u16 mm, mc;               /* sty_load's table (its offset in the scratch area), count */
static u8 ms;

/* ST_SIZE, st_n and the entries at the scratch area's o; the end */
u16 sty_save(u16 o) {
    mem_put(SCRATCH_LIN + o, ST_SIZE);
    mem_put(SCRATCH_LIN + o + 1, (u8)st_n);
    mem_put(SCRATCH_LIN + o + 2, (u8)(st_n >> 8));
    mem_copy(SCRATCH_LIN + o + 3, ST_LIN, st_n);
    return (u16)(o + 3 + st_n);
}

/* the file's table at the scratch area's o (before version 3: entries
 * 0-31, those styles), each entry changed in place to its index here; 1:
 * each its own; 2: not a table */
u8 sty_load(u16 o, u8 v) {
    u16 k;
    mm = (u16)(o + 3);
    mc = 32;
    if (v < 3) {
        for (k = 0; k < 32; k++) mem_put(SCRATCH_LIN + mm + k, (u8)k);
    } else {
        mc = mem_get(SCRATCH_LIN + o + 1) | (u16)(mem_get(SCRATCH_LIN + o + 2) << 8);
        if (mem_get(SCRATCH_LIN + o) != ST_SIZE || mc > 256) return 2;
    }
    ms = 1;
    for (k = 0; k < mc; k++) {
        u8 a = sty_find(mem_get(SCRATCH_LIN + mm + k));
        mem_put(SCRATCH_LIN + mm + k, a);
        if (a != k) ms = 0;
    }
    return ms;
}

/* the document's characters 0..n-1 (no gap) given sty_load's index for
 * theirs (past its count: 0) unless each was its own; v1: its CR / FF's
 * attributes cleared but for the alignment */
void sty_remap(u16 n, u8 v1) {
    u16 i;
    u8 a, c;
    for (i = 0; i < n; i++) {
        a = atr_at(i);
        c = txt_at(i);
        if (c == CH_CR || c == CH_FF) {
            if (v1) atr_put(i, a & A_ALIGN);
        } else if (!ms) {
            atr_put(i, a < mc ? mem_get(SCRATCH_LIN + mm + a) : 0);
        }
    }
}
#endif

/* ---- named styles (wp.h) ---- */
u8 st_nidx[NNAMES];
static char nbuf[NAME_LEN];

const char *name_at(u8 k) {
    mem_read((u8 *)nbuf, NAMES_LIN + (u16)k * NAME_LEN, NAME_LEN);
    return nbuf;
}

const char *sty_name(u8 i) {
    u8 k = sty_named(i);
    return k ? name_at((u8)(k - 1)) : 0;
}

#ifndef __cc45__
/* (on the MEGA65 style.s45's) */
u8 sty_named(u8 i) {
    u8 k;
    if (i)
        for (k = 0; k < NNAMES; k++)
            if (st_nidx[k] == i) return (u8)(k + 1);
    return 0;
}

u8 names_count(void) {
    u8 k = 0;
    while (k < NNAMES && st_nidx[k]) k++;
    return k;
}
#endif

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
u8 w_attr, w_idx;
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
    a = atr_at(i);
    while (++i < w_pe && atr_at(i) == a) ;
    w_attr = sty(a);
    w_idx = a;
    w_to = w_at = i;
    return EV_RUN;
}

u8 rvs_at(u16 i) {
    u8 c;
    while (i) {
        c = txt_at(--i);
        if (c == CH_CR || c == CH_FF) break;
        if (c == RVS_ON) return 1;
        if (c == RVS_OFF) return 0;
    }
    return 0;
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
u8 b_attr, b_done, b_named;

void build_start(void) {
    g_len = 0;
    b_n = 0;
    b_attr = b_named = 0;
}

void build_put(u8 c) {
    if (b_n >= DOC_MAX - 1) return;     /* (room kept for the final CR) */
    txt_put(b_n, c);
    atr_put(b_n, c == CH_CR || c == CH_FF ? b_attr : b_named ? b_named : sty_find(b_attr));   /* (b_attr: a style; a paragraph's) */
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
