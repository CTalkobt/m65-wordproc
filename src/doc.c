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
    txt_put(0, CH_CR);
    atr_put(0, 0);
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
    while (i < doc_len - 1 && txt_at(i) != CH_CR && txt_at(i) != CH_FF) i++;
    return i;
}

/* the alignment of the paragraph holding i (0 left .. 3 justify) */
u8 para_al(u16 i) { return (u8)((atr_at(para_end(i)) & A_ALIGN) >> A_ALIGN_SHIFT); }

/* the start of the paragraph holding i */
static u16 para_start(u16 i);

/* its kind (K_...) */
u8 para_kind(u16 i) { return atr_at(para_end(i)) & A_KIND; }

u8 cur_kind(void) { return para_kind(sel_on ? sel_lo() : cur); }

static u8 is_break(u8 c) { return c == CH_CR || c == CH_FF; }

/* the start of the paragraph holding i */
static u16 para_start(u16 i) {
    while (i > 0 && !is_break(txt_at(i - 1))) i--;
    return i;
}

/* the style for typing at the cursor: the character before it, or (at a
 * paragraph's start) the one after it */
u8 attr_at_cursor(void) {
    if (cur > 0 && !is_break(txt_at(cur - 1))) return atr_at(cur - 1) & A_STYLE;
    if (is_break(txt_at(cur))) return kind_attr[atr_at(cur) & A_KIND];     /* (an empty paragraph: its kind's) */
    return atr_at(cur) & A_STYLE;
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
    mem_write(TEXT_LIN + at, s, n);
    mem_write(ATTR_LIN + at, a, n);
    for (i = 0; i < n; i++)
        if (is_break(s[i])) lo = 0xFFFF;
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
     * kind and alignment, unless nothing of that one is left (the end after
     * the deletion: an empty paragraph joined), when it keeps its own */
    for (i = from; i < to; i++)
        if (is_break(txt_at(i))) {
            lo = para_start(from);
            if (is_break(txt_at(to))) atr_put(to, atr_at(i));
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
    u8 a, k, brk = is_break(c), at_end;
    if (brk && is_break(txt_at(cur)) && doc_list_level(0)) return 0;  /* RETURN in an empty list item: out a level */
    if (sel_on) {
        typing_attr = atr_at(sel_lo()) & A_STYLE;
        delete_selection();
    }
    a = typing_attr;
    at_end = is_break(txt_at(cur));
    if (brk) {
        if (nlines >= MAX_LINES - 8) return 1;  /* the line table is (almost) full */
        /* the new paragraph end keeps the paragraph's kind and alignment */
        a = atr_at(para_end(cur)) & A_PARA;
    }
    if (doc_insert(cur, &c, &a, 1)) return 1;
    cur++;
    k = (u8)((a & A_KIND) - K_H1);
    if (brk && at_end && k < 3) {
        /* after a heading's end: a normal paragraph */
        atr_put(cur, (u8)(atr_at(cur) & ~A_KIND));
        typing_attr = kind_attr[K_NORMAL];
    }
    goal_valid = 0;
    return 0;
}

void doc_backspace(void) {
    if (doc_list_level(0)) return;      /* at a list item's start: out a level */
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
    for (i = lo; i < hi; i++)
        if (!is_break(txt_at(i))) atr_put(i, (u8)((atr_at(i) & keep) | set));
    typing_attr = atr_at(lo) & A_STYLE;
    dirty = 1;
    layout_edit(lo, hi, 0);
}

/* bold / italic / underline: over the selection (all on unless the first
 * character has it already), else for what is typed next */
void doc_toggle(u8 bit) {
    if (!sel_on) typing_attr ^= bit;
    else if (atr_at(sel_lo()) & bit) restyle((u8)~bit, 0);
    else restyle(0xFF, bit);
}

/* the next font: over the selection (after its first character's), else
 * for what is typed next */
void doc_cycle_font(void) {
    u8 a = sel_on ? atr_at(sel_lo()) : typing_attr, f = (u8)(((a & A_FONT) + 1) % NFONTS);
    if (sel_on) restyle((u8)~A_FONT, f);
    else typing_attr = (u8)((typing_attr & ~A_FONT) | f);
}

/* the alignment of the paragraph(s) at the cursor / selection: left,
 * centre, right, justify, left ... */
void doc_cycle_align(void) {
    u16 lo = sel_on ? sel_lo() : cur, hi = sel_on ? sel_hi() : cur;
    u16 e = para_end(lo), ps = para_start(lo);
    u8 al = (u8)((((atr_at(e) & A_ALIGN) >> A_ALIGN_SHIFT) + 1) & 3);
    for (;;) {
        atr_put(e, (u8)((atr_at(e) & ~A_ALIGN) | (al << A_ALIGN_SHIFT)));
        if (e >= hi || e >= doc_len - 1) break;
        e = para_end(e + 1);
    }
    dirty = 1;
    layout_edit(ps, e, 0);
}

/* TAB, DEL, RETURN at a list item's start (the cursor, no selection): a
 * level in, or out (at level 0: a normal paragraph); 0: not there */
u8 doc_list_level(u8 in) {
    u16 e;
    u8 a;
    if (sel_on || cur != para_start(cur)) return 0;
    e = para_end(cur);
    a = atr_at(e);
    if ((a & A_KIND) < K_BULLET) return 0;
    if (in) {
        if ((a & A_LEVEL) != A_LEVEL) a += 1 << A_LEVEL_SHIFT;
    } else if (a & A_LEVEL) {
        a -= 1 << A_LEVEL_SHIFT;
    } else {
        a &= A_ALIGN;
    }
    atr_put(e, a);
    dirty = 1;
    layout_edit(cur, e, 0);
    return 1;
}

/* Style k (a paragraph kind) applied: a paragraph the selection holds
 * whole (all its text; with no selection, the cursor's) becomes of kind k,
 * its characters in k's font and style where they were in its old kind's
 * (others keep their font; bold, italic, underline added to the old kind's
 * stay); the selection's part of any other paragraph takes k's font and
 * style as they are (Normal: plain). */
void doc_style(u8 k) {
    u16 lo = cur, hi = cur, p, e, i, ps;
    u8 a, f, of, keep, ka = kind_attr[k], kf = ka & A_FONT, kfl = ka & ~A_FONT;
    if (sel_on) {
        lo = sel_lo();
        hi = sel_hi();
    }
    p = ps = para_start(lo);
    for (;;) {
        e = para_end(p);
        if (!sel_on || (lo <= p && hi >= e)) {
            a = kind_attr[atr_at(e) & A_KIND];
            of = a & A_FONT;
            keep = (u8)(~a & (A_BOLD | A_ITAL | A_UNDER));
            for (i = p; i < e; i++) {
                a = atr_at(i);
                f = a & A_FONT;
                if (f == of) f = kf;
                atr_put(i, (u8)(f | (a & keep) | kfl));
            }
            atr_put(e, (u8)((atr_at(e) & (k >= K_BULLET ? ~A_KIND : A_ALIGN)) | k));    /* (a list keeps its level) */
        } else {
            for (i = lo > p ? lo : p; i < e && i < hi; i++) atr_put(i, ka);
        }
        if (!sel_on || e + 1 >= hi || e >= doc_len - 1) break;
        p = (u16)(e + 1);
    }
    typing_attr = attr_at_cursor();
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
 * "WP", version 2, the length (2 bytes), the text, the attributes.
 * (Version 1, before paragraph kinds: a CR / FF's attribute also had the
 * typing style in its low bits, cleared when one is loaded.) */
#define WP_VERSION 2
u16 doc_pack(void) {
    mem_put(SCRATCH_LIN, 'W');
    mem_put(SCRATCH_LIN + 1, 'P');
    mem_put(SCRATCH_LIN + 2, WP_VERSION);
    mem_put(SCRATCH_LIN + 3, (u8)doc_len);
    mem_put(SCRATCH_LIN + 4, (u8)(doc_len >> 8));
    mem_copy(SCRATCH_LIN + 5, TEXT_LIN, doc_len);
    mem_copy(SCRATCH_LIN + 5 + doc_len, ATTR_LIN, doc_len);
    return 5 + 2 * doc_len;
}

u8 doc_unpack(void) {
    u16 n, i;
    u8 v = mem_get(SCRATCH_LIN + 2);
    if (mem_get(SCRATCH_LIN) != 'W' || mem_get(SCRATCH_LIN + 1) != 'P' || !v || v > WP_VERSION) return 1;
    n = mem_get(SCRATCH_LIN + 3) | (u16)(mem_get(SCRATCH_LIN + 4) << 8);
    if (n == 0 || n > DOC_MAX) return 1;
    mem_copy(TEXT_LIN, SCRATCH_LIN + 5, n);
    mem_copy(ATTR_LIN, SCRATCH_LIN + 5 + n, n);
    if (v == 1)
        for (i = 0; i < n; i++)
            if (is_break(txt_at(i))) atr_put(i, atr_at(i) & A_ALIGN);
    doc_len = n;
    doc_loaded(0);
    return 0;
}

/* a document just loaded or imported (doc_len characters): its final CR
 * made sure of, the cursor at its start, laid out anew; d: changed */
void doc_loaded(u8 d) {
    if (!doc_len || txt_at(doc_len - 1) != CH_CR) {
        if (doc_len < DOC_MAX) {
            atr_put(doc_len, 0);
            doc_len++;
        }
        txt_put(doc_len - 1, CH_CR);
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
    o_start();
    for (i = 0; i < doc_len; i++) {
        u8 c = as_ascii(txt_at(i));
        if (c >= 'a' && c <= 'z') c = (u8)(c - 32);
        else if (c >= 'A' && c <= 'Z') c = (u8)(c + 128);
        else if (c == CH_FF) c = CH_CR;
        else if (c == '_') c = 0xA4;
        o_put(c);
    }
    return o_n;
}

/* a PETSCII text file read (FREAD#) to SCRATCH_LIN, after GET_IMPORT_PREP
 * zeroed it, as the document; its length */
u16 doc_import(void) {
    u16 i;
    u8 c;
    build_start();
    for (i = 0; i < DOC_MAX - 1; i++) {
        c = mem_get(SCRATCH_LIN + i);
        if (!c) break;
        if (c != CH_CR) {
            if (c == 0xA0) c = ' ';
            c = petscii_to_ascii(c);
            if (!c) continue;
        }
        build_put(c);
    }
    /* past the file's end the drive gives CRs: one CR at the end */
    while (b_n > 1 && txt_at(b_n - 1) == CH_CR && txt_at(b_n - 2) == CH_CR) b_n--;
    i = build_end();
    doc_built();
    return i;
}

/* an import built (build_end): the cursor at the start, laid out anew */
void doc_built(void) {
    if (!b_done) return;
    b_done = 0;
    doc_loaded(1);
}
