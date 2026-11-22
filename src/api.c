/* api.c — the functions the BASIC program calls (SYS {name}, a, x, y)
 *
 *   wp_init          the graphics screen on; the first time, the demo document
 *   wp_key(k)        a key (its PETSCII code from GET): text, RETURN, DEL,
 *                    INST, the cursor keys, HOME, CLR, TAB, the F-keys' and
 *                    CTRL's commands; those BASIC does: its ACT_... (else 0)
 *   wp_cmd(c)        a command (CMD_...: styles, view, selection, clipboard, ...)
 *   wp_find(op)      find and replace: FIND_... (the strings poked into msg)
 *   wp_get(w)        a value for BASIC (RREG lo, hi): GET_... (files: the
 *                    image, plain text or Markdown at SCRATCH_LIN; Markdown
 *                    by the module MARKDOWN.OVL, RTF by RTF.OVL, HTML by
 *                    HTML.OVL and HTMLIN.OVL: 65535
 *                    when it is not loaded)
 *   wp_say(n)        message n of the table (WRITER.MSG, at MSG_TAB) shown in
 *                    the bottom bar, with what BASIC poked into msg (PETSCII)
 *                    in its place holders
 *   wp_setname       fname (PETSCII) as the document's name
 *   wp_panel(n)      the menu: n rows (MSG_MENU, ...) above the bottom bar
 *                    (MSG_VIEW); 0 closes it
 *   wp_video(on)     1: the graphics screen (again, after the disk or text
 *                    screen); 0: BASIC's text screen back
 */
#include "wp.h"
#include "msg.h"                /* MSG_... (made by tools/mkmsg.py) */

/* the messages: src/messages.txt, BLOADed by BASIC to MSG_TAB (free RAM
 * below the program, as the base page and stack: lowmem.s45) */
#ifdef __cc45__
#define msg_tab ((const char *)MSG_TAB)
#define msg_parm ((char *)MSG_PARM)
#else
extern const char msg_tab[];    /* (the host: build/msgtab.c) */
static char msg_parm[81];
#endif

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
#define CMD_UNDO 22
#define CMD_STYLE 32             /* + a kind (K_...): doc_style */
#define CMD_FX 48                /* + the effect pop-up's item (CTRL+E): ED_FX */
#define NFX 11

#define GET_DIRTY 0
#define GET_LEN 1
#define GET_PACK 2
#define GET_UNPACK 4
#define GET_SEL 5
#define GET_IMPORT_PREP 6
#define GET_IMPORT 7
#define GET_MDIMPORT 9
#define GET_LISTOK 10
#define GET_RTFBEGIN 12
#define GET_RTFCHUNK 13
#define GET_RTFEND 14
#define GET_FONTS 15
#define GET_KIND 16              /* the kind of the cursor's (the selection's first) paragraph */
#define GET_XEXPORT 17           /* + the file's kind (BASIC's fk): the document exported to
                                    SCRATCH_LIN as 0 text, 1 Markdown, 2 RTF, 3 HTML (a module) */
#define GET_HTMLBEGIN 21         /* HTML import (HTMLIN.OVL), streamed as RTF's: begin, chunk, end */
#define GET_HTMLCHUNK 22
#define GET_HTMLEND 23

/* what wp_key leaves to BASIC (its ON ... GOSUB) */
#define ACT_HELP 1                 /* F8, HELP */
#define ACT_COPY 2                 /* F10 */
#define ACT_STYLE 3                /* CTRL+D: the style drop-down */
#define ACT_REDRAW 4               /* CTRL+L */
#define ACT_ESC 5                  /* ESC: the menu */
#define ACT_FIND 6                 /* CTRL+F */
#define ACT_REPLACE 7              /* CTRL+H */
#define ACT_SAVE 8                 /* CTRL+W: save, asking only for a new name */
#define ACT_FX 9                   /* CTRL+E: the font and effect pop-up */

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
    ovl_fresh();
    video_on();
    render_invalidate();
    if (!started) {
        sty_init();
        view_mode = VIEW_PAGE;
        doc_new();
        started = 1;
    }
    msg[0] = 0;
    refresh(1);
}

static void after_move(void) {
    u_open = 0;                 /* (an edit after a move: undone on its own) */
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

static const char ed_need[] = "No DEFAULT0.OVL.";

static void typed(u8 c) {
    if (doc_type(c)) say("The document is full.");
}

/* Compose (CTRL+A), then two characters: the character compose_find
 * (cold.c) gives them (issue #1) */
#define K_COMPOSE 1
static u8 compose, comp1;

void wp_cmd(u8 c);
u16 wp_find(u8 op);

/* Keys that are commands, and what each is: a command (CMD_...), or one BASIC
 * does (K_BAS | ACT_..., from 1); K_CTRL: only with CTRL held, which gives
 * codes some F-keys give too (so first). CTRL+B / I / U / Z bold, italic,
 * underline, undo; CTRL+F / H / W find, replace, save; F1 view, F3 / F5 /
 * F7 bold / italic / underline, F2 font, F4 align, F6 page break, F9 mark,
 * F11 cut, F12 paste, F13 / F14 page up / down; F8, HELP the help, F10
 * copy, CTRL+D the style drop-down, CTRL+E the font and effect pop-up,
 * CTRL+L redraw, ESC the menu.
 * (SHIFT+CTRL+F / B: the next match, the one before.) */
#define K_CTRL 0x40
#define K_BAS 0x80
static const u8 keys[] = {
    2, K_CTRL | CMD_BOLD, 9, K_CTRL | CMD_ITALIC, 0x15, K_CTRL | CMD_UNDER, 0x1A, K_CTRL | CMD_UNDO,
    6, K_CTRL | K_BAS | ACT_FIND, 8, K_CTRL | K_BAS | ACT_REPLACE, 0x17, K_CTRL | K_BAS | ACT_SAVE,
    5, K_CTRL | K_BAS | ACT_FX,
    0x85, CMD_VIEW, 0x86, CMD_BOLD, 0x87, CMD_ITALIC, 0x88, CMD_UNDER, 0x89, CMD_FONT, 0x8A, CMD_ALIGN,
    0x8B, CMD_PAGEBREAK, 0x10, CMD_MARK, 0x16, CMD_CUT, 0x17, CMD_PASTE, 0x19, CMD_PGUP, 0x1A, CMD_PGDN,
    0x8C, K_BAS | ACT_HELP, 0x84, K_BAS | ACT_HELP, 0x15, K_BAS | ACT_COPY, 0x04, K_BAS | ACT_STYLE,
    0x0C, K_BAS | ACT_REDRAW, 0x1B, K_BAS | ACT_ESC,
};

u8 wp_key(u8 k) {
    u8 c, relayout = 0, mv = 1, m = kbd_mods(), v;
    if ((m & KM_CTRL) && (m & KM_SHIFT) && (k == 6 || k == 2)) {
        wp_find(k == 2 ? FIND_BACK : FIND_FWD);
        return 0;
    }
    for (c = 0; c < sizeof keys; c += 2) {
        v = keys[c + 1];
        if (k != keys[c] || ((v & K_CTRL) && !(m & KM_CTRL))) continue;
        if (v & K_BAS) return v & 0x0F;
        wp_cmd(v & 0x3F);
        return 0;
    }
    msg[0] = 0;
    ovl_fresh();                /* (petscii_to_ascii, compose_find) */
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
        return 0;
    }
    if (is_crsr(k)) {
        if (!ctrl_key) {
            if (m & KM_CTRL) return 0;
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
    case 9:                     /* TAB: four spaces (at a list item's start: a level in) */
        if (doc_list_level(1)) break;
        for (c = 0; c < 4; c++) doc_type(' ');
        relayout = 1;
        break;
    default:
        c = key_char(k);
        if (!c) return 0;
        typed(c);
        relayout = 1;
        mv = 0;
        break;
    }
    if (mv && !relayout) after_move();
    refresh(relayout);
    return 0;
}

void wp_cmd(u8 c) {
    u8 relayout = 1;
    u16 l;
    msg[0] = 0;
    u_open = 0;                 /* (each command an edit of its own) */
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
    case CMD_UNDO:
        l = edit_run(ED_UNDO);
        if (l) say(l == 1 ? "Nothing to undo." : ed_need);
        break;
    case CMD_LIST:              /* list mode (LIST.OVL) on / off */
        if (list_ok) list_on ^= 1;
        render_invalidate();
        relayout = 0;
        break;
    default:
        if ((u8)(c - CMD_FX) < NFX) ed(ED_FX, (u8)(c - CMD_FX));
        else if ((u8)(c - CMD_STYLE) < NSTYLES) doc_style((u8)(c - CMD_STYLE));
        else return;
        break;
    }
    refresh(relayout);
}

/* a file's image or an export in the scratch area n bytes long (0: all of
 * it): past UNDO_OFS, undo's buffer is gone */
static u16 scratched(u16 n) {
    if (!n || n > UNDO_OFS) u_have = 0;
    return n;
}

u16 wp_get(u8 w) {
    ovl_fresh();
    switch (w) {
    case GET_DIRTY: return dirty;
    case GET_LEN: return doc_len;
    case GET_PACK: return scratched(doc_pack());
    case GET_UNPACK: {          /* (the file BLOADed over undo's buffer) */
        u8 r;
        u_have = 0;
        r = doc_unpack();
        refresh(1);
        return r;
    }
    case GET_SEL: return sel_on;
    case GET_IMPORT_PREP:
        u_have = 0;
        mem_fill(SCRATCH_LIN, SCRATCH_MAX, 0);       /* (all the scratch area: Markdown is longer) */
        return 0;
    case GET_XEXPORT: return scratched(doc_export());
    case GET_XEXPORT + 1: return scratched(mod_run(MOD_MD_EXPORT));
    case GET_XEXPORT + 2: return scratched(mod_run(MOD_RTF_EXPORT));
    case GET_XEXPORT + 3: return scratched(mod_run(MOD_HTML_EXPORT));
    case GET_RTFBEGIN: return mod_run(MOD_RTF_BEGIN);
    case GET_RTFCHUNK: return mod_run(MOD_RTF_CHUNK);
    case GET_HTMLBEGIN:
    case GET_HTMLCHUNK: return mod_run((u8)(w + (MOD_HTML_BEGIN - GET_HTMLBEGIN)));
    case GET_LISTOK:            /* (and DEFAULT0.OVL, loaded at mod_area) */
        edit_keep();
        return list_check();
    case GET_FONTS:             /* (and whether there is attic RAM, the text's: 0 yes) */
        font_install();
        pet_fonts();
        mem_put(TEXT_LIN, 0x5A);
        return mem_get(TEXT_LIN) ^ 0x5A;
    case GET_KIND: return cur_kind();
    case GET_IMPORT:
    case GET_MDIMPORT:
    case GET_RTFEND:
    case GET_HTMLEND: {
        u16 n = w == GET_IMPORT      ? doc_import()
                : w == GET_MDIMPORT ? mod_run(MOD_MD_IMPORT)
                : mod_run(w == GET_RTFEND ? MOD_RTF_END : MOD_HTML_END);
        refresh(1);
        return n;
    }
    }
    return 0;
}

/* find and replace: op FIND_... (SETF / SETR: msg as the string); 1 when
 * a match is selected (DEFAULT0.OVL says what it found, in msg) */
u16 wp_find(u8 op) {
    u16 r;
    ovl_fresh();
    if (op >= FIND_SETF) to_ascii(msg, FIND_MAX);
    else u_open = 0;
    r = edit_run(op);
    if (r == 0xFFFF) {
        say(ed_need);
        r = 0;
    }
    refresh(1);
    return r;
}

/* message n of the table */
const char *msg_get(u8 n) {
    const char *s = msg_tab;
    while (n--)
        while (*s++) {}
    return s;
}

void wp_say(u8 n) {
    u8 i;
    ovl_fresh();
    to_ascii(msg, 78);
    for (i = 0; (msg_parm[i] = msg[i]) != 0; i++) {}
    msg_fill(n, msg_parm);
    render_bars();
}

/* the menu's panel above the bottom bar: n rows (0 or PANEL_ROWS) of
 * MSG_MENU, ..., the bar's MSG_VIEW (with List on / off when LIST.OVL is
 * loaded); closed (0): msg emptied, the view drawn again under it */
void wp_panel(u8 n) {
    ovl_fresh();
    panel = n;
    if (n) msg_fill(MSG_VIEW, list_ok ? msg_get(MSG_LISTON + list_on) : "");
    else msg[0] = 0;
    render();
}

void wp_setname(void) {
    ovl_fresh();
    to_ascii(fname, 16);
    render_bars();
}

void wp_video(u8 on) {
    ovl_fresh();
    if (on) {
        video_on();
        render_invalidate();
        refresh(0);
    } else {
        video_off();
    }
}

/* RUN/STOP ignored while editing (on 1), or not (0): stopvec (cold.c) */
void wp_stopvec(u8 on) {
    ovl_fresh();
    stopvec(on);
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
