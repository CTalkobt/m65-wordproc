/* md.c — Markdown export and import (UTF-8 text, LF line ends)
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
 *                           block (``` ... ```), a line a paragraph
 *   bold, italic            **...**, *...* (where they differ from the
 *                           paragraph kind's own style: a heading's bold is
 *                           not written)
 *   underline               <u>...</u>
 *   Mono in a paragraph     `...`
 *   centre, right, justify  <div align="center"> ... </div> around it
 *   page break              <div style="page-break-after: always"></div>
 *   Serif, Head in part     (no Markdown for them: plain text)
 * Punctuation that Markdown reads is escaped with "\"; spaces at a
 * paragraph's start are &nbsp;. Empty paragraphs are not written. Latin-1
 * characters are written as UTF-8.
 *
 * Import (md_import: SCRATCH_LIN, as FREAD# left it, as the document):
 * the above (headings past ### as heading 3), setext headings (= heading
 * 1, - heading 2), lists (their markers kept as text), links ([text](url) -> "text (url)", images: their text), HTML
 * tags left out, the common entities, and UTF-8 characters (Latin-1's as
 * they are, curly quotes, dashes, ... as ASCII, others "?": build_uni). Blocks are separated by an empty paragraph (not after a
 * heading). Emphasis is paired within a paragraph; an unpaired * or _ is
 * text.
 */
#include "wp.h"

#define M_CODE 0x80             /* export: a character shown as code */
#define M_FLAGS (A_UNDER | A_BOLD | A_ITAL)

/* ==== export ==== */
#define out o_put
#define outs o_puts

static u8 is_sp(u8 c) { return c == ' '; }
static u8 is_letter(u8 c) { return (u8)((c | 0x20) - 'a') < 26; }
static u8 is_alnum(u8 c) { return is_letter(c) || (u8)(c - '0') < 10; }
static u8 is_punct(u8 c) { return c > ' ' && c < 127 && !is_alnum(c); }

static u8 base;                 /* the paragraph kind's own style (kind_attr) */

/* the Markdown style of character i: its underline / bold / italic (those
 * the paragraph's kind does not give it), and M_CODE for Mono in another
 * kind (never for a backtick, which a code span cannot hold) */
static u8 raw_style(u16 i) {
    u8 a = atr_at(i), s = a & M_FLAGS & ~base;
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
static const u8 mark_bit[4] = { A_UNDER, A_BOLD, A_ITAL, M_CODE };
static const char *const mark_open[4] = { "<u>", "**", "*", "`" };
static const char *const mark_close[4] = { "</u>", "**", "*", "`" };
static u8 open_st;              /* the markers open */

/* the first marker (outermost first) that differs between the open ones
 * and style t */
static u8 first_diff(u8 t) {
    u8 k = 0;
    while (k < 4 && ((open_st ^ t) & mark_bit[k]) == 0) k++;
    return k;
}

/* from the markers open to style t: the inner ones closed back to the
 * first that changes, then t's opened from there */
static void restyle_to(u8 t) {
    u8 k = first_diff(t), m;
    if (k == 4) return;
    for (m = 4; m-- > k;)
        if (open_st & mark_bit[m]) outs(mark_close[m]);
    for (m = k; m < 4; m++)
        if (t & mark_bit[m]) outs(mark_open[m]);
    open_st = t;
}

static u8 in_set(const char *s, u8 c) {
    while (*s)
        if ((u8)*s++ == c) return 1;
    return 0;
}

/* the text of paragraph ps..pe-1 (pe: its CR / FF); code: as a code block */
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
            out_utf8(c);
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
            if (in_set("\\`*_[]~", c) || (lead && in_set("#>-+=", c)) || (digits && !lead && in_set(".)", c)) ||
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
    u8 ev, kind, al, gap = 0, k;
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
        al = (u8)((w_attr & A_ALIGN) >> A_ALIGN_SHIFT);
        if (kind == K_CODE) al = 0;
        /* (an empty paragraph: a gap, but an empty line in a code block) */
        if (pe == ps && (kind != K_CODE || prev != K_CODE || gap)) {
            gap = 1;
            continue;
        }
        if (!gap && kind == prev && kind == K_CODE) {
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
            if (kind == K_CODE) outs("```\n");
            pal = al;
        }
        if (kind == K_QUOTE) outs("> ");
        base = kind_attr[kind];
        out_para(ps, pe, kind == K_CODE);
        prev = kind;
        gap = 0;
    }
    end_block();
    return o_end();
}

/* ==== import ==== */
static u8 pk, base_attr;        /* the paragraph's kind, its style (kind_attr) */
static u8 flags;                /* bold / italic / underline open */
static u8 code;                 /* in a code span or block */
static u8 para_open, after_head, align, hard;
static u16 pstart;              /* the paragraph's first character */

/* the input (a zero byte at its end) */
static u8 in(u16 i) { return mem_get(SCRATCH_LIN + i); }

/* past a run of ch from i */
static u16 skip(u16 i, u8 ch) {
    while (in(i) == ch) i++;
    return i;
}

/* the first ch in i..e-1 (e: none) */
static u16 find(u16 i, u16 e, u8 ch) {
    while (i < e && in(i) != ch) i++;
    return i;
}

static u8 lower(u8 c) { return is_letter(c) ? (u8)(c | 0x20) : c; }

/* does s (lower case) start at i? */
static u8 starts(u16 i, const char *s) {
    while (*s)
        if (lower(in(i++)) != (u8)*s++) return 0;
    return 1;
}

/* a code fence (``` or ~~~) at i */
static u8 is_fence(u16 i) {
    u8 c = in(i);
    return (c == '`' || c == '~') && skip(i, c) >= i + 3;
}

/* a character into the document */
static void raw(u8 c, u8 a) {
    b_attr = a;
    build_put(c);
}

/* the style of the text now */
static void style(void) { b_attr = (u8)((code ? F_MONO : base_attr) | flags); }

static void put(u8 c) {
    style();
    build_put(c);
}

static void puts_(const char *s) {
    while (*s) put((u8)*s++);
}

static void lit(u8 c, u8 k) {
    while (k--) put(c);
}

/* the paragraph's end: a CR with its alignment */
static void end_para(void) {
    if (!para_open) return;
    raw(CH_CR, (u8)((align << A_ALIGN_SHIFT) | pk));
    para_open = 0;
    flags = code = 0;
}

/* a block starts: an empty paragraph before it (not at the start, nor
 * after a heading or a page break) */
static void block_start(void) {
    if (b_n && !after_head) raw(CH_CR, 0);
    after_head = 0;
}

/* a paragraph of kind k starts */
static void para_start(u8 k) {
    para_open = 1;
    pstart = b_n;
    pk = k;
    base_attr = kind_attr[k];
}

/* a closing c (not after a space) from i to the paragraph's end (a blank line) */
static u8 closer(u8 c, u16 i) {
    u8 nl = 0, d;
    for (;; i++) {
        d = in(i);
        if (!d) return 0;
        if (d == '\n') {
            if (nl) return 0;
            nl = 1;
        } else if (d != ' ' && d != '\r') {
            nl = 0;
            if (d == c && in(i - 1) != ' ') return 1;
        }
    }
}

/* a UTF-8 character from i as the document's (build_uni); its length */
static u8 utf8(u16 i) {
    u8 c = in(i), n = c < 0xC0 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4, k;
    u16 u = c & (n == 2 ? 0x1F : 0x0F);
    for (k = 1; k < n; k++) {
        if ((in(i + k) & 0xC0) != 0x80) return k;   /* broken: dropped */
        u = (u16)((u << 6) | (in(i + k) & 0x3F));
    }
    if (n == 1 || n == 4) u = '?';                  /* a stray continuation byte; beyond U+FFFF */
    style();
    build_uni(u);
    return n;
}

/* a hard line break inside a paragraph: a new paragraph (no empty one) */
static void line_break(void) {
    u8 f = flags;
    end_para();
    para_open = 1;
    flags = f;
}

static const char *const entity[] = { "nbsp;", " ", "amp;", "&", "lt;", "<", "gt;", ">", "quot;", "\"", "#39;", "'" };
#define NENT (sizeof entity / sizeof entity[0])

/* the inline text i..e-1 into the document */
static void inline_md(u16 i, u16 e) {
    u16 j, cl, lk = 0xFFFF, u0 = 0, u1 = 0, after = 0;
    u8 c, k, n, img = 0, want, pv, nx;
    while (i < e) {
        c = in(i);
        if (i == lk) {
            /* a link's text done: its address after it (not an image's) */
            if (!img && u1 > u0) {
                puts_(" (");
                for (j = u0; j < u1; j++) put(in(j));
                put(')');
            }
            lk = 0xFFFF;
            i = after;
            continue;
        }
        if (c == '\\' && i + 1 < e && is_punct(in(i + 1))) {
            put(in(i + 1));
            i += 2;
            continue;
        }
        if (c == '`') {
            /* a code span: to the next run of as many backticks */
            j = skip(i, '`');
            k = (u8)(j - i);
            for (;;) {
                j = find(j, e, '`');
                if (j >= e) break;
                n = (u8)(skip(j, '`') - j);
                if (n == k) break;
                j += n;
            }
            if (j >= e) {
                lit('`', k);
                i += k;
                continue;
            }
            cl = j;
            i += k;
            if (in(i) == ' ' && in(j - 1) == ' ' && j - i > 2) i++, j--;
            code = 1;
            while (i < j) {
                if (in(i) >= 0x80) i += utf8(i);
                else put(in(i++));
            }
            code = 0;
            i = (u16)(cl + k);
            continue;
        }
        if (c == '*' || c == '_') {
            pv = i ? in(i - 1) : ' ';
            k = (u8)(skip(i, c) - i);
            if (k > 3) k = 3;
            nx = (u16)(i + k) < e ? in(i + k) : ' ';
            if (c == '_' && is_alnum(pv) && is_alnum(nx)) {
                lit(c, k);
                i += k;
                continue;
            }
            want = k == 3 ? A_BOLD | A_ITAL : k == 2 ? A_BOLD : A_ITAL;
            if (pv != ' ' && (flags & want)) {
                /* closing what is open; the rest of the run may open (as
                 * the italic in **a***b*) */
                want &= flags;
                flags &= (u8)~want;
                n = (u8)(((want & A_BOLD) ? 2 : 0) + ((want & A_ITAL) ? 1 : 0));
                i += n;
                k -= n;
                want = k == 2 ? A_BOLD : A_ITAL;
            }
            if (k) {
                if (nx != ' ' && closer(c, (u16)(i + k))) flags |= want;
                else lit(c, k);
                i += k;
            }
            continue;
        }
        if (c == '<' && (is_alnum(in(i + 1)) || in(i + 1) == '/')) {
            j = find(i, e, '>');
            if (j < e) {
                if (starts(i, "<http")) {
                    for (i++; i < j; i++) put(in(i));
                } else if (starts(i, "<u>")) {
                    flags |= A_UNDER;
                } else if (starts(i, "</u>")) {
                    flags &= (u8)~A_UNDER;
                } else if (starts(i, "<br")) {
                    line_break();
                }
                i = (u16)(j + 1);
                continue;
            }
        }
        if (c == '&') {
            for (k = 0; k < NENT && !starts((u16)(i + 1), entity[k]); k += 2) ;
            if (k < NENT) {
                put((u8)entity[k + 1][0]);
                i = (u16)(find(i, e, ';') + 1);
                continue;
            }
        }
        if ((c == '[' || (c == '!' && in(i + 1) == '[')) && lk == 0xFFFF) {
            /* [text](address): the text, then the address */
            u16 o = (u16)(i + (c == '!') + 1);
            j = find(o, e, ']');
            if (j < e && in(j + 1) == '(') {
                u1 = find((u16)(j + 2), e, ')');
                if (u1 < e) {
                    img = c == '!';
                    lk = j;
                    u0 = (u16)(j + 2);
                    after = (u16)(u1 + 1);
                    i = o;
                    continue;
                }
            }
        }
        if (c >= 0x80) {
            i += utf8(i);
            continue;
        }
        if (c == '\t') c = ' ';
        if (c >= ' ') put(c);
        i++;
    }
}

/* SCRATCH_LIN, after GET_IMPORT_PREP zeroed it and FREAD# filled it, as
 * the document; its length */
u16 md_import(void) {
    u16 i = 0, ls, le, e, j, t;
    u8 c, fence = 0, n, item = 0, q;
    mem_put(SCRATCH_LIN + SCRATCH_MAX - 1, 0);  /* (an end, even for a file that fills it) */
    build_start();
    para_open = after_head = align = hard = flags = code = pk = base_attr = 0;
    while (in(i)) {
        /* the line ls..le-1 (without its CR / LF) */
        for (ls = i; (c = in(i)) != 0 && c != '\n' && c != '\r'; i++) ;
        le = i;
        if (in(i) == '\r') i++;
        if (in(i) == '\n') i++;
        j = skip(ls, ' ');
        c = in(j);
        if (fence) {
            if (is_fence(j)) {
                fence = 0;
                after_head = 0;
                continue;
            }
            if (fence == 1) block_start();
            fence = 2;
            para_start(K_CODE);
            code = 1;
            for (e = ls; e < le; e++) {
                c = in(e);
                if (c == '\t') puts_("    ");
                else if (c >= 0x80) e += (u16)(utf8(e) - 1);
                else if (c >= ' ') put(c);
            }
            end_para();
            continue;
        }
        if (j == le) {
            /* a blank line */
            end_para();
            item = 0;
            continue;
        }
        if (is_fence(j)) {
            end_para();
            fence = 1;
            continue;
        }
        if (starts(j, "<div")) {
            end_para();
            if (starts((u16)(j + 12), "page-break")) {
                /* <div style="page-break-after: always"></div> */
                if (b_n) raw(CH_FF, 0);
                after_head = 1;
            } else {
                /* <div align="...">: the alignment of what follows */
                for (align = 3; align && !starts((u16)(j + 12), align_attr[align]); align--) ;
            }
            continue;
        }
        if (starts(j, "</div")) {
            end_para();
            align = 0;
            continue;
        }
        /* a setext heading's underline, or a thematic break */
        if (c == '=' || c == '-' || c == '*' || c == '_') {
            for (e = j, n = 0; e < le && (in(e) == c || in(e) == ' '); e++) n += in(e) == c;
            if (e == le && (para_open ? c == '=' || c == '-' : n >= 3)) {
                if (para_open) {
                    pk = c == '=' ? K_H1 : K_H2;
                    for (e = pstart; e < b_n; e++) atr_put(e, (u8)(kind_attr[pk] | (atr_at(e) & M_FLAGS)));
                    end_para();
                    after_head = 1;
                }
                continue;
            }
        }
        if (c == '#') {
            n = (u8)(skip(j, '#') - j);
            if (n <= 6 && (j + n == le || in(j + n) == ' ')) {
                end_para();
                block_start();
                /* its text, without a closing run of # after a space */
                for (e = le; e > j && in(e - 1) == ' '; e--) ;
                for (t = e; t > j && in(t - 1) == '#'; t--) ;
                if (t < e && (t == j || in(t - 1) == ' ')) e = t;
                para_start((u8)(n > 3 ? K_H3 : n));
                inline_md(skip((u16)(j + n), ' '), e);
                end_para();
                after_head = 1;
                continue;
            }
        }
        /* quotes: their > left out, a quote paragraph (lines without one
         * go on with it); ">" alone ends a paragraph in it */
        q = in(j) == '>';
        while (in(j) == '>') j = skip((u16)(j + 1), ' ');
        if (q && (j >= le || pk != K_QUOTE)) end_para();
        if (j >= le) continue;
        c = in(j);
        /* list items: a paragraph each, their marker kept */
        n = 0;
        if ((c == '-' || c == '*' || c == '+') && in(j + 1) == ' ') {
            n = 2;
        } else {
            for (e = j; (u8)(in(e) - '0') < 10; e++) ;
            if (e > j && (in(e) == '.' || in(e) == ')') && in(e + 1) == ' ') n = (u8)(e + 2 - j);
        }
        if (n) {
            end_para();
            if (!item) block_start();
            item = 1;
            para_start(q ? K_QUOTE : K_NORMAL);
            for (; n; n--) put(in(j++));
        } else if (!para_open) {
            /* a new paragraph */
            block_start();
            para_start(q ? K_QUOTE : K_NORMAL);
        } else if (hard) {
            /* after a hard break: a paragraph with no empty one before it */
            line_break();
        } else {
            put(' ');
        }
        /* a hard break at its end: "\" or two spaces */
        e = le;
        hard = 0;
        if (in(e - 1) == '\\') {
            e--;
            hard = 1;
        } else if (le - ls >= 2 && in(e - 1) == ' ' && in(e - 2) == ' ') {
            hard = 1;
        }
        while (e > j && in(e - 1) == ' ') e--;
        inline_md(j, e);
    }
    end_para();
    return build_end();
}

/* MARKDOWN.OVL's entry (mod_run): export or import */
void md_main(void) {
    mod_ret = mod_cmd == MOD_MD_EXPORT ? md_export() : md_import();
}
