/* doc.c — the document: editing, styles, the clipboard, the file image */
#include "wp.h"

u16 doc_len;
u16 cur;
u16 sel_anchor;
u8 sel_on;
u8 typing_attr;
u8 dirty;
u8 view_mode;
u16 goal_x;
u8 goal_valid;

u16 clip_len;

void doc_new(void) {
    TXT[0] = CH_CR;
    ATR[0] = 0;
    doc_len = 1;
    cur = 0;
    sel_on = 0;
    typing_attr = 0;
    dirty = 0;
    view_y = 0;
    goal_valid = 0;
    layout_all();
}

u16 sel_lo(void) { return sel_anchor < cur ? sel_anchor : cur; }
u16 sel_hi(void) { return sel_anchor < cur ? cur : sel_anchor; }

/* the CR / FF that ends the paragraph holding i */
u16 para_end(u16 i) {
    while (i < doc_len - 1 && TXT[i] != CH_CR && TXT[i] != CH_FF) i++;
    return i;
}

static u8 is_break(u8 c) { return c == CH_CR || c == CH_FF; }

/* the start of the paragraph holding i */
static u16 para_start(u16 i) {
    while (i > 0 && !is_break(TXT[i - 1])) i--;
    return i;
}

/* the style for typing at the cursor: the character before it, or (at a
 * paragraph's start) the one after it */
u8 attr_at_cursor(void) {
    if (cur > 0 && !is_break(TXT[cur - 1])) return ATR[cur - 1] & A_STYLE;
    return ATR[cur] & A_STYLE;
}

/* room for n characters at `at`: the text and attributes after it moved
 * up, through the scratch area (the DMA copies upwards only when the
 * ranges do not overlap) */
static void open_gap(u16 at, u16 n) {
    u16 tail = doc_len - at;
    u32 o = 0;
    u8 k;
    for (k = 0; k < 2; k++, o = ATTR_LIN - TEXT_LIN) {
        mem_copy(SCRATCH_LIN, TEXT_LIN + o + at, tail);
        mem_copy(TEXT_LIN + o + at + n, SCRATCH_LIN, tail);
    }
}

/* n characters (s, attributes a) inserted at `at`; 1 when there is no room */
u8 doc_insert(u16 at, const u8 *s, const u8 *a, u16 n) {
    u16 i, lo = at;
    if (n == 0) return 0;
    if (doc_len + n > DOC_MAX) return 1;
    open_gap(at, n);
    for (i = 0; i < n; i++) {
        TXT[at + i] = s[i];
        ATR[at + i] = a[i];
        if (is_break(s[i])) lo = 0xFFFF;
    }
    /* a paragraph end inserted: the lines before it, in its paragraph,
     * take its alignment */
    if (lo == 0xFFFF) lo = para_start(at);
    doc_len += n;
    dirty = 1;
    layout_edit(lo, at, (int16_t)n);
    return 0;
}

/* characters from..to-1 removed (never the final CR) */
void doc_delete(u16 from, u16 to) {
    u16 n, i, lo = from;
    if (to > doc_len - 1) to = doc_len - 1;
    if (from >= to) return;
    /* a paragraph end deleted: the paragraph before it takes the next one's
     * alignment */
    for (i = from; i < to; i++)
        if (is_break(TXT[i])) {
            lo = para_start(from);
            break;
        }
    n = doc_len - to;
    mem_copy(TEXT_LIN + from, TEXT_LIN + to, n);
    mem_copy(ATTR_LIN + from, ATTR_LIN + to, n);
    doc_len -= to - from;
    dirty = 1;
    layout_edit(lo, to, (int16_t)-(int16_t)(to - from));
}

static void delete_selection(void) {
    u16 lo = sel_lo(), hi = sel_hi();
    doc_delete(lo, hi);
    cur = lo;
    sel_on = 0;
}

/* an ASCII character (or CR / FF) typed at the cursor */
u8 doc_type(u8 c) {
    u8 a;
    if (sel_on) {
        typing_attr = ATR[sel_lo()] & A_STYLE;
        delete_selection();
    }
    a = typing_attr;
    if (is_break(c) && nlines >= MAX_LINES - 8) return 1;   /* the line table is (almost) full */
    if (is_break(c))    /* the new paragraph end keeps the paragraph's alignment */
        a |= ATR[para_end(cur)] & A_ALIGN;
    if (doc_insert(cur, &c, &a, 1)) return 1;
    cur++;
    goal_valid = 0;
    return 0;
}

void doc_backspace(void) {
    if (sel_on) {
        delete_selection();
    } else if (cur > 0) {
        /* joining two paragraphs: the joined one keeps the second's end */
        doc_delete(cur - 1, cur);
        cur--;
    }
    typing_attr = attr_at_cursor();
    goal_valid = 0;
}

void doc_delete_fwd(void) {
    if (sel_on) delete_selection();
    else doc_delete(cur, cur + 1);
    typing_attr = attr_at_cursor();
    goal_valid = 0;
}

/* the selection's attributes: (a & keep) | set; then the typing style
 * is its first character's */
static void restyle(u8 keep, u8 set) {
    u16 lo = sel_lo(), hi = sel_hi(), i;
    for (i = lo; i < hi; i++) ATR[i] = (u8)((ATR[i] & keep) | set);
    typing_attr = ATR[lo] & A_STYLE;
    dirty = 1;
    layout_edit(lo, hi, 0);
}

/* bold / italic / underline: over the selection (all on unless the first
 * character has it already), else for what is typed next */
void doc_toggle(u8 bit) {
    if (!sel_on) typing_attr ^= bit;
    else if (ATR[sel_lo()] & bit) restyle((u8)~bit, 0);
    else restyle(0xFF, bit);
}

/* the next font: over the selection (after its first character's), else
 * for what is typed next */
void doc_cycle_font(void) {
    u8 a = sel_on ? ATR[sel_lo()] : typing_attr, f = (u8)(((a & A_FONT) + 1) % NFONTS);
    if (sel_on) restyle((u8)~A_FONT, f);
    else typing_attr = (u8)((typing_attr & ~A_FONT) | f);
}

/* the alignment of the paragraph(s) at the cursor / selection: left,
 * centre, right, justify, left ... */
void doc_cycle_align(void) {
    u16 lo = sel_on ? sel_lo() : cur, hi = sel_on ? sel_hi() : cur;
    u16 e = para_end(lo), ps = para_start(lo);
    u8 al = (u8)((((ATR[e] & A_ALIGN) >> A_ALIGN_SHIFT) + 1) & 3);
    for (;;) {
        ATR[e] = (u8)((ATR[e] & ~A_ALIGN) | (al << A_ALIGN_SHIFT));
        if (e >= hi || e >= doc_len - 1) break;
        e = para_end(e + 1);
    }
    dirty = 1;
    layout_edit(ps, e, 0);
}

/* ---- the clipboard (bank 4: text, then attributes) ---- */
void doc_copy(void) {
    u16 lo, n;
    if (!sel_on) return;
    lo = sel_lo();
    n = sel_hi() - lo;
    if (n > CLIP_MAX) n = CLIP_MAX;
    mem_copy(CLIP_LIN, TEXT_LIN + lo, n);
    mem_copy(CLIP_LIN + CLIP_MAX, ATTR_LIN + lo, n);
    clip_len = n;
}

void doc_cut(void) {
    if (!sel_on) return;
    doc_copy();
    delete_selection();
    goal_valid = 0;
}

void doc_paste(void) {
    u16 n = clip_len;
    if (!n) return;
    if (sel_on) delete_selection();
    if (doc_len + n > DOC_MAX) return;
    open_gap(cur, n);
    mem_copy(TEXT_LIN + cur, CLIP_LIN, n);
    mem_copy(ATTR_LIN + cur, CLIP_LIN + CLIP_MAX, n);
    layout_edit(para_start(cur), cur, (int16_t)n);
    doc_len += n;
    cur += n;
    dirty = 1;
    goal_valid = 0;
}

/* ---- files ----
 * The image BASIC saves with BSAVE and loads with BLOAD, at SCRATCH_LIN:
 * "WP", version 1, the length (2 bytes), the text, the attributes. */
u16 doc_pack(void) {
    mem_put(SCRATCH_LIN, 'W');
    mem_put(SCRATCH_LIN + 1, 'P');
    mem_put(SCRATCH_LIN + 2, 1);
    mem_put(SCRATCH_LIN + 3, (u8)doc_len);
    mem_put(SCRATCH_LIN + 4, (u8)(doc_len >> 8));
    mem_copy(SCRATCH_LIN + 5, TEXT_LIN, doc_len);
    mem_copy(SCRATCH_LIN + 5 + doc_len, ATTR_LIN, doc_len);
    return 5 + 2 * doc_len;
}

u8 doc_unpack(void) {
    u16 n;
    if (mem_get(SCRATCH_LIN) != 'W' || mem_get(SCRATCH_LIN + 1) != 'P' || mem_get(SCRATCH_LIN + 2) != 1) return 1;
    n = mem_get(SCRATCH_LIN + 3) | (u16)(mem_get(SCRATCH_LIN + 4) << 8);
    if (n == 0 || n > DOC_MAX) return 1;
    mem_copy(TEXT_LIN, SCRATCH_LIN + 5, n);
    mem_copy(ATTR_LIN, SCRATCH_LIN + 5 + n, n);
    doc_len = n;
    doc_loaded(0);
    return 0;
}

/* a document just loaded or imported (doc_len characters): its final CR
 * made sure of, the cursor at its start, laid out anew; d: changed */
void doc_loaded(u8 d) {
    if (!doc_len || TXT[doc_len - 1] != CH_CR) {
        if (doc_len < DOC_MAX) {
            ATR[doc_len] = 0;
            doc_len++;
        }
        TXT[doc_len - 1] = CH_CR;
    }
    cur = 0;
    sel_on = 0;
    view_y = 0;
    goal_valid = 0;
    dirty = d;
    typing_attr = attr_at_cursor();
    layout_all();
}

/* the text as PETSCII (lower-case set) for a SEQ file; the size */
u16 doc_export(void) {
    u16 i;
    for (i = 0; i < doc_len; i++) {
        u8 c = TXT[i];
        if (c >= 'a' && c <= 'z') c = (u8)(c - 32);
        else if (c >= 'A' && c <= 'Z') c = (u8)(c + 128);
        else if (c == CH_FF) c = CH_CR;
        else if (c == '_') c = 0xA4;
        mem_put(SCRATCH_LIN + i, c);
    }
    return doc_len;
}

/* a PETSCII text file read (FREAD#) to SCRATCH_LIN, after GET_IMPORT_PREP
 * zeroed it, as the document; its length */
u16 doc_import(void) {
    u16 i;
    u8 c;
    doc_len = 0;
    for (i = 0; i < DOC_MAX - 1; i++) {
        c = mem_get(SCRATCH_LIN + i);
        if (!c) break;
        if (c == CH_CR) {
            TXT[doc_len] = CH_CR;
        } else {
            if (c == 0xA0) c = ' ';
            c = petscii_to_ascii(c);
            if (!c) continue;
            TXT[doc_len] = c;
        }
        ATR[doc_len] = 0;
        doc_len++;
    }
    /* past the file's end the drive gives CRs: one CR at the end */
    while (doc_len > 1 && TXT[doc_len - 1] == CH_CR && TXT[doc_len - 2] == CH_CR) doc_len--;
    doc_loaded(1);
    return (u16)(doc_len - 1);
}
