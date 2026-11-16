/* md.c — Markdown export and import (UTF-8 / ASCII text, LF line ends)
 *
 * Export (md_export: the document to SCRATCH_LIN):
 *   a paragraph             a line; paragraphs that follow each other with
 *                           no empty one between (and the same alignment)
 *                           are one Markdown paragraph with hard breaks
 *                           ("\" at the line's end); others are separate
 *                           blocks (a blank line between)
 *   all Head font           "# " heading
 *   all Mono, unstyled      a fenced code block (``` ... ```), one line a
 *                           paragraph
 *   bold, italic            **...**, *...*
 *   underline               <u>...</u>
 *   Mono in a paragraph     `...`
 *   centre, right, justify  <div align="center"> ... </div> around it
 *   page break              <div style="page-break-after: always"></div>
 *   Serif, Head in part     (no Markdown for them: plain text)
 * Punctuation that Markdown reads is escaped with "\"; spaces at a
 * paragraph's start are &nbsp;. Empty paragraphs are not written.
 *
 * Import (md_import: SCRATCH_LIN, as FREAD# left it, as the document):
 * the above, and setext headings, lists and quotes (their markers kept as
 * text), links ([text](url) -> "text (url)", images: their text), HTML
 * tags left out, the common entities, and UTF-8 characters as the nearest
 * ASCII (accented letters without their accents, curly quotes, dashes, ...;
 * others "?"). Blocks are separated by an empty paragraph (not after a
 * heading). Emphasis is paired within a paragraph; an unpaired * or _ is
 * text.
 */
#include "wp.h"

#define F_MONO 2
#define F_HEAD 3
#define M_CODE 0x80             /* export: a character shown as code */
#define M_FLAGS (A_UNDER | A_BOLD | A_ITAL)
#define MD_MAX 30720u           /* the scratch area */

/* ==== export ==== */
static u16 on;                  /* bytes written */

static void out(u8 c) {
    if (on < MD_MAX) mem_put(SCRATCH_LIN + on, c);
    on++;
}

static void outs(const char *s) {
    while (*s) out((u8)*s++);
}

static u8 is_sp(u8 c) { return c == ' '; }
static u8 is_break(u8 c) { return c == CH_CR || c == CH_FF; }
static u8 is_letter(u8 c) { return (u8)((c | 0x20) - 'a') < 26; }
static u8 is_alnum(u8 c) { return is_letter(c) || (u8)(c - '0') < 10; }
static u8 is_punct(u8 c) { return c > ' ' && c < 127 && !is_alnum(c); }

/* the Markdown style of character i: its underline / bold / italic, and
 * M_CODE for Mono (never for a backtick, which a code span cannot hold) */
static u8 raw_style(u16 i) {
    u8 a = ATR[i], s = a & M_FLAGS;
    if ((a & A_FONT) == F_MONO && TXT[i] != '`') s |= M_CODE;
    return s;
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
    for (j = pe; j > ps && is_sp(TXT[j - 1]); j--) ;
    if (j > ps && !code) pe = j;
    while (i < pe) {
        c = TXT[i];
        if (code) {
            out(c);
            i++;
            continue;
        }
        if (is_sp(c)) {
            /* a run of spaces: the markers the next character keeps stay
             * open over it; the others are closed before it, the new ones
             * opened after it (a marker never next to a space inside) */
            for (j = i; j < pe && is_sp(TXT[j]); j++) ;
            s = j < pe ? raw_style(j) : 0;
            for (c = first_diff(s), s = 0; c-- > 0;) s |= open_st & mark_bit[c];
            restyle_to(s);
            for (; i < j; i++) outs(lead ? "&nbsp;" : " ");
            continue;
        }
        s = raw_style(i);
        restyle_to(s);
        if (s & M_CODE) {
            out(c);
        } else {
            /* escaped: what Markdown would read as markup (at the start: a
             * heading, quote, list, or the dot or ) after a number) */
            u8 n = TXT[i + 1];
            if (in_set("\\`*_[]~", c) || (lead && in_set("#>-+=", c)) || (digits && !lead && in_set(".)", c)) ||
                (c == '<' && (is_letter(n) || in_set("/!?", n))) || (c == '&' && (is_letter(n) || n == '#')))
                out('\\');
            out(c);
        }
        if (c < '0' || c > '9') digits = 0;
        lead = 0;
        i++;
    }
    restyle_to(0);
}

enum { K_NONE, K_PLAIN, K_HEAD, K_CODE, K_BREAK };

static const char *const align_attr[4] = { "", "center", "right", "justify" };
static u8 prev, pal;            /* the last block's kind; its alignment (a div open) */

/* the last block closed (its fence, its div) and a blank line after it */
static void end_block(void) {
    if (prev == K_CODE) outs("\n```");
    if (pal) outs("\n\n</div>");
    if (prev != K_NONE) outs("\n\n");
    pal = 0;
}

/* the document as Markdown at SCRATCH_LIN; its size (0: more than fits) */
u16 md_export(void) {
    u16 ps = 0, pe, i;
    u8 kind, al, gap = 0, a, head, mono, any;
    on = 0;
    prev = pal = K_NONE;
    while (ps < doc_len) {
        for (pe = ps; !is_break(TXT[pe]); pe++) ;
        al = (u8)((ATR[pe] & A_ALIGN) >> A_ALIGN_SHIFT);
        if (pe > ps) {
            /* its kind: all Head, all unstyled Mono (its spaces aside: a
             * code span or block takes them in), or plain */
            head = mono = 1;
            any = 0;
            for (i = ps; i < pe; i++) {
                a = ATR[i];
                if (is_sp(TXT[i])) continue;
                any = 1;
                if ((a & A_FONT) != F_HEAD) head = 0;
                if ((a & A_FONT) != F_MONO || (a & M_FLAGS)) mono = 0;
            }
            kind = !any ? K_PLAIN : head ? K_HEAD : mono ? K_CODE : K_PLAIN;
            if (kind == K_CODE) al = 0;
            if (!gap && kind == prev && kind == K_CODE) {
                out('\n');
            } else if (!gap && kind == prev && kind == K_PLAIN && al == pal) {
                outs("\\\n");
            } else {
                end_block();
                if (al) {
                    outs("<div align=\"");
                    outs(align_attr[al]);
                    outs("\">\n\n");
                }
                if (kind == K_HEAD) outs("# ");
                if (kind == K_CODE) outs("```\n");
                pal = al;
            }
            out_para(ps, pe, kind == K_CODE);
            prev = kind;
            gap = 0;
        } else {
            gap = 1;
        }
        if (TXT[pe] == CH_FF) {
            end_block();
            outs("<div style=\"page-break-after: always\"></div>");
            prev = K_BREAK;
            gap = 1;
        }
        ps = (u16)(pe + 1);
    }
    end_block();
    return on <= MD_MAX ? on : 0;
}

/* ==== import ==== */
static u8 base_font;            /* Head in a heading */
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
    if (doc_len >= DOC_MAX - 1) return;     /* (room kept for the final CR) */
    TXT[doc_len] = c;
    ATR[doc_len] = a;
    doc_len++;
}

static void put(u8 c) { raw(c, (u8)((code ? F_MONO : base_font) | flags)); }

static void puts_(const char *s) {
    while (*s) put((u8)*s++);
}

static void lit(u8 c, u8 k) {
    while (k--) put(c);
}

/* the paragraph's end: a CR with its alignment */
static void end_para(void) {
    if (!para_open) return;
    raw(CH_CR, (u8)(align << A_ALIGN_SHIFT));
    para_open = 0;
    flags = code = 0;
}

/* a block starts: an empty paragraph before it (not at the start, nor
 * after a heading or a page break) */
static void block_start(void) {
    if (doc_len && !after_head) raw(CH_CR, 0);
    after_head = 0;
}

/* a paragraph starts */
static void para_start(void) {
    para_open = 1;
    pstart = doc_len;
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

/* UTF-8 characters as ASCII: these, then Latin-1 letters U+00C0-U+00FF
 * without their accents; others "?" */
static const u16 uni[] = { 0xDF, 0xA0, 0x2018, 0x2019, 0x201A, 0xB4, 0x201C, 0x201D, 0x201E, 0xAB, 0xBB,
                           0x2013, 0x2010, 0x2212, 0x2014, 0x2026, 0x2022, 0xB7, 0x2192, 0x2190, 0xA9 };
static const char *const uni_as[] = { "ss", " ", "'", "'", "'", "'", "\"", "\"", "\"", "\"", "\"",
                                      "-", "-", "-", "--", "...", "*", "*", "->", "<-", "(c)" };
static const char latin[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";

/* a UTF-8 character from i as ASCII; its length */
static u8 utf8(u16 i) {
    u8 c = in(i), n = c < 0xC0 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4, k;
    u16 u = c & (n == 2 ? 0x1F : 0x0F);
    for (k = 1; k < n; k++) {
        if ((in(i + k) & 0xC0) != 0x80) return k;   /* broken: dropped */
        u = (u16)((u << 6) | (in(i + k) & 0x3F));
    }
    if (n == 1 || n == 4) u = '?';                  /* a stray continuation byte; beyond U+FFFF */
    for (k = 0; k < sizeof uni / sizeof uni[0]; k++)
        if (uni[k] == u) {
            puts_(uni_as[k]);
            return n;
        }
    put(u >= 0xC0 && u <= 0xFF ? (u8)latin[u - 0xC0] : '?');
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
            for (; i < j; i++) put(in(i));
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
    u8 c, fence = 0, n, item = 0;
    mem_put(SCRATCH_LIN + MD_MAX - 1, 0);   /* (an end, even for a file that fills it) */
    doc_len = 0;
    para_open = after_head = align = hard = flags = code = base_font = 0;
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
            para_start();
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
                if (doc_len) raw(CH_FF, 0);
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
                    for (e = pstart; e < doc_len; e++) ATR[e] = (u8)((ATR[e] & ~A_FONT) | F_HEAD);
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
                base_font = F_HEAD;
                para_start();
                inline_md(skip((u16)(j + n), ' '), e);
                end_para();
                base_font = 0;
                after_head = 1;
                continue;
            }
        }
        /* quotes: their > left out */
        while (in(j) == '>') j = skip((u16)(j + 1), ' ');
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
            para_start();
            for (; n; n--) put(in(j++));
        } else if (!para_open) {
            /* a new paragraph */
            block_start();
            para_start();
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
    doc_loaded(1);
    return (u16)(doc_len - 1);
}

/* MARKDOWN.OVL's entry (mod_run): export or import */
void mod_main(void) {
    mod_ret = mod_cmd == MOD_MD_EXPORT ? md_export() : md_import();
}
