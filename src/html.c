/* html.c — HTML export (UTF-8): the module HTML.OVL (issue #4)
 *
 * The document to SCRATCH_LIN (html_export) as one page: a head with the
 * document's name as its title and a style sheet that keeps the spaces
 * (white-space: pre-wrap) and gives the kinds the Writer's fonts, then
 *   a paragraph             <p>; paragraphs that follow each other with no
 *                           empty one between (and the same alignment) are
 *                           one, with <br> between (as Markdown's export)
 *   paragraph kinds         heading 1-3: <h1>-<h3>; quote: <blockquote>
 *                           (with <br> between, as above); code: <pre><code>,
 *                           a line a paragraph; list items: <li> in <ul> or
 *                           <ol> (start="N" when it starts past 1), nested
 *                           by their level inside the item before
 *   bold, italic, underline <b>, <i>, <u> (where they add to the paragraph
 *                           kind's own style: a heading's bold is not
 *                           written)
 *   another font in part    Mono <code>, Serif, Sans and Head a <span> (the
 *                           style sheet's classes r, s, h)
 *   centre, right, justify  style="text-align: ..." on the paragraph
 *   page break              <div style="page-break-after: always"></div>
 * & < > are entities; Latin-1 characters are UTF-8. Empty paragraphs
 * separate blocks and are not written (in a code block: an empty line).
 */
#include "wp.h"

#define out o_put
#define outs o_puts

/* a character: & < > as entities, Latin-1 as UTF-8 (two bytes from $80) */
static void out_ch(u8 c) {
    if (c == '&') outs("&amp;");
    else if (c == '<') outs("&lt;");
    else if (c == '>') outs("&gt;");
    else {
        if (c >= 0x80) {
            out((u8)(0xC0 | (c >> 6)));
            c = (u8)(0x80 | (c & 0x3F));
        }
        out(c);
    }
}

static void out_num(u16 v) {
    char t[5];
    u8 k = 0;
    do {
        t[k++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (k) out((u8)t[--k]);
}

/* ---- the inline markup: a character's style as markers open, outermost
 * first: its font (H_FONT and the font in bits 5-6, when it is not the
 * paragraph kind's own), underline, bold, italic ---- */
#define H_FONT 0x80
#define H_FMASK 0xE0
static const u8 mark_mask[4] = { H_FMASK, A_UNDER, A_BOLD, A_ITAL };
static const char *const mark_open[4] = { 0, "<u>", "<b>", "<i>" };
static const char *const mark_close[4] = { 0, "</u>", "</b>", "</i>" };
static const char *const font_open[4] = { "<span class=\"s\">", "<span class=\"r\">", "<code>", "<span class=\"h\">" };
static u8 base;                 /* the paragraph kind's own style (kind_attr) */
static u8 open_st;

static u8 h_style(u16 i) {
    u8 a = atr_at(i), s = a & (A_UNDER | A_BOLD | A_ITAL) & ~base, f = a & A_FONT;
    if (f != (base & A_FONT)) s |= (u8)(H_FONT | (f << 5));
    return s;
}

/* from the markers open to style t: the inner ones closed back to the
 * first that changes, then t's opened from there */
static void restyle_to(u8 t) {
    u8 k = 0, m;
    while (k < 4 && ((open_st ^ t) & mark_mask[k]) == 0) k++;
    if (k == 4) return;
    for (m = 4; m-- > k;) {
        if (!(open_st & mark_mask[m])) continue;
        if (m) outs(mark_close[m]);
        else outs(((open_st >> 5) & A_FONT) == F_MONO ? "</code>" : "</span>");
    }
    for (m = k; m < 4; m++) {
        if (!(t & mark_mask[m])) continue;
        outs(m ? mark_open[m] : font_open[(t >> 5) & A_FONT]);
    }
    open_st = t;
}

/* the text of paragraph ps..pe-1; code: as it is (in <pre><code>) */
static void out_para(u16 ps, u16 pe, u8 code) {
    open_st = 0;
    for (; ps < pe; ps++) {
        if (!code) restyle_to(h_style(ps));
        out_ch(txt_at(ps));
    }
    restyle_to(0);
}

/* ---- blocks ---- */
#define P_NONE 0xFF             /* (prev: no block yet; a page break) */
#define P_BREAK 0xFE
static u8 prev, pal;            /* the last block's kind; its alignment */
static u8 ld, lt[4];            /* lists open; each one's kind (K_BULLET / K_NUMBER) */

static const char *const align_name[4] = { 0, "center", "right", "justify" };

/* the innermost list (and its item) closed */
static void list_close(void) {
    outs("</li>\n");
    outs(lt[--ld] == K_BULLET ? "</ul>" : "</ol>");
}

/* a list item of kind k at level lv (paragraph ps): the lists past its
 * level closed (and its own, when of the other kind), then the item; the
 * lists up to its level opened (one inside the item before) */
static void list_item(u8 k, u8 lv, u16 ps) {
    u16 n;
    while (ld > lv + 1 || (ld == lv + 1 && lt[lv] != k)) list_close();
    if (ld == lv + 1) {
        outs("</li>\n<li>");
        return;
    }
    while (ld <= lv) {
        u8 t = ld == lv ? k : K_BULLET;
        lt[ld++] = t;
        if (t == K_BULLET) {
            outs("<ul>\n<li>");
        } else {
            outs("<ol");
            n = item_num(ps);
            if (n != 1) {
                outs(" start=\"");
                out_num(n);
                out('"');
            }
            outs(">\n<li>");
        }
    }
}

/* the last block closed */
static void end_block(void) {
    if (prev == K_NORMAL) outs("</p>");
    else if ((u8)(prev - K_H1) < 3) {
        outs("</h");
        out((u8)('0' + prev));
        out('>');
    } else if (prev == K_QUOTE) outs("</blockquote>");
    else if (prev == K_CODE) outs("</code></pre>");
    else if (prev != P_NONE && prev != P_BREAK) {
        while (ld) list_close();
    }
    if (prev != P_NONE && prev != P_BREAK) out('\n');
}

/* a block's start tag: <name, its alignment, > */
static void start_tag(const char *name, u8 al) {
    out('<');
    outs(name);
    if (al) {
        outs(" style=\"text-align: ");
        outs(align_name[al]);
        out('"');
    }
    out('>');
}

static const char head[] =
    "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n<title>";
static const char style[] =
    "</title>\n<style>\n"
    "body { font-family: sans-serif; }\n"
    "p, h1, h2, h3, blockquote, li { white-space: pre-wrap; }\n"
    "h3, .r { font-family: serif; }\n"
    "blockquote { font-family: serif; font-style: italic; }\n"
    ".s { font-family: sans-serif; }\n"
    ".h { font-size: larger; font-weight: bold; }\n"
    "</style>\n</head>\n<body>\n";

/* the document as HTML at SCRATCH_LIN; its size (0: more than fits) */
u16 html_export(void) {
    u16 ps, pe;
    u8 ev, kind, al, gap = 0;
    const char *t;
    o_start();
    outs(head);
    for (t = fname[0] ? fname : "untitled"; *t; t++) out_ch((u8)*t);
    outs(style);
    walk_start();
    prev = P_NONE;
    pal = 0;
    ld = 0;
    while ((ev = walk_next()) != EV_END) {
        if (ev == EV_PEND && w_attr == CH_FF) {
            end_block();
            outs("<div style=\"page-break-after: always\"></div>\n");
            prev = P_BREAK;
            gap = 1;
        }
        if (ev != EV_PARA) continue;
        ps = w_pos;
        pe = w_to;
        kind = w_attr & A_KIND;
        al = (u8)((w_attr & A_ALIGN) >> A_ALIGN_SHIFT);
        if (kind >= K_CODE) al = 0;
        /* (an empty paragraph: a gap, but an empty line in a code block) */
        if (pe == ps && (kind != K_CODE || prev != K_CODE || gap)) {
            gap = 1;
            continue;
        }
        if (!gap && kind == prev && kind == K_CODE) {
            out('\n');
        } else if (!gap && kind >= K_BULLET && (u8)(prev - K_BULLET) < 2) {
            list_item(kind, (u8)((w_attr & A_LEVEL) >> A_LEVEL_SHIFT), ps);
        } else if (!gap && kind == prev && (kind == K_NORMAL || kind == K_QUOTE) && al == pal) {
            outs("<br>");
        } else {
            end_block();
            if (kind == K_NORMAL) start_tag("p", al);
            else if ((u8)(kind - K_H1) < 3) {
                char h[3];
                h[0] = 'h';
                h[1] = (char)('0' + kind);
                h[2] = 0;
                start_tag(h, al);
            } else if (kind == K_QUOTE) start_tag("blockquote", al);
            else if (kind == K_CODE) outs("<pre><code>");
            else list_item(kind, (u8)((w_attr & A_LEVEL) >> A_LEVEL_SHIFT), ps);
            pal = al;
        }
        base = kind_attr[kind];
        out_para(ps, pe, kind == K_CODE);
        prev = kind;
        gap = 0;
    }
    end_block();
    outs("</body>\n</html>\n");
    return o_end();
}

/* HTML.OVL's entry (mod_run) */
void html_main(void) {
    mod_ret = html_export();
}
