/* md.c — Markdown export (UTF-8 text, LF line ends): the module
 * MARKDOWN.OVL (the import is MDIN.OVL, mdin.c)
 *
 * Export (md_export: the document to SCRATCH_LIN):
 *   a paragraph             a line; paragraphs that follow each other with
 *                           no empty one between (and the same alignment)
 *                           are one Markdown paragraph with hard breaks
 *                           ("\" at the line's end); others are separate
 *                           blocks (a blank line between)
 *   paragraph kinds         heading 1-3: "#", "##", "###"; quote: "> "
 *                           (quote paragraphs that follow each other: one
 *                           quote, with hard breaks); code: a fenced code
 *                           block (``` ... ```), a line a paragraph;
 *                           a PETSCII line: one fenced ```petscii, its
 *                           characters as Unicode (petuni.h)
 *                           list items: "- " or "N. " (the item's
 *                           number), four spaces before it a level
 *   bold, italic            **...**, *...* (where they differ from the
 *                           paragraph kind's own style: a heading's bold is
 *                           not written)
 *   underline               <u>...</u>
 *   strikethrough           ~~...~~
 *   superscript, subscript  <sup>...</sup>, <sub>...</sub>
 *   Mono in a paragraph     `...`
 *   centre, right, justify  <div align="center"> ... </div> around it
 *   page break              <div style="page-break-after: always"></div>
 *   Serif, Head in part     (no Markdown for them: plain text)
 * Punctuation that Markdown reads (^ too: Pandoc's superscript) is escaped with "\"; spaces at a
 * paragraph's start are &nbsp;. Empty paragraphs are not written. Latin-1
 * characters are written as UTF-8.
 */
#include "wp.h"
#define PET_OUT
#define PET_UTF8
#include "petuni.h"

#define M_CODE 0x01             /* a character shown as code (a font bit: not in M_FLAGS) */
#define M_FLAGS (A_UNDER | A_BOLD | A_ITAL | A_STRIKE | A_SCRIPT)

/* ==== export ==== */
#define out o_put
#define outs o_puts

static u8 is_sp(u8 c) { return c == ' '; }
static u8 is_letter(u8 c) { return (u8)((c | 0x20) - 'a') < 26; }

static u8 base;                 /* the paragraph kind's own style (kind_attr) */

/* the Markdown style of character i: its underline / bold / italic (those
 * the paragraph's kind does not give it), and M_CODE for Mono in another
 * kind (never for a backtick, which a code span cannot hold) */
static u8 raw_style(u16 i) {
    u8 a = sty(atr_at(i)), s = a & M_FLAGS & ~base;
    if ((a & A_FONT) == F_MONO && (base & A_FONT) != F_MONO && txt_at(i) != '`') s |= M_CODE;
    return s;
}

/* a character as UTF-8 (Latin-1: two bytes from $80) */
static void out_utf8(u8 c) {
    if (c >= 0x80) {
        out((u8)(0xC0 | (c >> 6)));
        c = (u8)(0x80 | (c & 0x3F));
    }
    out(c);
}

/* the markers, outermost first */
#define NMARK 7
static const u8 mark_bit[NMARK] = { A_UNDER, A_SUP, A_SUB, A_STRIKE, A_BOLD, A_ITAL, M_CODE };
static const char *const mark_open[NMARK] = { "<u>", "<sup>", "<sub>", "~~", "**", "*", "`" };
static const char *const mark_close[NMARK] = { "</u>", "</sup>", "</sub>", "~~", "**", "*", "`" };
static u8 open_st;              /* the markers open */

/* the first marker (outermost first) that differs between the open ones
 * and style t */
static u8 first_diff(u8 t) {
    u8 k = 0;
    while (k < NMARK && ((open_st ^ t) & mark_bit[k]) == 0) k++;
    return k;
}

/* from the markers open to style t: the inner ones closed back to the
 * first that changes, then t's opened from there */
static void restyle_to(u8 t) {
    u8 k = first_diff(t), m;
    if (k == NMARK) return;
    for (m = NMARK; m-- > k;)
        if (open_st & mark_bit[m]) outs(mark_close[m]);
    for (m = k; m < NMARK; m++)
        if (t & mark_bit[m]) outs(mark_open[m]);
    open_st = t;
}

static u8 in_set(const char *s, u8 c) {
    while (*s)
        if ((u8)*s++ == c) return 1;
    return 0;
}

/* the text of paragraph ps..pe-1 (pe: its CR / FF); code: as a code block
 * (2: a PETSCII line) */
static void out_para(u16 ps, u16 pe, u8 code) {
    u16 i = ps, j;
    u8 c, s, lead = 1, digits = 1;
    open_st = 0;
    /* spaces at the end left out (two would be a hard break), unless
     * there is nothing else */
    for (j = pe; j > ps && is_sp(txt_at(j - 1)); j--) ;
    if (j > ps && !code) pe = j;
    while (i < pe) {
        c = txt_at(i);
        if (code) {
            if (code > 1) out_uni(pet_to_uni(c));
            else out_utf8(c);
            i++;
            continue;
        }
        if (is_sp(c)) {
            /* a run of spaces: the markers the next character keeps stay
             * open over it; the others are closed before it, the new ones
             * opened after it (a marker never next to a space inside) */
            for (j = i; j < pe && is_sp(txt_at(j)); j++) ;
            s = j < pe ? raw_style(j) : 0;
            for (c = first_diff(s), s = 0; c-- > 0;) s |= open_st & mark_bit[c];
            restyle_to(s);
            for (; i < j; i++) outs(lead ? "&nbsp;" : " ");
            continue;
        }
        s = raw_style(i);
        restyle_to(s);
        if (s & M_CODE) {
            out_utf8(c);
        } else {
            /* escaped: what Markdown would read as markup (at the start: a
             * heading, quote, list, or the dot or ) after a number) */
            u8 n = txt_at(i + 1);
            if (in_set("\\`*_[]~^", c) || (lead && in_set("#>-+=", c)) || (digits && !lead && in_set(".)", c)) ||
                (c == '<' && (is_letter(n) || in_set("/!?", n))) || (c == '&' && (is_letter(n) || n == '#')))
                out('\\');
            out_utf8(c);
        }
        if (c < '0' || c > '9') digits = 0;
        lead = 0;
        i++;
    }
    restyle_to(0);
}

#define P_NONE 0xFF             /* (prev: no block yet; a page break) */
#define P_BREAK 0xFE

static const char *const align_attr[4] = { "", "center", "right", "justify" };
static u8 prev, pal;            /* the last block's kind; its alignment (a div open) */
static u8 ppet;                 /* the last block a PETSCII line (A_PET) */

/* the last block closed (its fence, its div) and a blank line after it */
static void end_block(void) {
    if (prev == K_CODE) outs("\n```");
    if (pal) outs("\n\n</div>");
    if (prev != P_NONE) outs("\n\n");
    pal = 0;
}

/* the document as Markdown at SCRATCH_LIN; its size (0: more than fits) */
u16 md_export(void) {
    u16 ps, pe;
    u8 ev, kind, al, gap = 0, k, pet;
    o_start();
    walk_start();
    prev = P_NONE;
    pal = 0;
    while ((ev = walk_next()) != EV_END) {
        if (ev == EV_PEND && w_attr == CH_FF) {
            end_block();
            outs("<div style=\"page-break-after: always\"></div>");
            prev = P_BREAK;
            gap = 1;
        }
        if (ev != EV_PARA) continue;
        ps = w_pos;
        pe = w_to;
        kind = w_attr & A_KIND;
        pet = kind == K_CODE ? w_attr & A_PET : 0;
        al = (u8)((w_attr & A_ALIGN) >> A_ALIGN_SHIFT);
        if (kind >= K_CODE) al = 0;
        /* (an empty paragraph: a gap, but an empty line in a code block) */
        if (pe == ps && (kind != K_CODE || prev != K_CODE || pet != ppet || gap)) {
            gap = 1;
            continue;
        }
        if (!gap && ((kind == prev && kind == K_CODE && pet == ppet) || (kind >= K_BULLET && (u8)(prev - K_BULLET) < 2))) {
            out('\n');
        } else if (!gap && kind == prev && (kind == K_NORMAL || kind == K_QUOTE) && al == pal) {
            outs("\\\n");
        } else {
            end_block();
            if (al) {
                outs("<div align=\"");
                outs(align_attr[al]);
                outs("\">\n\n");
            }
            if ((u8)(kind - K_H1) < 3) {
                for (k = kind; k; k--) out('#');
                out(' ');
            }
            if (kind == K_CODE) outs(pet ? "```petscii\n" : "```\n");
            pal = al;
        }
        if (kind == K_QUOTE) outs("> ");
        if (kind >= K_BULLET) {
            /* a list item: its level's indent, its marker */
            for (k = (u8)((w_attr & A_LEVEL) >> A_LEVEL_SHIFT); k; k--) outs("    ");
            if (kind == K_BULLET) {
                out('-');
            } else {
                char t[5];
                u16 v = item_num(ps);
                k = 0;
                do {
                    t[k++] = (char)('0' + v % 10);
                    v /= 10;
                } while (v);
                while (k) out((u8)t[--k]);
                out('.');
            }
            out(' ');
        }
        base = kind_attr[kind];
        out_para(ps, pe, kind == K_CODE ? 1 + !!pet : 0);
        prev = kind;
        ppet = pet;
        gap = 0;
    }
    end_block();
    return o_end();
}

/* MARKDOWN.OVL's entry (mod_run) */
void md_main(void) {
    mod_ret = md_export();
}
