/* wp.h — MEGA65 Writer: the C half's shared definitions
 *
 * The document is two parallel arrays in bank 5: the text (ASCII, CR ends a
 * paragraph, FF a paragraph and its page) and one attribute byte per
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
 *         (80 x 50 x 2), $4A000 the fonts' glyphs (writer.fnt, BLOADed by
 *         BASIC), $4D000 the layout's line table
 * bank 5: $50000 the text, $53B00 the attributes, $57600 the scratch area
 *         (insertions' bounce buffer; the file image BSAVE writes), $5EC00
 *         the clipboard (text, then attributes) */
#define BITMAP_LIN  0x40000UL
#define SCREEN_LIN  0x48000UL
#define FONT_LIN    0x4A000UL
#define LINES_LIN   0x4D000UL
#define TEXT_LIN    0x50000UL
#define ATTR_LIN    0x53B00UL
#define SCRATCH_LIN 0x57600UL
#define CLIP_LIN    0x5EC00UL
#define COLOR_OFS   0x1000u     /* our cells' colours in colour RAM */

#define DOC_MAX   15000u        /* characters, the final CR included */
#define CLIP_MAX  2560u
#define MAX_LINES 1024u         /* 12 bytes each */

/* ---- characters and attributes ---- */
#define CH_CR 13
#define CH_FF 12
#define A_FONT  0x03
#define A_BOLD  0x04
#define A_ITAL  0x08
#define A_UNDER 0x10
#define A_STYLE 0x1F            /* what a character carries */
#define A_ALIGN 0x60            /* a paragraph's CR / FF: 0 left, 1 centre, 2 right, 3 justify */
#define A_ALIGN_SHIFT 5
#define NFONTS 4

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
#define G_PILCROW 127
#define G_BLOCK 128
struct font {
    u8 height, ascent;
    const u8 *w;                /* advance of each code FONT_FIRST.. */
    const u16 *o;               /* each glyph's first byte from bits */
    u32 bits;                   /* the glyphs (FONT_LIN + ...): rows of 1 byte (width <= 8) or 2 */
};
extern const struct font font_sans, font_serif, font_mono, font_head, font_raw;
extern const u8 font_raw_rows[];        /* the raw font's glyphs (13 rows each) in bank 0 */
extern const struct font *const fonts[NFONTS];
extern const char *const font_names[NFONTS];

/* ---- the line table ---- */
struct line {
    u16 start;                  /* first character */
    u16 y;                      /* top, in document pixels (page view) or raw rows x 16 */
    u8 h, asc;                  /* height, baseline from the top */
    u16 x;                      /* first character's x from the text's left */
    u8 extra, nsp;              /* justify: pixels spread over nsp spaces */
    u8 page, pad;
};

/* ---- platform (m65.c / tests/host.c) ---- */
u8 mem_get(u32 a);
void mem_put(u32 a, u8 v);
void mem_copy(u32 dst, u32 src, u16 n);        /* overlapping: dst < src only */
void mem_fill(u32 dst, u16 n, u8 v);
void mem_read(u8 *dst, u32 src, u16 n);        /* far memory to a bank 0 buffer */
void blit_row(u16 sy, const u8 *src);           /* 80 bytes to screen row sy */
void color_fill(u8 row, u8 col, u8 n, u8 c);    /* cells' foreground colour */
void color_row(u8 row, const u8 *c);            /* a row's 80 colours */
void video_on(void);
void video_off(void);

#ifdef __cc45__
#define TXT ((u8 __far *)TEXT_LIN)
#define ATR ((u8 __far *)ATTR_LIN)
#define LINES ((struct line __far *)LINES_LIN)
#else
extern u8 MEM[0x60000];
#define TXT (MEM + TEXT_LIN)
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
void doc_toggle(u8 bit);
void doc_cycle_font(void);
void doc_cycle_align(void);
void doc_copy(void);
void doc_cut(void);
void doc_paste(void);
u16 doc_pack(void);             /* the file image at SCRATCH_LIN; its size */
u8 doc_unpack(void);            /* 0: loaded */
u16 doc_export(void);           /* the PETSCII text at SCRATCH_LIN; its size */
u16 doc_import(void);           /* PETSCII text at SCRATCH_LIN as the document */
u16 para_end(u16 i);            /* the CR / FF ending the paragraph of i */
u8 attr_at_cursor(void);
u16 sel_lo(void);
u16 sel_hi(void);
void doc_load_demo(void);       /* (tools/welcome.c: the host only) */

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
u16 x_of(u16 l, u16 pos);       /* pos's x in line l (pixels from the text's left, or cells) */
u16 pos_at_x(u16 l, u16 x);
u8 char_w(u8 c, u8 a);          /* a character's advance, page view */
u8 raw_tag(u16 i, char *out);
u8 prev_style(u16 i);           /* the style of the last character shown before i */   /* raw view: the tags before character i; their length */
void scroll_to_cursor(void);
u16 doc_height(void);

/* ---- drawing (render.c) ---- */
extern char msg[81];            /* the bottom bar's message ("": the status) */
extern char fname[17];          /* the document's name */
void render(void);
void render_bars(void);
void render_invalidate(void);   /* the colours rewritten next time */
u8 petscii_to_ascii(u8 c);

#endif
