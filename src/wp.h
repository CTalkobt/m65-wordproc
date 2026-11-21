/* wp.h — MEGA65 Writer: the C half's shared definitions
 *
 * The document is two parallel arrays in bank 5: the text (Latin-1: ASCII
 * and $A0-$FF; CR ends a paragraph, FF a paragraph and its page) and one attribute byte per
 * character (font, bold, italic, underline; a paragraph's alignment on its
 * CR / FF). The document always ends with a CR the user cannot delete.
 *
 * The screen is 640 x 400, one bit a pixel: an 80 x 50 VIC-IV text screen of
 * 16-bit characters, every cell its own character, so the 4000 characters'
 * 8-byte glyphs (bank 4) are a bitmap laid out as the C64's (8 x 8 tiles
 * along each row). Each cell has its own foreground colour; the background
 * is white.
 *
 * The C files are plain C99 so they also build on the host (tests/): the
 * memory of banks 4 and 5 is then an array (MEM), the colour RAM another.
 */
#ifndef WP_H
#define WP_H

#ifdef __cc45__
#pragma encoding ascii
#define FAR __far
#else
#define FAR
#endif

#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

/* ---- memory (28-bit linear addresses) ----
 * bank 4: $40000 the bitmap (4000 glyphs x 8 bytes), $48000 the screen
 *         (80 x 50 x 2), $4A000 writer.fnt's first part (font_install)
 * bank 5: $50000 the scratch area (insertions' bounce buffer; the file
 *         image BSAVE writes; undo's buffer at its end), $5EC00 the
 *         clipboard (text, then attributes)
 * attic RAM (the MEGA65's 8 MB HyperRAM at $8000000), 64 KB each: the
 *         text and the attributes, each with the gap; the layout's line
 *         table; the line table's bounce buffer (the host: from $60000) */
#define BITMAP_LIN  0x40000UL
#define SCREEN_LIN  0x48000UL
#define FONT_LIN    0x4A000UL
#ifdef __cc45__
#define TEXT_LIN    0x8000000UL
#define ATTR_LIN    0x8010000UL
#define LINES_LIN   0x8020000UL
#define LBOUNCE_LIN 0x8030000UL
#else
#define TEXT_LIN    0x60000UL
#define ATTR_LIN    0x70000UL
#define LINES_LIN   0x80000UL
#define LBOUNCE_LIN 0x90000UL
#endif
#define AO          (ATTR_LIN - TEXT_LIN)
/* the gap kept in the document's store at the last edit (doc.c): opened
 * GAP_SPAN at a time (can be changed: the store holds DOC_MAX + GAP_SPAN) */
#define GAP_SPAN    2048u
#define SCRATCH_LIN 0x50000UL
#define CLIP_LIN    0x5EC00UL
#define SCRATCH_MAX 60416u      /* the scratch area's size: SCRATCH_LIN to CLIP_LIN */
#define COLOR_OFS   0x1000u     /* our cells' colours in colour RAM ($FF81000-$FF82F3F) */
/* the Latin-1 glyphs and the compose table: colour RAM's last 20 KB, which
 * every MEGA65 has and nothing else uses (the host: bank 3 of MEM) */
#ifdef __cc45__
#define HI_LIN      0xFF83000UL
#else
#define HI_LIN      0x30000UL
#endif

/* The rest of colour RAM, after writer.fnt's second part (12306 bytes
 * from HI_LIN; colour RAM ends at HI_LIN + $5000): the find and replace
 * strings (FIND_MAX characters each), the drawing's caches while DEFAULT0.OVL
 * runs (CACHE_LIN), DEFAULT0.OVL (EDIT_LIN, EDIT_SIZE bytes: edit.c).
 * Undo's buffer (UNDO_MAX characters: text, then attributes) is the
 * scratch area's last part, past where open_gap's copies go (the
 * document's length) and the middle undo replaces waits (PARK_LIN): a file
 * image or an export that reaches it (UNDO_OFS) leaves nothing to undo. */
#define FIND_MAX  40
#define FIND_LIN  (HI_LIN + 0x3020)
#define REPL_LIN  (FIND_LIN + FIND_MAX + 1)
#define CACHE_LIN (HI_LIN + 0x3080)
#define EDIT_LIN  (HI_LIN + 0x3680)
#define UNDO_MAX  7500u
#define PARK_LIN  (SCRATCH_LIN + DOC_MAX)       /* (its 2 * UNDO_MAX before UNDO_LIN) */
#define UNDO_OFS  (SCRATCH_MAX - 2 * UNDO_MAX)
#define UNDO_LIN  (SCRATCH_LIN + UNDO_OFS)

#define DOC_MAX   30000u        /* characters, the final CR included */
#define CLIP_MAX  2560u
#define MAX_LINES 4096u         /* 12 bytes each */

/* ---- characters and attributes ---- */
#define CH_CR 13
#define CH_FF 12
#define A_FONT  0x03
#define A_BOLD  0x04
#define A_ITAL  0x08
#define A_UNDER 0x10
#define A_STYLE 0x1F            /* what a character carries */
/* a paragraph's CR / FF carries the paragraph's properties instead: */
#define A_KIND  0x07            /* its kind (style): K_... */
#define A_LEVEL 0x18            /* a list item's level, 0-3 (others: 0) */
#define A_LEVEL_SHIFT 3
#define A_ALIGN 0x60            /* 0 left, 1 centre, 2 right, 3 justify */
#define A_ALIGN_SHIFT 5
#define A_PARA (A_KIND | A_LEVEL | A_ALIGN)
#define NFONTS 4
#define F_SANS 0
#define F_SERIF 1
#define F_MONO 2
#define F_HEAD 3

/* paragraph kinds (issue #4): what the paragraph is, which the converters
 * carry (Markdown #, >, ```; RTF \outlinelevel, \s), and how it is laid
 * out: a quote and a code block have an indent (a quote a bar in it, a code
 * block a dotted line). Applied as a style (doc_style) its characters take
 * kind_attr's font and style. A list item (bulleted or numbered) has a
 * level too, and is indented by it; its bullet or number is drawn, not
 * text: a number counts the items before it at its level (item_num). */
enum { K_NORMAL, K_H1, K_H2, K_H3, K_QUOTE, K_CODE, K_BULLET, K_NUMBER, NKINDS };
#define KIND_TAB NKINDS
extern const u8 kind_attr[KIND_TAB];    /* (layout.c) the characters' style */
extern const char *const kind_names[KIND_TAB];

/* ---- screen geometry ---- */
#define SCR_W 640
#define SCR_H 400
#define COLS 80
#define ROWS 50
#define BAR_H 16
#define VIEW_TOP 16             /* the document view: screen rows 16..383 */
#define VIEW_H 368
#define BAND_H 24               /* the tallest line drawn in one band (Head: 23) */

/* page layout: a page 544 x 704 (8.5 x 11 at 64 pixels an inch) */
#define PAGE_X 48
#define PAGE_W 544
#define PAGE_H 704
#define PAGE_GAP 16             /* desk between pages (and above the first) */
#define PAGE_STRIDE (PAGE_H + PAGE_GAP)
#define MARGIN_X 56
#define MARGIN_TOP 48
#define MARGIN_BOT 48
#define TEXT_X (PAGE_X + MARGIN_X)
#define TEXT_W (PAGE_W - 2 * MARGIN_X)
#define TEXT_H (PAGE_H - MARGIN_TOP - MARGIN_BOT)

/* raw view: 80 columns of 8 x 16 cells */
#define RAW_H 16

/* colours (the default palette) */
#define C_BLACK 0
#define C_WHITE 1
#define C_RED 2
#define C_PURPLE 4
#define C_GREEN 5
#define C_BLUE 6
#define C_DGREY 11
#define C_GREY 12
#define C_LBLUE 14

enum { VIEW_PAGE = 0, VIEW_RAW = 1 };

/* ---- fonts (fonts.c, made by tools/mkfonts.py) ---- */
#define FONT_FIRST 32
#define FONT_LAST 128
#define FONT_HI 0xA0            /* Latin-1 $A0-$FF (issue #1); 129-159 are drawn as "?" */
#define G_PILCROW 127
#define G_BLOCK 128
struct font {
    u8 height, ascent;
    u32 offs;                   /* each code's record, 32-255 (FONT_LIN / HI_LIN + ...): its advance, its glyph's first byte from bits (2) */
    u32 bits;                   /* the glyphs: rows of 1 byte (width <= 8) or 2 */
};
extern const struct font font_sans, font_serif, font_mono, font_head;
extern const u32 font_raw_at;           /* the raw font's glyphs (8 x 13, 13 bytes each, codes 32-255) */
extern const u32 compose_at;            /* the compose table: 2 characters (both orders), a code; 0 at the end */
extern const u16 font_lo_size, font_hi_size;    /* writer.fnt's parts, for FONT_LIN and HI_LIN */
void font_install(void);                /* writer.fnt, BLOADed to SCRATCH_LIN, to FONT_LIN and HI_LIN */
u32 raw_at(u8 c);                       /* the raw font's glyph of c */
#define NO_GLYPH(c) ((c) < FONT_FIRST || ((c) > FONT_LAST && (c) < FONT_HI))   /* drawn as "?" */
extern const struct font *const fonts[NFONTS];
extern const char *const font_names[NFONTS];

/* ---- the line table ---- */
struct line {
    u16 start;                  /* first character */
    u16 y;                      /* top, in document pixels (page view) or raw rows x 16 */
    u8 h, asc;                  /* height, baseline from the top */
    u16 x;                      /* first character's x from the text's left */
    u8 extra, nsp;              /* justify: pixels spread over nsp spaces */
    u8 page, kind;              /* (page view) its paragraph's kind and level (A_KIND | A_LEVEL), L_FIRST */
#define L_FIRST 0x80            /* (in kind) the paragraph's first line */
};

/* ---- platform (m65.c / tests/host.c) ---- */
u8 mem_get(u32 a);
void mem_put(u32 a, u8 v);
void mem_copy(u32 dst, u32 src, u16 n);        /* overlapping: dst < src only */
void mem_fill(u32 dst, u16 n, u8 v);
void mem_read(u8 *dst, u32 src, u16 n);        /* far memory to a bank 0 buffer */
void mem_write(u32 dst, const u8 *src, u16 n); /* a bank 0 buffer to far memory */
void blit_row(u16 sy, const u8 *src, u8 n);     /* n bytes to screen row sy's first n cells */
void color_fill(u8 row, u8 col, u8 n, u8 c);    /* cells' foreground colour */
void color_row(u8 row, const u8 *c);            /* a row's 80 colours */
void video_on(void);
void video_off(void);
/* the keyboard, read directly (the KERNAL gives no key for CTRL with a
 * cursor key, or not always) */
#define KM_SHIFT 0x03               /* kbd_mods(): either SHIFT */
#define KM_CTRL 0x04
u8 kbd_mods(void);                  /* the modifier keys held now ($D611) */
u8 kbd_ctrl_crsr(void);             /* CTRL + a cursor key held: its code (29 157 17 145), else 0 */
u8 frames(void);                    /* video frames counted (wraps) */

#ifdef __cc45__
#define TXT ((u8 __far *)TEXT_LIN)
#define FARP(a) ((u8 __far *)(a))
#define ATR ((u8 __far *)ATTR_LIN)
#define LINES ((struct line __far *)LINES_LIN)
#else
extern u8 MEM[0xA0000];
#define TXT (MEM + TEXT_LIN)
#define FARP(a) (MEM + (a))
#define ATR (MEM + ATTR_LIN)
#define LINES ((struct line *)(MEM + LINES_LIN))
#endif

/* ---- the editor's state (doc.c) ---- */
extern u16 doc_len;             /* characters, the final CR included */
extern u16 cur;                 /* the cursor: before character cur */
extern u16 sel_anchor;          /* the selection's other end */
extern u8 sel_on;
extern u8 typing_attr;          /* the style a typed character gets */
extern u8 dirty;
extern u8 view_mode;
extern u16 nlines;
extern u8 npages;
extern u16 view_y;              /* the document y at the view's top */
extern u16 goal_x;              /* up / down keep to this x */
extern u8 goal_valid;

void doc_new(void);
u8 doc_insert(u16 at, const u8 *s, const u8 *a, u16 n);
void doc_delete(u16 from, u16 to);
u8 doc_type(u8 c);              /* ASCII at the cursor (replacing a selection) */
void doc_backspace(void);
void doc_delete_fwd(void);
void doc_copy(void);
void doc_cut(void);
void doc_paste(void);
u16 para_start(u16 i);          /* the start of the paragraph holding i */
void open_gap(u16 at, u16 n);   /* the text from at moved up n (doc_len not changed) */
u8 make_room(u16 at, u16 n);    /* room for n characters at at (noted for undo); 1: none */
void room(u16 at, u16 n);       /* the same, not noted, nor checked */
void del_raw(u16 from, u16 to); /* from..to-1 removed, not noted (from < to < doc_len) */
extern u16 g_at, g_len;         /* the gap in the store (doc.c) */
void gap_close(void);           /* the store's text without it */
void move2(u16 d, u16 s, u16 n);        /* the store's s..s+n-1 to d (doc.c; gapmove.s45) */
void gap_to(u16 p);             /* the gap moved to before p (the same) */
void doc_out(u16 d, u16 o, u16 i, u16 n);       /* n characters from i to SCRATCH_LIN + d (attributes to + o) */

/* ---- undo, find and replace: DEFAULT0.OVL (edit.c), run by edit_run (doc.c) ---- */
#define FIND_STOP 0             /* (mod_cmd) replacing ends (BASIC: a key not Y / N / A) */
#define FIND_REPL 1             /* the match selected replaced, then the next (Y) */
#define FIND_FWD 2              /* the next match selected (N; SHIFT+CTRL+F) */
#define FIND_ALL 3              /* all replaced (A): how many, said */
#define FIND_BACK 4             /* the match before (SHIFT+CTRL+B) */
#define FIND_SETF 5             /* msg (ASCII) as the find string */
#define FIND_SETR 6             /* msg as the replacement: replacing starts */
#define ED_NOTE 7               /* ed_a..ed_b-1 about to change (undo_note) */
#define ED_UNDO 8               /* the last edit undone (again: done); 1: none */
#define ED_TOGGLE 9             /* doc_toggle(ed_a) ... */
#define ED_FONT 10
#define ED_ALIGN 11
#define ED_LEVEL 12
#define ED_STYLE 13
extern u16 u_pos, u_tail, u_n, ed_a, ed_b;
extern u8 u_have, u_open, f_len, r_len, r_on, ed_ok, ed_busy;
u16 edit_run(u8 cmd);           /* DEFAULT0.OVL's mod_cmd; its mod_ret (0xFFFF: not loaded) */
void edit_keep(void);           /* DEFAULT0.OVL, BLOADed to mod_area, kept in colour RAM */
void undo_note(u16 from, u16 to);
u16 ed(u8 cmd, u16 a);          /* edit_run(cmd) with ed_a = a */
/* the styles, a list item's level: DEFAULT0.OVL's */
#define doc_toggle(bit) ed(ED_TOGGLE, bit)              /* bold / italic / underline (A_...) */
#define doc_cycle_font() ed(ED_FONT, 0)
#define doc_cycle_align() ed(ED_ALIGN, 0)
#define doc_list_level(in) (ed(ED_LEVEL, in) == 1)     /* TAB / DEL, RETURN at a list item's start; 1: done */
#define doc_style(k) ed(ED_STYLE, k)                    /* paragraph style k (K_...) */
void edit_main(void);           /* DEFAULT0.OVL's entry (edit.c) */
void mod_entry(void);           /* (modcall.s45: a jump to the module in mod_area) */
u16 doc_pack(void);             /* the file image at SCRATCH_LIN; its size */
u8 doc_unpack(void);            /* 0: loaded */
u16 doc_export(void);           /* the PETSCII text at SCRATCH_LIN; its size */
u16 doc_import(void);           /* PETSCII text at SCRATCH_LIN as the document */
void doc_loaded(u8 d);          /* after a load / import: the final CR, the cursor at the start; d: changed */
u16 md_export(void);            /* Markdown at SCRATCH_LIN; its size (0: too long) */
u16 md_import(void);            /* Markdown at SCRATCH_LIN as the document; its length */

/* ---- what the converters share (conv.c; issue #3) ---- */
u8 txt_at(u16 i);               /* TXT[i], ATR[i]: smaller code than the far access */
u8 atr_at(u16 i);
void txt_put(u16 i, u8 c);      /* TXT[i] = c, ATR[i] = a */
void atr_put(u16 i, u8 a);
/* output: bytes to SCRATCH_LIN (o_n of them; past SCRATCH_MAX counted, not written) */
extern u16 o_n;
void o_start(void);
void o_put(u8 c);
void o_puts(const char *s);
u16 o_end(void);                /* the size written; 0: more than fits */
/* export: the document walked, an event at a time (walk_next):
 *   EV_PARA  a paragraph: its text w_pos..w_to-1, w_to its CR / FF, w_attr
 *            that one's attribute (its alignment)
 *   EV_RUN   characters w_pos..w_to-1 of one style, w_attr (A_STYLE)
 *   EV_PEND  the paragraph's end at w_pos: w_attr CH_CR, or CH_FF (a page)
 *   EV_END   the document's end */
enum { EV_END, EV_PARA, EV_RUN, EV_PEND };
extern u16 w_pos, w_to;
extern u8 w_attr;
void walk_start(void);
u8 walk_next(void);
/* import: the document built from the start (b_n characters so far), each
 * character with the attribute b_attr (a paragraph's CR / FF: its
 * alignment); build_end: the document's length, set, and it laid out */
extern u16 b_n;
extern u8 b_attr, b_done;
void build_start(void);
void build_put(u8 c);
void build_uni(u16 u);          /* a Unicode character as the document's (Latin-1, or ASCII for some) */
u8 as_ascii(u8 c);              /* a document character as ASCII (accents left out, else '?') */
u16 build_end(void);            /* the final CR, doc_len, b_done; the length without the final CR */
void doc_built(void);           /* after build_end (b_done): doc_loaded(1) (mod_run calls it) */

/* ---- modules (.OVL files, loaded by BASIC into mod_area: render.c) ----
 * A module is built apart (Makefile: linked at mod_area against the
 * program's map) and is at most MOD_SIZE bytes, code, data and variables. */
#define MOD_SIZE 6784            /* (MARKDOWN.OVL: 6710, the largest module) */
#define MOD_MORE 0x80                   /* (in a command: more calls follow, the module stays) */
#define MOD_MD_EXPORT 0                 /* MARKDOWN.OVL: md_export */
#define MOD_MD_IMPORT 1                 /* MARKDOWN.OVL: md_import */
#define MOD_RTF_EXPORT 2                /* RTF.OVL: the document as RTF at SCRATCH_LIN */
#define MOD_RTF_BEGIN (MOD_MORE | 3)    /* RTF.OVL: an import starts */
#define MOD_RTF_CHUNK (MOD_MORE | 4)    /* RTF.OVL: the next 1 KB of the file, at SCRATCH_LIN */
#define MOD_RTF_END 5                   /* RTF.OVL: the import's end: the document's length */
extern u8 mod_cmd;                      /* what the module is to do */
extern u16 mod_ret;                     /* its result */
u8 mod_ok(const u8 *m);                 /* a module's header there, of this build */
u16 mod_run(u8 cmd);                    /* run the module loaded; 0xFFFF: none */
void md_main(void);                     /* MARKDOWN.OVL's entry (md.c) */
void rtf_main(void);                    /* RTF.OVL's entry (rtf.c) */
/* LIST.OVL (list.c), list mode: resident, it draws its markers over each
 * line drawn while list_on (render.c calls list_main with hk_l, hk_L: the
 * line). It is loaded by BASIC at the start into mod_area's last LIST_SIZE
 * bytes, past the drawing's buffers (mod_area.r: at most LIST_OFS bytes),
 * so it costs no memory of its own; a module run in mod_area (Markdown)
 * overwrites it, so list_ok is then 0 until BASIC has loaded it again. */
#define LIST_OFS 4288           /* (wp.bas's too) */
#define LIST_SIZE (MOD_SIZE - LIST_OFS)
extern u8 list_ok, list_on;             /* LIST.OVL loaded (and from this build); asked for */
extern u16 hk_l;                        /* the line drawn (raw view: >= nlines, a row past the end) */
extern const struct line *hk_L;
u8 list_check(void);                    /* list_ok, from LIST.OVL's header (and then drawn) */
void list_main(void);                   /* LIST.OVL's entry (list.c) */
u16 para_end(u16 i);            /* the CR / FF ending the paragraph of i */
u8 para_al(u16 i);              /* its alignment (0 left, 1 centre, 2 right, 3 justify) */
u8 para_kind(u16 i);            /* its kind (K_...) */
u8 cur_kind(void);              /* the kind of the cursor's paragraph (the selection's first) */
u16 item_num(u16 p);            /* paragraph p's number in its list (a numbered item) */
u8 para_indent(u8 kl);          /* (layout.c) a paragraph's indent: its kind and level (A_KIND | A_LEVEL) */
u8 attr_at_cursor(void);
u16 sel_lo(void);
u16 sel_hi(void);
void doc_load_help(void);       /* (tools/helpdoc.c: the host only) */

/* ---- layout (layout.c) ---- */
extern u8 layout_done;          /* all lines laid out (else 0..nlines-1) */
void layout(void);              /* all of it, now */
void layout_all(void);          /* all of it again, from the next layout_update */
void layout_edit(u16 lo, u16 hi, int16_t delta);
void layout_update(void);       /* the edits laid out (as far as the screen needs) */
u8 layout_idle(void);
void ensure_pos(u16 p);
void ensure_y(u16 y);
u16 line_of(u16 pos);           /* the line holding pos */
u16 line_end(u16 l);            /* its end (the next line's start) */
void get_line(u16 l, struct line *L);   /* line l of the table (in bank 4) */
u16 line_start(u16 l);          /* its first character */
struct just {                   /* walking along a line: its justification */
    u8 extra, nsp, k;
};
void just_init(struct just *j, const struct line *L);
u8 just_w(u8 c, u8 a, struct just *j);  /* c's width, with a space's share of the justification */
u16 x_of(u16 l, u16 pos);       /* pos's x in line l (pixels from the text's left, or cells) */
u16 pos_at_x(u16 l, u16 x);
u8 char_w(u8 c, u8 a);          /* a character's advance, page view */
u8 raw_tag(u16 i, char *out);
u8 prev_style(u16 i);           /* the style of the last character shown before i */   /* raw view: the tags before character i; their length */
void scroll_to_cursor(void);
u16 doc_height(void);

/* ---- drawing (render.c) ---- */
/* the menu's panel: panel rows (0 or 2) above the bottom bar (MSG_MENU, ...) */
#define PANEL_ROWS 2

/* What was drawn where: a segment of the view (a line's band, blank page,
 * desk) starting at a screen row, with a hash of what it shows. */
#define MAXSEG 40               /* (a page view frame has at most ~31; more: drawn again) */
struct seg {
    u32 hash;
    u16 sy, n;
    u8 kind, r0;
};
/* The drawing's buffers and caches, all remade after a full redraw (the
 * rows by init_rows, the rest drawn or marked unknown by render_invalidate),
 * share their memory with the module region: a module (an .OVL file run
 * once: Markdown, ...) is loaded there by BASIC, run by mod_run, and the
 * screen then drawn again. */
#define TMP_LINES 32
/* mod_area's first part, its buffers in use only while drawing (a struct
 * line is 12 bytes); DEFAULT0.OVL's room, up to LIST.OVL */
#define MOD_SCRATCH (TMP_LINES * 12 + BAND_H * COLS + COLS + 2 + 48 + COLS + 48 + COLS + 1)
#define EDIT_SIZE LIST_OFS
union mod_area {
    struct {
        /* (in use only while the screen is drawn or laid out: DEFAULT0.OVL
         * runs here, EDIT_SIZE bytes) */
        struct line lay_tmp[TMP_LINES]; /* layout_update's new lines */
        u8 band[BAND_H * COLS];
        char bar_t[COLS + 2];          /* a bar's new text */
        char tag_buf[48];              /* raw_tag's tags, when not wanted */
        u8 cellcol[COLS];              /* raw view: the row's cells' colours */
        u8 gbuf[48];                   /* a glyph's rows, copied from bank 4 */
        char sb[COLS + 1];             /* a bar's text */
        /* (kept from one frame to the next) */
        struct seg segs[2][MAXSEG];    /* this frame's and the last one's, in screen order */
        char bar_text[4][COLS + 2];    /* what each bar shows ("" : unknown) */
        u8 row_desk[COLS];             /* the desk between pages */
        u8 row_page[COLS];             /* a page's blank row: desk at the sides */
        u8 row_blank[COLS];
        u8 rowcol[ROWS];               /* each cell row's colour scheme (0xFF: unknown) */
        u8 desk_row[ROWS];
    } r;
    u8 m[MOD_SIZE];
};
extern union mod_area mod_area;
extern char msg[81];            /* the bottom bar's message ("": the status) */
extern u8 panel;
void band_xor(u16 x0, u16 x1, u8 r0, u8 r1);    /* the band's pixels x0..x1-1, rows r0..r1-1, inverted */
extern char fname[17];          /* the document's name ("": untitled) */
const char *msg_get(u8 n);      /* message n of the table (WRITER.MSG: api.c) */
void render(void);
void render_bars(void);
void render_invalidate(void);   /* the colours rewritten next time */
void wp_drop(u8 k);             /* the style drop-down under the top bar, k lit (0xFF: closed) */
u8 petscii_to_ascii(u8 c);

#endif
