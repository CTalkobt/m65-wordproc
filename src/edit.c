/* edit.c — DEFAULT0.OVL: undo (CTRL+Z), find and replace (CTRL+F, CTRL+H,
 * SHIFT+CTRL+F / B), styles
 *
 * A module kept in colour RAM and copied into mod_area for each call
 * (doc.c's edit_run: mod_cmd ED_..., arguments ed_a / ed_b, result
 * mod_ret). It is copied anew each time, so it keeps nothing of its own:
 * its state is doc.c's (u_..., f_len, r_len) and the buffers in colour RAM.
 *
 * Undo: the last edit kept as the text it replaced. An edit replaced the
 * document's u_pos..doc_len-u_tail-1 (the middle: all before and after it
 * is as it was) with what is there now; the old middle's u_n characters are
 * in the undo buffer (UNDO_LIN: text, then attributes at UNDO_MAX). Each
 * change notes the part of the document it will change (undo_note, before
 * it does: ED_NOTE): the middle grows to hold it, with the old text of the
 * part added. The edits from one key to the next cursor move or command
 * (typing, DEL) are one; a cursor move, a command or a search closes it
 * (u_open 0), and the next edit starts anew. Undo swaps the middle for the
 * undo buffer's, so a second undo redoes it. u_have 0: nothing to undo
 * (none yet, or more than UNDO_MAX characters).
 *
 * Styles: what F2-F7, CTRL+B / I / U and the style drop-down do, and a
 * list item's level (TAB, DEL, RETURN at its start).
 *
 * (Undo's part is edit_undo.c, compiled with -fno-zp-locals: it calls the
 * program's mem_copy, whose base-page bytes its own would share.)
 *
 * Find: the strings in colour RAM (FIND_LIN, folded to lower case: letters
 * match either case; REPL_LIN), set from msg (ASCII by then).
 */
#include "wp.h"

/* undo's record kept (edit_undo.c) */
void undo_rec(u16 from, u16 to);
u8 undo_swap(void);
#define note undo_rec

/* ---- styles (F3 / F5 / F7, CTRL+B / I / U; F2; F4; CTRL+D), list levels ---- */
static u8 is_brk(u8 c) { return c == CH_CR || c == CH_FF; }

/* the selection's styles: (a & keep) | set (each character's entry in the
 * style table changed for that style's); then the typing style is its
 * first character's */
static void restyle(u8 keep, u8 set) {
    u16 lo = sel_lo(), hi = sel_hi(), i;
    note(lo, hi);
    for (i = lo; i < hi; i++)
        if (!is_brk(txt_at(i))) atr_put(i, sty_find((u8)((sty(atr_at(i)) & keep) | set)));
    typing_attr = atr_at(lo);
    dirty = 1;
    layout_edit(lo, hi, 0);
}

/* bold / italic / underline / strikethrough / superscript / subscript:
 * over the selection (all on unless the first character has it already),
 * else for what is typed next (superscript and subscript: one or neither) */
static void toggle(u8 bit) {
    u8 clr = bit & A_SCRIPT ? A_SCRIPT : bit, a = sty(sel_on ? atr_at(sel_lo()) : typing_attr);
    if (!sel_on) typing_attr = sty_find(a & bit ? (u8)(a & ~bit) : (u8)((a & ~clr) | bit));
    else if (a & bit) restyle((u8)~bit, 0);
    else restyle((u8)~clr, bit);
}

/* font f (0xFF: the next, after the selection's first character's):
 * over the selection, else for what is typed next */
static void set_font(u8 f) {
    u8 a = sty(sel_on ? atr_at(sel_lo()) : typing_attr);
    if (f >= NFONTS) f = (u8)((a + 1) & A_FONT);   /* (NFONTS: 4) */
    if (sel_on) restyle((u8)~A_FONT, f);
    else typing_attr = sty_find((u8)((a & ~A_FONT) | f));
}

/* the effect pop-up's item n (CTRL+E): a font, then an effect */
static const u8 fx_bits[6] = { A_BOLD, A_ITAL, A_UNDER, A_STRIKE, A_SUP, A_SUB };
static void fx(u8 n) {
    if (n < NFONTS) set_font(n);
    else toggle(fx_bits[n - NFONTS]);
}

/* the alignment of the paragraph(s) at the cursor / selection: left,
 * centre, right, justify, left ... */
static void cycle_align(void) {
    u16 lo = sel_on ? sel_lo() : cur, hi = sel_on ? sel_hi() : cur;
    u16 e = para_end(lo), ps = para_start(lo);
    u8 al = (u8)((((atr_at(e) & A_ALIGN) >> A_ALIGN_SHIFT) + 1) & 3);
    note(ps, para_end(hi) + 1);
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
static u8 list_level(u8 in) {
    u16 e;
    u8 a;
    if (sel_on || cur != para_start(cur)) return 0;
    e = para_end(cur);
    a = atr_at(e);
    if ((a & A_KIND) < K_BULLET) return 0;
    note(e, e + 1);
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
static void style(u8 k) {
    u16 lo = cur, hi = cur, p, e, i, ps;
    u8 a, f, of, keep, ka = kind_attr[k], kf = ka & A_FONT, kfl = ka & ~A_FONT;
    if (sel_on) {
        lo = sel_lo();
        hi = sel_hi();
    }
    p = ps = para_start(lo);
    note(ps, para_end(hi) + 1);
    for (;;) {
        e = para_end(p);
        if (!sel_on || (lo <= p && hi >= e)) {
            a = kind_attr[atr_at(e) & A_KIND];
            of = a & A_FONT;
            keep = (u8)(~a & (A_BOLD | A_ITAL | A_UNDER | A_STRIKE | A_SCRIPT));
            for (i = p; i < e; i++) {
                a = sty(atr_at(i));
                f = a & A_FONT;
                if (f == of) f = kf;
                atr_put(i, sty_find((u8)(f | (a & keep) | kfl)));
            }
            atr_put(e, (u8)((atr_at(e) & (k >= K_BULLET ? ~A_KIND : A_ALIGN)) | k));    /* (a list keeps its level) */
        } else {
            for (i = lo > p ? lo : p; i < e && i < hi; i++) atr_put(i, sty_find(ka));
        }
        if (!sel_on || e + 1 >= hi || e >= doc_len - 1) break;
        p = (u16)(e + 1);
    }
    typing_attr = attr_at_cursor();
    dirty = 1;
    layout_edit(ps, e, 0);
}

static u8 fold(u8 c) { return (u8)(c - 'A') < 26 ? (u8)(c | 0x20) : c; }

/* msg as the find string (r 0) or the replacement (1) */
static void find_set(u8 r) {
    u8 n = 0;
    for (; msg[n]; n++)
        if (!r) msg[n] = (char)fold((u8)msg[n]);
    mem_write(r ? REPL_LIN : FIND_LIN, (const u8 *)msg, n);
    if (r) r_len = n;
    else f_len = n;
}

static u8 match_at(u16 p) {
    u8 i;
    for (i = 0; i < f_len; i++)
        if (fold(txt_at(p + i)) != mem_get(FIND_LIN + i)) return 0;
    return 1;
}

/* the find string's next place (d 1) or the one before (-1), from the
 * cursor, or the selection's start (a match found before), round the
 * document's end (wrap 1); selected there; 0: none */
static u8 find(signed char d, u8 wrap) {
    u16 n, p, k;
    if (!f_len || f_len >= doc_len) return 0;
    n = doc_len - f_len;        /* the places 0..n-1 (the final CR left out) */
    p = sel_on ? sel_lo() : cur;
    if (sel_on || d < 0) p += d;
    for (k = 0; k < n; k++, p += d) {
        if (p >= n) {
            if (!wrap) return 0;
            p = d > 0 ? 0 : n - 1;
        }
        if (match_at(p)) {
            sel_on = 1;
            sel_anchor = p;
            cur = p + f_len;
            goal_valid = 0;
            return 1;
        }
    }
    return 0;
}

/* the selection, when it is the find string, replaced (in its first
 * character's style); the cursor after the replacement */
static void replace_sel(void) {
    u16 lo = sel_lo();
    u8 a;
    if (!sel_on || sel_hi() - lo != f_len || !match_at(lo) || (u16)(doc_len - f_len + r_len) > DOC_MAX) return;
    a = atr_at(lo);
    note(lo, lo + f_len + 1);   /* (as doc_delete would: and the end after it) */
    del_raw(lo, lo + f_len);
    cur = lo;
    sel_on = 0;
    if (r_len) {
        room(lo, r_len);
        mem_copy(TEXT_LIN + lo, REPL_LIN, r_len);
        mem_fill(ATTR_LIN + lo, r_len, a);
        layout_edit(lo, lo, r_len);
        cur = lo + r_len;
    }
}

/* every place in the document replaced; how many */
static u16 replace_all(void) {
    u16 n = 0;
    cur = 0;
    sel_on = 0;
    while (find(1, 0)) {
        replace_sel();
        n++;
    }
    return n;
}

/* msg: s */
static void say(const char *s) {
    u8 i = 0;
    while ((msg[i] = *s++) != 0) i++;
}

/* msg: "Replaced n." (the number made at msg's end, then moved up) */
static void say_n(u16 n) {
    char *p = msg + 20, *d = msg + 9;
    p[0] = '.';
    p[1] = 0;
    do {
        *--p = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    say("Replaced ");
    while ((*d++ = *p++) != 0) {}
}

static const char ask_repl[] = "Replace? Y)es N)o A)ll";

/* Find and replace (FIND_...): 1 when a match is selected; what was found
 * said (msg), and while replacing (r_on: from FIND_SETR until a match is
 * not looked for, or none is found) the question for the next. */
void edit_main(void) {
    u8 c = mod_cmd, a = (u8)ed_a, r = 0;
    switch (c) {
    case ED_NOTE: note(ed_a, ed_b); return;
    case ED_UNDO: r = undo_swap(); goto out;
    case ED_TOGGLE: toggle(a); return;
    case ED_FONT: set_font(a); return;
    case ED_FX: fx(a); return;
    case ED_ALIGN: cycle_align(); return;
    case ED_LEVEL: r = list_level(a); goto out;
    case ED_STYLE: style(a); return;
    case FIND_SETF:
    case FIND_SETR:
        find_set((u8)(c - FIND_SETF));
        r_on = (u8)(c == FIND_SETR);
        say(r_on ? ask_repl : "");
        goto out;
    }
    msg[0] = 0;
    if (c == FIND_ALL) say_n(replace_all());
    else {
        if (c == FIND_REPL) replace_sel();      /* (then the next) */
        if (c != FIND_STOP) r = find(c == FIND_BACK ? -1 : 1, 1);
    }
    if (!r) {
        if (c != FIND_STOP && c != FIND_ALL) say("Not found.");
        r_on = 0;
    } else if (r_on) {
        say(ask_repl);
    }
out:
    mod_ret = r;
}
