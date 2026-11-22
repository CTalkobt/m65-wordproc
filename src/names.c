/* names.c — the file image (doc_pack, doc_unpack) and named styles'
 * commands (#5 step 3; wp.h): on the MEGA65 the code overlay "names", with
 * stylecold.s45's table routines, as cold.c's (called only from api.c's SYS entries
 * after ovl_fresh; it draws nothing, lays nothing out, runs no module:
 * api.c notes the selection for undo first, and redraws after)
 */
#include "wp.h"
#include "names_def.h"
#ifdef __cc45__
#pragma cc45 overlay(names)
#endif

/* entry k for the selection (its characters) and what is typed next */
static void apply(u8 k) {
    u16 lo, hi, i;
    if (sel_on) {
        lo = sel_lo();
        hi = sel_hi();
        for (i = lo; i < hi; i++)
            if (txt_at(i) != CH_CR && txt_at(i) != CH_FF) atr_put(i, k);
        dirty = 1;
        layout_edit(lo, hi, 0);
    }
    typing_attr = k;
}

/* the SHIFT+CTRL+S pop-up's command c (wp.h: NM_...): name n applied, or
 * redefined as the look of the selection's first character (or of what is
 * typed next) and applied, or its name taken away (its characters keep
 * their look); NM_NEW: msg (ASCII) the name of a new style of that look,
 * applied (a name there already: redefined). 1: no room for another. */
u8 names_cmd(u8 c) {
    u8 n = c & 15, k, e = sty(sel_on ? atr_at(sel_lo()) : typing_attr);
    if (c >= NM_NEW) {
        k = name_define(msg, e);
        if (!k) return 1;
    } else if (c >= NM_UNNAME) {
        for (k = n; k < NNAMES - 1; k++) {
            st_nidx[k] = st_nidx[k + 1];
            mem_copy(NAMES_LIN + (u16)k * NAME_LEN, NAMES_LIN + (u16)(k + 1) * NAME_LEN, NAME_LEN);
        }
        st_nidx[NNAMES - 1] = 0;
        sty_flush();
        return 0;
    } else {
        k = st_nidx[n];
        if (!k) return 0;
        if (c >= NM_REDEF) {
            mem_put(ST_LIN + k, e);
            sty_flush();
            layout_all();
        }
    }
    apply(k);
    return 0;
}

/* ---- files ----
 * The image BASIC saves with BSAVE and loads with BLOAD, at SCRATCH_LIN:
 * "WP", version 3, the length n (2 bytes), the text, the attributes (a
 * character's: an index into the style table), then the style table:
 * ST_SIZE, its entries' count (2 bytes), the entries; then (version 4)
 * the named styles: their count, each one's entry and name (NAME_LEN
 * bytes). Loaded, the table's entries are merged into the session's
 * (sty_find; a named one by its name, names_load) and the characters'
 * indexes changed to match, where they differ.
 * (Version 2, before the table: a character's attribute was its style;
 * version 1, before paragraph kinds: a CR / FF's attribute also had the
 * typing style in its low bits, cleared when one is loaded.) */
#define WP_VERSION 4

u16 doc_pack(void) {
    u8 h[5];
    h[0] = 'W';
    h[1] = 'P';
    h[2] = WP_VERSION;
    h[3] = (u8)doc_len;
    h[4] = (u8)(doc_len >> 8);
    mem_write(SCRATCH_LIN, h, 5);
    doc_out(5, doc_len, 0, doc_len);
    return names_save(sty_save((u16)(5 + 2 * doc_len)));
}

/* the named styles at the scratch area's o: their count, each one's
 * entry and name; the end */
u16 names_save(u16 o) {
    u8 k, n = names_count();
    mem_put(SCRATCH_LIN + o++, n);
    for (k = 0; k < n; k++) {
        mem_put(SCRATCH_LIN + o, st_nidx[k]);
        mem_copy(SCRATCH_LIN + o + 1, NAMES_LIN + (u16)k * NAME_LEN, NAME_LEN);
        o += NAME_LEN + 1;
    }
    return o;
}

/* a file's named styles at o, its table's entries at t (as sty_load left
 * them: each one's index here) of count n: each named here (a name there
 * already: redefined as the file's), its entry made that index; 1: any */
static u8 names_load(u16 o, u16 t, u16 n) {
    u8 k, c = mem_get(SCRATCH_LIN + o), f, i;
    char nm[NAME_LEN];
    for (k = 0; k < c && k < NNAMES; k++) {
        o++;
        f = mem_get(SCRATCH_LIN + o);
        mem_read((u8 *)nm, SCRATCH_LIN + o + 1, NAME_LEN);
        nm[NAME_LEN - 1] = 0;
        o += NAME_LEN;
        if (f >= n) continue;
        i = name_define(nm, sty(mem_get(SCRATCH_LIN + t + f)));
        if (i) mem_put(SCRATCH_LIN + t + f, i);
    }
    return c != 0;
}

u8 doc_unpack(void) {
    u16 n;
    u8 h[5], v, same;
    mem_read(h, SCRATCH_LIN, 5);
    v = h[2];
    n = h[3] | (u16)(h[4] << 8);
    if (h[0] != 'W' || h[1] != 'P' || !v || v > WP_VERSION || n == 0 || n > DOC_MAX) return 1;
    same = sty_load((u16)(5 + 2 * n), v);
    if (same > 1) return 1;
    if (v >= 4) {
        u16 t = (u16)(5 + 2 * n + 3), c = mem_get(SCRATCH_LIN + t - 2) | (u16)(mem_get(SCRATCH_LIN + t - 1) << 8);
        if (names_load(t + c, t, c)) same = 0;
    }
    g_len = 0;
    mem_copy(TEXT_LIN, SCRATCH_LIN + 5, n);
    mem_copy(ATTR_LIN, SCRATCH_LIN + 5 + n, n);
    if (v == 1 || !same) sty_remap(n, v == 1);
    doc_len = n;
    doc_loaded(0);
    return 0;
}
