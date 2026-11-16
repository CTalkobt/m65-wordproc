/* api.c — the functions the BASIC program calls (SYS {name}, a, x, y)
 *
 *   wp_init          the graphics screen on; the first time, the demo document
 *   wp_key(k)        an editing key (its PETSCII code from GET): text, RETURN,
 *                    DEL, INST, the cursor keys, HOME, CLR, TAB
 *   wp_cmd(c)        a command (CMD_...: styles, view, selection, clipboard, ...)
 *   wp_get(w)        a value for BASIC (RREG lo, hi): GET_... (files: the
 *                    image, plain text or Markdown at SCRATCH_LIN; Markdown
 *                    by the module MARKDOWN.OVL, RTF by RTF.OVL: 65535
 *                    when it is not loaded)
 *   wp_setmsg        msg (poked by BASIC as PETSCII) shown in the bottom bar
 *   wp_setname       fname (PETSCII) as the document's name
 *   wp_panel(n)      the menu: n rows of menu_text above the bottom bar (msg);
 *                    0 closes it
 *   wp_video(on)     1: the graphics screen (again, after the disk or text
 *                    screen); 0: BASIC's text screen back
 */
#include "wp.h"

#define CMD_BOLD 1
#define CMD_ITALIC 2
#define CMD_UNDER 3
#define CMD_FONT 4
#define CMD_ALIGN 5
#define CMD_PAGEBREAK 6
#define CMD_VIEW 7
#define CMD_MARK 8
#define CMD_UNMARK 9
#define CMD_COPY 10
#define CMD_CUT 11
#define CMD_PASTE 12
#define CMD_NEW 13
#define CMD_LINEEND 14
#define CMD_DOCEND 15
#define CMD_PGUP 16
#define CMD_PGDN 17
#define CMD_ALL 18
#define CMD_CLEAN 20
#define CMD_LIST 21
#define CMD_STYLE 32             /* + a kind (K_...): doc_style */

#define GET_DIRTY 0
#define GET_LEN 1
#define GET_PACK 2
#define GET_EXPORT 3
#define GET_UNPACK 4
#define GET_SEL 5
#define GET_IMPORT_PREP 6
#define GET_IMPORT 7
#define GET_MDEXPORT 8
#define GET_MDIMPORT 9
#define GET_LISTOK 10
#define GET_RTFEXPORT 11
#define GET_RTFBEGIN 12
#define GET_RTFCHUNK 13
#define GET_RTFEND 14
#define GET_FONTS 15
#define GET_KIND 16              /* the kind of the cursor's (the selection's first) paragraph */

static u8 started;
u8 mod_cmd;
u16 mod_ret;
/* BASIC reads this before its first SYS (before BSS is cleared), so it is
 * data: $5A until BASIC has loaded writer.fnt and set it to $A5 */
u8 fonts_loaded = 0x5A;

/* the edits laid out, the cursor in view, the screen drawn (what changed) */
static void refresh(u8 relayout) {
    (void)relayout;
    layout_update();
    scroll_to_cursor();
    render();
}

void wp_init(void) {
    video_on();
    render_invalidate();
    if (!started) {
        u8 i;
        const char *n = "untitled";
        for (i = 0; n[i]; i++) fname[i] = n[i];
        fname[i] = 0;
        view_mode = VIEW_PAGE;
        doc_new();
        started = 1;
    }
    msg[0] = 0;
    refresh(1);
}

static void after_move(void) {
    if (!sel_on) typing_attr = attr_at_cursor();
}

static void vertical(signed char d) {
    u16 l = line_of(cur);
    ensure_pos(line_end(l));    /* (the next line laid out) */
    if (!goal_valid) {
        goal_x = x_of(l, cur);
        goal_valid = 1;
    }
    if (d < 0) {
        if (l == 0) {
            cur = 0;
            return;
        }
        l--;
    } else {
        if (l + 1 >= nlines) {
            cur = (u16)(doc_len - 1);
            return;
        }
        l++;
    }
    cur = pos_at_x(l, goal_x);
}

static u16 line_y(u16 l) {
    struct line L;
    get_line(l, &L);
    return L.y;
}

/* a screenful up or down: the cursor a line at a time (keeping its x)
 * until it has moved VIEW_H or can go no further, the view with it */
static void page_move(signed char d) {
    u16 y = line_y(line_of(cur)), c, ny;
    do {
        c = cur;
        vertical(d);
        ny = line_y(line_of(cur));
    } while (cur != c && (u16)(d < 0 ? y - ny : ny - y) < VIEW_H);
    view_y = d > 0 ? (u16)(view_y + VIEW_H) : view_y > VIEW_H ? (u16)(view_y - VIEW_H) : 0;
}

/* CTRL + a cursor key selects: a selection started at the cursor when
 * there is none, else the one there is extended. The keys are read from the
 * keyboard (ctrl_keys): the KERNAL drops CTRL + right / down, and gives
 * CTRL + left / up as plain left / up on a MEGA65 but not in xemu; so a
 * cursor key from GET while CTRL is held is left out. */
static u8 ctrl_key;

static u8 is_crsr(u8 k) { return k == 29 || k == 157 || k == 17 || k == 145; }

static void say(const char *m) {
    u8 i;
    for (i = 0; m[i]; i++) msg[i] = m[i];
    msg[i] = 0;
}

static void typed(u8 c) {
    if (doc_type(c)) say("The document is full.");
}

/* Compose (CTRL+A), then two characters: the Latin-1 character the compose
 * table (writer.fnt: compose_at) gives them, in either order: "a -> a with
 * its diaeresis, 'e, `e, ^e, ~n, oa, ,c, ss, ae, o/, << ... (issue #1) */
#define K_COMPOSE 1
static u8 compose, comp1;

static u8 compose_find(u8 a, u8 b) {
    u8 e[3];
    u16 i = 0;
    do {
        mem_read(e, compose_at + i, 3);
        if (e[0] == a && e[1] == b) return e[2];
        i += 3;
    } while (e[0]);
    return 0;
}

/* writer.fnt, BLOADed to the scratch area: its parts to FONT_LIN, HI_LIN */
void font_install(void) {
    mem_copy(FONT_LIN, SCRATCH_LIN, font_lo_size);
    mem_copy(HI_LIN, SCRATCH_LIN + font_lo_size, font_hi_size);
}

void wp_key(u8 k) {
    u8 c, relayout = 0, mv = 1;
    msg[0] = 0;
    if (compose || k == K_COMPOSE) {
        c = petscii_to_ascii(k);
        if (!compose) {
            compose = 1;
        } else if (c && compose == 1) {
            comp1 = c;
            compose = 2;
        } else {
            compose = 0;
            if (c) c = compose_find(comp1, c);
            if (c) typed(c);
            else say("No such character.");
        }
        if (compose) say("Compose");
        refresh(1);
        return;
    }
    if (is_crsr(k)) {
        if (!ctrl_key) {
            if (kbd_mods() & KM_CTRL) return;
        } else if (!sel_on) {
            sel_on = 1;
            sel_anchor = cur;
        }
    }
    switch (k) {
    case 29:                    /* right */
        if (cur < doc_len - 1) cur++;
        goal_valid = 0;
        break;
    case 157:                   /* left */
        if (cur > 0) cur--;
        goal_valid = 0;
        break;
    case 17:                    /* down */
        vertical(1);
        break;
    case 145:                   /* up */
        vertical(-1);
        break;
    case 19: {                  /* HOME: the line's start (there already: its end) */
        u16 l = line_of(cur);
        u16 st = line_start(l);
        cur = cur == st ? (u16)(line_end(l) - 1) : st;
        goal_valid = 0;
        break;
    }
    case 147:                   /* CLR: the document's start (there already: its end) */
        cur = cur ? 0 : (u16)(doc_len - 1);
        goal_valid = 0;
        break;
    case 13:
    case 141:
        doc_type(CH_CR);
        relayout = 1;
        break;
    case 20:                    /* DEL */
        doc_backspace();
        relayout = 1;
        break;
    case 148:                   /* INST: delete forwards */
        doc_delete_fwd();
        relayout = 1;
        break;
    case 9:                     /* TAB: four spaces */
        for (c = 0; c < 4; c++) doc_type(' ');
        relayout = 1;
        break;
    default:
        c = petscii_to_ascii(k);
        if (!c) return;
        typed(c);
        relayout = 1;
        mv = 0;
        break;
    }
    if (mv && !relayout) after_move();
    refresh(relayout);
}

void wp_cmd(u8 c) {
    u8 relayout = 1;
    u16 l;
    msg[0] = 0;
    switch (c) {
    case CMD_BOLD: doc_toggle(A_BOLD); break;
    case CMD_ITALIC: doc_toggle(A_ITAL); break;
    case CMD_UNDER: doc_toggle(A_UNDER); break;
    case CMD_FONT: doc_cycle_font(); break;
    case CMD_ALIGN: doc_cycle_align(); break;
    case CMD_PAGEBREAK: doc_type(CH_FF); break;
    case CMD_VIEW:
        view_mode = (u8)(view_mode ^ 1);
        render_invalidate();
        layout_all();
        view_y = 0;
        break;
    case CMD_MARK:
        if (sel_on) sel_on = 0;
        else {
            sel_on = 1;
            sel_anchor = cur;
        }
        relayout = 0;
        break;
    case CMD_UNMARK:
        sel_on = 0;
        relayout = 0;
        break;
    case CMD_COPY:
        doc_copy();
        sel_on = 0;
        relayout = 0;
        break;
    case CMD_CUT: doc_cut(); break;
    case CMD_PASTE: doc_paste(); break;
    case CMD_NEW: doc_new(); break;
    case CMD_LINEEND:
        l = line_of(cur);
        cur = (u16)(line_end(l) - 1);
        goal_valid = 0;
        after_move();
        relayout = 0;
        break;
    case CMD_DOCEND:
        cur = (u16)(doc_len - 1);
        goal_valid = 0;
        after_move();
        relayout = 0;
        break;
    case CMD_PGUP:
        page_move(-1);
        after_move();
        relayout = 0;
        break;
    case CMD_PGDN:
        page_move(1);
        after_move();
        relayout = 0;
        break;
    case CMD_ALL:
        sel_on = 1;
        sel_anchor = 0;
        cur = (u16)(doc_len - 1);
        relayout = 0;
        break;
    case CMD_CLEAN:
        dirty = 0;
        relayout = 0;
        break;
    case CMD_LIST:              /* list mode (LIST.OVL) on / off */
        if (list_ok) list_on ^= 1;
        render_invalidate();
        relayout = 0;
        break;
    default:
        if ((u8)(c - CMD_STYLE) >= NKINDS) return;
        doc_style((u8)(c - CMD_STYLE));
        break;
    }
    refresh(relayout);
}

u16 wp_get(u8 w) {
    switch (w) {
    case GET_DIRTY: return dirty;
    case GET_LEN: return doc_len;
    case GET_PACK: return doc_pack();
    case GET_EXPORT: return doc_export();
    case GET_UNPACK: {
        u8 r = doc_unpack();
        refresh(1);
        return r;
    }
    case GET_SEL: return sel_on;
    case GET_IMPORT_PREP:
        mem_fill(SCRATCH_LIN, 30720u, 0);       /* (all the scratch area: Markdown is longer) */
        return 0;
    case GET_MDEXPORT: return mod_run(MOD_MD_EXPORT);
    case GET_RTFEXPORT: return mod_run(MOD_RTF_EXPORT);
    case GET_RTFBEGIN: return mod_run(MOD_RTF_BEGIN);
    case GET_RTFCHUNK: return mod_run(MOD_RTF_CHUNK);
    case GET_LISTOK: return list_check();
    case GET_FONTS: font_install(); return 0;
    case GET_KIND: return cur_kind();
    case GET_IMPORT:
    case GET_MDIMPORT:
    case GET_RTFEND: {
        u16 n = w == GET_IMPORT ? doc_import() : mod_run(w == GET_MDIMPORT ? MOD_MD_IMPORT : MOD_RTF_END);
        refresh(1);
        return n;
    }
    }
    return 0;
}

static void to_ascii(char *s, u8 max) {
    u8 i;
    for (i = 0; i < max && s[i]; i++) {
        u8 c = petscii_to_ascii((u8)s[i]);
        s[i] = (char)(c ? c : '?');
    }
    s[i] = 0;
}

void wp_setmsg(void) {
    to_ascii(msg, 78);
    render_bars();
}

/* the menu's panel above the bottom bar: n rows (0 or PANEL_ROWS) from
 * menu_text, the bar's from msg; closed (0): msg emptied, the view drawn
 * again under it */
void wp_panel(u8 n) {
    to_ascii(menu_text[0], 78);
    to_ascii(menu_text[1], 78);
    to_ascii(msg, 78);
    panel = n;
    if (!n) msg[0] = 0;
    render();
}

void wp_setname(void) {
    to_ascii(fname, 16);
    render_bars();
}

void wp_video(u8 on) {
    if (on) {
        video_on();
        render_invalidate();
        refresh(0);
    } else {
        video_off();
    }
}

/* RUN/STOP ignored while editing (on 1), or not (0): the KERNAL's STOP
 * vector ($0328) pointed at a routine that answers "not pressed" as the
 * KERNAL's own nstop does (A = $FF, the flags of CMP #$7F). It must be in
 * RAM that BASIC's memory map shows (BASIC 65 runs with its ROM over
 * $2000-$BFFF), so it goes to $0378, free between the KERNAL's file tables
 * (to $0369) and its RAM code ($0380). */
#define NOSTOP 0x0378
static u16 old_stop;
void wp_stopvec(u8 on) {
#ifdef __cc45__
    static const u8 code[5] = { 0xA9, 0xFF, 0xC9, 0x7F, 0x60 };     /* lda #$ff : cmp #$7f : rts */
    volatile u16 *v = (volatile u16 *)0x0328;
    u8 i;
    if (on) {
        for (i = 0; i < 5; i++) ((volatile u8 *)NOSTOP)[i] = code[i];
        if (*v != NOSTOP) old_stop = *v;
        *v = NOSTOP;
    } else if (old_stop) {
        *v = old_stop;
    }
#else
    (void)on;
    (void)old_stop;
#endif
}

/* while no key waits: more of the document laid out; 1 while there is more
 * (the screen drawn again when the last of it is: the page count, the
 * desk below the last page) */
/* CTRL + a cursor key: the keyboard read here (BASIC calls wp_idle while
 * no key waits), repeated as the KERNAL's keys are: after REP_FIRST
 * frames, then every REP_NEXT */
#define REP_FIRST 20
#define REP_NEXT 3
static u8 rep_key, rep_at;

static void ctrl_keys(void) {
    u8 k = kbd_ctrl_crsr(), now = frames();
    if (k != rep_key) {
        rep_key = k;
        rep_at = (u8)(now + REP_FIRST);
    } else if (!k || (signed char)(now - rep_at) < 0) {
        return;
    } else {
        rep_at = (u8)(now + REP_NEXT);
    }
    if (!k) return;
    ctrl_key = 1;
    wp_key(k);
    ctrl_key = 0;
}

/* between keys: CTRL + cursor keys; more of the layout (then the
 * screen drawn again); 1 while there is layout left to do */
u8 wp_idle(void) {
    u8 more;
    ctrl_keys();
    if (layout_done) return 0;
    more = layout_idle();
    if (!more) render();
    return more;
}

void wp_redraw(void) {
    render_invalidate();
    refresh(1);
}
