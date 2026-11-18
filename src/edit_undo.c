/* edit_undo.c — DEFAULT0.OVL's undo: the record of the last edit kept
 * (undo_rec, as each change notes its part) and swapped back (undo_swap);
 * edit.c says how. Compiled with -fno-zp-locals: its functions' variables
 * are not in the base page, where they would share bytes with mem_copy's
 * (which it calls). */
#include "wp.h"

/* n characters, text and attributes, copied: from s to d, each the address
 * of a text whose attributes follow at the document's offset (ATTR_LIN -
 * TEXT_LIN) or, the f bits (1: d, 2: s) set, the undo buffer's (UNDO_MAX) */
#define AO (ATTR_LIN - TEXT_LIN)
static void copy2(u32 d, u32 s, u16 n, u8 f) {
    mem_copy(d, s, n);
    mem_copy(d + (f & 1 ? UNDO_MAX : AO), s + (f & 2 ? UNDO_MAX : AO), n);
}

void undo_rec(u16 from, u16 to) {
    u16 k, e;
    if (!u_open) {
        u_open = u_have = 1;
        u_pos = from;
        u_tail = doc_len - from;
        u_n = 0;
    }
    if (!u_have) return;
    e = doc_len - u_tail;
    if (from < u_pos) {
        k = u_pos - from;
        if (u_n + k > UNDO_MAX) goto big;
        copy2(SCRATCH_LIN, UNDO_LIN, u_n, 3);
        copy2(UNDO_LIN + k, SCRATCH_LIN, u_n, 3);
        copy2(UNDO_LIN, TEXT_LIN + from, k, 1);
        u_n += k;
        u_pos = from;
    }
    if (to > e) {
        k = to - e;
        if (u_n + k > UNDO_MAX) goto big;
        copy2(UNDO_LIN + u_n, TEXT_LIN + e, k, 1);
        u_n += k;
        u_tail = doc_len - to;
    }
    return;
big:
    u_have = 0;
}

/* the last edit undone (or, undone, done again); 1: there is none */
u8 undo_swap(void) {
    u16 e = doc_len - u_tail, m = e - u_pos, n = u_n;
    u8 keep = m <= UNDO_MAX;
    u_open = 0;
    if (!u_have) return 1;
    /* the middle now, kept for a redo (past where open_gap's copies go)
     * when the undo buffer can hold it */
    if (keep) copy2(PARK_LIN, TEXT_LIN + u_pos, m, 1);
    if (n > m) open_gap(e, n - m);
    else copy2(TEXT_LIN + u_pos + n, TEXT_LIN + e, u_tail, 0);
    copy2(TEXT_LIN + u_pos, UNDO_LIN, n, 2);
    if (keep) copy2(UNDO_LIN, PARK_LIN, m, 3);
    u_n = m;
    u_have = keep;
    doc_len += n - m;
    layout_edit(para_start(u_pos), e, (int16_t)(n - m));
    cur = u_pos + n;
    if (cur >= doc_len) cur = doc_len - 1;
    sel_on = 0;
    dirty = 1;
    goal_valid = 0;
    typing_attr = attr_at_cursor();
    return 0;
}

