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

/* undo's record and the find strings' lengths (edit.c) */
u16 u_pos, u_tail, u_n;
u8 u_have, u_open, f_len, r_len, r_on;

void doc_new(void) {
    g_len = 0;
    txt_put(0, CH_CR);
    atr_put(0, 0);
    doc_len = 1;
    cur = 0;
    sel_on = 0;
    typing_attr = 0;
    dirty = 0;
    view_y = 0;
    goal_valid = 0;
    u_have = 0;
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


/* its kind (K_...) */
u8 para_kind(u16 i) { return atr_at(para_end(i)) & A_KIND; }

u8 cur_kind(void) { return para_kind(sel_on ? sel_lo() : cur); }

static u8 is_break(u8 c) { return c == CH_CR || c == CH_FF; }

/* the start of the paragraph holding i */
u16 para_start(u16 i) {
    while (i > 0 && !is_break(txt_at(i - 1))) i--;
    return i;
}

/* the style for typing at the cursor (its index): the character before
 * it, or (at a paragraph's start) the one after it */
u8 attr_at_cursor(void) {
    if (cur > 0 && !is_break(txt_at(cur - 1))) return atr_at(cur - 1);
    if (is_break(txt_at(cur))) return sty_find(kind_attr[atr_at(cur) & A_KIND]);   /* (an empty paragraph: its kind's) */
    return atr_at(cur);
}

/* ---- the gap: room kept in the store (attic RAM) at the last edit, so
 * that typing there does not move the rest of the document along. Before
 * the document's character g_at the store holds g_len unused bytes: the
 * characters from g_at on are g_len further on (txt_at, atr_at, txt_put,
 * atr_put, doc_out map them; g_len 0: no gap). An insertion fills it from
 * its start; a deletion widens it; an edit elsewhere moves it there (the
 * characters between moved across: as far as the edits are apart). Only
 * when it is used up does the rest of the document move: GAP_SPAN more.
 * Anything that moves the store's text itself (undo, open_gap) closes it
 * first (gap_close: the gap moved to the end, where it is no gap). ---- */
u16 g_at, g_len;

/* n characters, text and attributes, moved from the store's s to its d
 * (up: through the scratch area, as the DMA copies upwards only when the
 * ranges do not overlap); on the MEGA65 gapmove.s45 */
#ifndef __cc45__
void move2(u16 d, u16 s, u16 n) {
    u32 t = TEXT_LIN, b;
    u8 k;
    for (k = 0; k < 2; k++, t += AO) {
        b = t + s;
        if (d > s) {
            mem_copy(SCRATCH_LIN, b, n);
            b = SCRATCH_LIN;
        }
        mem_copy(t + d, b, n);
    }
}

/* the gap moved to before character p (gapmove.s45 on the MEGA65) */
void gap_to(u16 p) {
    u16 s = p, d = p + g_len, n = g_at - p;     /* (before it: up across it) */
    if (p > g_at) {
        s = g_at + g_len;
        d = g_at;
        n = p - g_at;
    }
    if (g_len) move2(d, s, n);
    g_at = p;
}
#endif

void gap_close(void) {
    gap_to(doc_len);
    g_len = 0;
}

/* n characters from i on: text to bank 5's d (SCRATCH_LIN + d: the scratch
 * area, the clipboard), attributes to d + o (a byte at a time, round the
 * gap: smaller than DMA copies, and only a copy, a save or undo's note) */
void doc_out(u16 d, u16 o, u16 i, u16 n) {
    u8 FAR *p = FARP(SCRATCH_LIN + d);
    for (; n; n--, i++, p++) {
        p[0] = txt_at(i);
        p[o] = atr_at(i);
    }
}

/* room for n characters at `at` in the store, without the gap (closed
 * first): the text and attributes after it moved up */
void open_gap(u16 at, u16 n) {
    gap_close();
    move2(at + n, at, doc_len - at);
}

/* ---- undo, find, styles: DEFAULT0.OVL (edit.c), a module kept in colour RAM
 * (EDIT_LIN) and copied for each call (edit_run) into mod_area, up to
 * LIST.OVL: its first MOD_SCRATCH bytes are buffers the drawing uses only
 * while it draws, the rest its caches, kept meanwhile in colour RAM
 * (CACHE_LIN). Its state is here (it keeps none of its own). ed_busy: it is
 * running (its edits' own undo_notes are then left out: it notes them). */
#ifdef __cc45__
u8 ed_ok;
#else
u8 ed_ok = 1;                   /* (the host: edit.c linked in) */
#endif
u8 ed_busy;
u16 ed_a, ed_b;

/* the drawing's caches: mod_area.r past its buffers (MOD_SCRATCH), not
 * all of it up to EDIT_SIZE, which is more than CACHE_LIN has room for */
#define CACHE_SIZE (sizeof mod_area.r - MOD_SCRATCH)
#ifdef __cc45__
typedef char cache_fits[CACHE_SIZE <= EDIT_LIN - CACHE_LIN ? 1 : -1];
#endif

u16 edit_run(u8 cmd) {
    mod_ret = 0xFFFF;
    if (!ed_ok || ed_busy) return mod_ret;
    ed_busy = 1;
    mod_cmd = cmd;
#ifdef __cc45__
    mem_copy(CACHE_LIN, (u32)(u16)(mod_area.m + MOD_SCRATCH), CACHE_SIZE);
    mem_copy((u32)(u16)mod_area.m, EDIT_LIN, EDIT_SIZE);
    mod_entry();
    mod_area.m[0] = 0;          /* (the drawing overwrites it: not kept again) */
    mem_copy((u32)(u16)(mod_area.m + MOD_SCRATCH), CACHE_LIN, CACHE_SIZE);
#else
    edit_main();
#endif
    ed_busy = 0;
    return mod_ret;
}

u16 ed(u8 cmd, u16 a) {
    ed_a = a;
    return edit_run(cmd);
}

/* the document's from..to-1 about to change (to: before the change) */
void undo_note(u16 from, u16 to) {
    ed_b = to;
    ed(ED_NOTE, from);
}

/* DEFAULT0.OVL just loaded at mod_area by BASIC: kept (GET_LISTOK) */
void edit_keep(void) {
#ifdef __cc45__
    if (!mod_ok(mod_area.m)) return;
    mem_copy(EDIT_LIN, (u32)(u16)mod_area.m, EDIT_SIZE);
    mod_area.m[0] = 0;
    ed_ok = 1;
    render_invalidate();
#endif
}


/* room for n characters at `at` (the edit noted for undo), the document
 * that much longer; 1: none */
u8 make_room(u16 at, u16 n) {
    if (doc_len + n > DOC_MAX) return 1;
    undo_note(at, at);
    room(at, n);
    return 0;
}

/* (and without noting it: DEFAULT0.OVL's, which notes its own): the gap
 * moved to at (and when it is too small, GAP_SPAN more opened there), its
 * first n bytes the room */
void room(u16 at, u16 n) {
    gap_to(at);
    if (g_len < n) {
        move2(at + GAP_SPAN + n, at + g_len, doc_len - at);
        g_len = GAP_SPAN + n;
    }
    g_at += n;
    g_len -= n;
    doc_len += n;
    dirty = 1;
}

/* n characters (s, attributes a) inserted at `at`; 1 when there is no room */
u8 doc_insert(u16 at, const u8 *s, const u8 *a, u16 n) {
    u16 i, lo = at;
    if (n == 0) return 0;
    if (make_room(at, n)) return 1;
    mem_write(TEXT_LIN + at, s, n);
    mem_write(ATTR_LIN + at, a, n);
    for (i = 0; i < n; i++)
        if (is_break(s[i])) lo = 0xFFFF;
    /* a paragraph end inserted: the lines before it, in its paragraph,
     * take its alignment */
    if (lo == 0xFFFF) lo = para_start(at);
    layout_edit(lo, at, (int16_t)n);
    return 0;
}

/* characters from..to-1 removed (never the final CR) */
void doc_delete(u16 from, u16 to) {
    if (to > doc_len - 1) to = doc_len - 1;
    if (from >= to) return;
    undo_note(from, to + 1);    /* (and the end after it, which may change) */
    del_raw(from, to);
}

/* from..to-1 removed (from < to <= doc_len - 1), not noted for undo
 * (DEFAULT0.OVL's: it notes its own) */
void del_raw(u16 from, u16 to) {
    u16 n, i, lo = from;
    /* a paragraph end deleted: the paragraph before it takes the next one's
     * kind and alignment, unless nothing of that one is left (the end after
     * the deletion: an empty paragraph joined), when it keeps its own */
    for (i = from; i < to; i++)
        if (is_break(txt_at(i))) {
            lo = para_start(from);
            if (is_break(txt_at(to))) atr_put(to, atr_at(i));
            break;
        }
    /* the gap widened over them (moved to whichever end is nearer it) */
    if (g_at > from) {
        gap_to(to);
        g_at = from;
    } else {
        gap_to(from);
    }
    n = to - from;
    g_len += n;
    doc_len -= n;
    dirty = 1;
    layout_edit(lo, to, (int16_t)-(int16_t)n);
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
        typing_attr = atr_at(sel_lo());
        delete_selection();
    }
    a = typing_attr;
    at_end = is_break(txt_at(cur));
    if (brk) {
        if (nlines >= MAX_LINES - 8) return 1;  /* the line table is (almost) full */
        undo_note(cur, cur + 1);        /* (the end after it may change) */
        /* the new paragraph end keeps the paragraph's kind and alignment */
        a = atr_at(para_end(cur)) & A_PARA;
    }
    if (doc_insert(cur, &c, &a, 1)) return 1;
    cur++;
    k = (u8)((a & A_KIND) - K_H1);
    if (brk && at_end && k < 3) {
        /* after a heading's end: a normal paragraph */
        atr_put(cur, (u8)(atr_at(cur) & ~A_KIND));
        typing_attr = sty_find(kind_attr[K_NORMAL]);
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

/* ---- the clipboard (bank 4: text, then attributes) ---- */
void doc_copy(void) {
    u16 lo, n;
    if (!sel_on) return;
    lo = sel_lo();
    n = sel_hi() - lo;
    if (n > CLIP_MAX) n = CLIP_MAX;
    doc_out((u16)(CLIP_LIN - SCRATCH_LIN), CLIP_MAX, lo, n);
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
    if (make_room(cur, n)) return;
    mem_copy(TEXT_LIN + cur, CLIP_LIN, n);
    mem_copy(ATTR_LIN + cur, CLIP_LIN + CLIP_MAX, n);
    layout_edit(para_start(cur), cur, (int16_t)n);
    cur += n;
    goal_valid = 0;
}

/* ---- files ----
 * The image BASIC saves with BSAVE and loads with BLOAD, at SCRATCH_LIN:
 * "WP", version 3, the length n (2 bytes), the text, the attributes (a
 * character's: an index into the style table), then the style table:
 * ST_SIZE, its entries' count (2 bytes), the entries. Loaded, the table's
 * entries are merged into the session's (sty_find) and the characters'
 * indexes changed to match, where they differ.
 * (Version 2, before the table: a character's attribute was its style;
 * version 1, before paragraph kinds: a CR / FF's attribute also had the
 * typing style in its low bits, cleared when one is loaded.) */
#define WP_VERSION 3

/* the scratch area's byte o (one 32-bit sum, not one at each use) */
static u32 scr(u16 o) { return SCRATCH_LIN + o; }

u16 doc_pack(void) {
    u16 o = (u16)(5 + 2 * doc_len);
    u8 h[5];
    h[0] = 'W';
    h[1] = 'P';
    h[2] = WP_VERSION;
    h[3] = (u8)doc_len;
    h[4] = (u8)(doc_len >> 8);
    mem_write(SCRATCH_LIN, h, 5);
    doc_out(5, doc_len, 0, doc_len);
    h[0] = ST_SIZE;
    h[1] = (u8)st_n;
    h[2] = (u8)(st_n >> 8);
    mem_write(scr(o), h, 3);
    mem_copy(scr((u16)(o + 3)), ST_LIN, st_n);
    return (u16)(o + 3 + st_n);
}

u8 doc_unpack(void) {
    u16 n, o, i, cnt = 32;
    u8 h[5], v, same, *m = mod_area.r.band;  /* (the drawing's: free here) */
    mem_read(h, SCRATCH_LIN, 5);
    v = h[2];
    n = h[3] | (u16)(h[4] << 8);
    if (h[0] != 'W' || h[1] != 'P' || !v || v > WP_VERSION || n == 0 || n > DOC_MAX) return 1;
    /* the file's table (before version 3: entries 0-31, those styles),
     * each entry's index in the session's */
    o = (u16)(5 + 2 * n);
    if (v < 3) {
        for (i = 0; i < 32; i++) m[i] = (u8)i;
    } else {
        mem_read(h, scr(o), 3);
        cnt = h[1] | (u16)(h[2] << 8);
        if (h[0] != ST_SIZE || cnt > 256) return 1;
        mem_read(m, scr((u16)(o + 3)), cnt);
    }
    same = sty_merge(m, cnt);
    g_len = 0;
    mem_copy(TEXT_LIN, SCRATCH_LIN + 5, n);
    mem_copy(ATTR_LIN, scr((u16)(5 + n)), n);
    if (v == 1 || !same) sty_remap(n, v == 1);
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
    u_have = 0;
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
