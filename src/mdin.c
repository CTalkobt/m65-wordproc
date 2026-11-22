/* mdin.c — Markdown import (UTF-8 text): the module MDIN.OVL (the export
 * is MARKDOWN.OVL, md.c: apart, as together they were the largest module,
 * and MOD_SIZE is bank 0's)
 *
 * md_import: SCRATCH_LIN, as FREAD# left it, as the document: what the
 * export writes (headings past ### as heading 3), setext headings (=
 * heading 1, - heading 2), list items (the level by the marker's indent
 * against the items before it; an item's marker not kept), links
 * ([text](url) -> "text (url)", images: their text), HTML tags left out,
 * the common entities, and UTF-8 characters (Latin-1's as they are, curly
 * quotes, dashes, ... as ASCII, others "?": build_uni). Blocks are
 * separated by an empty paragraph (not after a heading). Emphasis is
 * paired within a paragraph; an unpaired * or _ is text. Also ~~strike~~,
 * Pandoc's ^superscript^ and ~subscript~, and <u> <s> <del> <strike> <sup>
 * <sub>. A ```petscii fence is a PETSCII line's: its characters back to
 * PETSCII (petuni.h).
 */
#include "wp.h"
#define PET_IN
#include "petuni.h"

#define M_FLAGS (A_UNDER | A_BOLD | A_ITAL | A_STRIKE | A_SCRIPT)

static u8 is_letter(u8 c) { return (u8)((c | 0x20) - 'a') < 26; }
static u8 is_alnum(u8 c) { return is_letter(c) || (u8)(c - '0') < 10; }
static u8 is_punct(u8 c) { return c > ' ' && c < 127 && !is_alnum(c); }
static const char *const align_attr[4] = { "", "center", "right", "justify" };

static u8 pk, base_attr;        /* the paragraph's kind (and level), its style (kind_attr) */
static u8 item, lv, lv_sp[4];   /* in a list; the last item's level; each level's indent */
static u8 flags;                /* bold / italic / underline / strikethrough / script open */
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
    after_head = item = 0;
}

/* a paragraph of kind k starts */
static void md_para(u8 k) {
    para_open = 1;
    pstart = b_n;
    pk = k;
    base_attr = kind_attr[k & A_KIND];
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

/* a UTF-8 character from i (or ASCII): uv (U+1FBxx: 0xFBxx; beyond U+FFFF
 * otherwise, or a stray continuation byte: "?"; broken: 0); its length */
static u16 uv;
static u8 utf8_get(u16 i) {
    u8 c = in(i), n = c < 0xC0 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4, k;
    uv = c & (n == 2 ? 0x1F : 0x0F);
    for (k = 1; k < n; k++) {
        if ((in(i + k) & 0xC0) != 0x80) {
            uv = 0;
            return k;
        }
        uv = (u16)((uv << 6) | (in(i + k) & 0x3F));
    }
    if (n == 1) uv = c < 0x80 ? c : '?';
    else if (n == 4 && (c != 0xF0 || in(i + 1) != 0x9F || (uv >> 8) != 0xFB)) uv = '?';
    return n;
}

/* a UTF-8 character from i as the document's (build_uni); its length */
static u8 utf8(u16 i) {
    u8 n = utf8_get(i);
    if (uv) {
        style();
        build_uni(uv);
    }
    return n;
}

/* a hard line break inside a paragraph: a new paragraph (no empty one) */
static void line_break(void) {
    u8 f = flags;
    end_para();
    para_open = 1;
    flags = f;
}

static const char *const tag_name[] = { "u>", "s>", "del>", "strike>", "sup>", "sub>" };
static const u8 tag_bit[] = { A_UNDER, A_STRIKE, A_STRIKE, A_STRIKE, A_SUP, A_SUB };
#define NTAG 6

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
        if (c == '~' && in(i + 1) == '~') {
            /* ~~strikethrough~~, paired as emphasis is */
            pv = i ? in(i - 1) : ' ';
            nx = (u16)(i + 2) < e ? in(i + 2) : ' ';
            if ((flags & A_STRIKE) && pv != ' ') flags &= (u8)~A_STRIKE;
            else if (nx != ' ' && closer('~', (u16)(i + 2))) flags |= A_STRIKE;
            else lit(c, 2);
            i += 2;
            continue;
        }
        if (c == '^' || c == '~') {
            /* Pandoc's ^superscript^ and ~subscript~: to the next one with
             * no space between */
            want = c == '^' ? A_SUP : A_SUB;
            for (j = (u16)(i + 1); j < e && in(j) != c && in(j) != ' '; j++) ;
            if (flags & want) flags &= (u8)~want;
            else if (j < e && in(j) == c && j > i + 1) flags = (u8)((flags & ~A_SCRIPT) | want);
            else put(c);
            i++;
            continue;
        }
        if (c == '<' && (is_alnum(in(i + 1)) || in(i + 1) == '/')) {
            j = find(i, e, '>');
            if (j < e) {
                if (starts(i, "<http")) {
                    for (i++; i < j; i++) put(in(i));
                } else if (starts(i, "<br")) {
                    line_break();
                } else {
                    /* <u> <s> <del> <strike> <sup> <sub>, and their ends */
                    n = in(i + 1) == '/';
                    for (k = 0; k < NTAG && !starts((u16)(i + 1 + n), tag_name[k]); k++) ;
                    if (k < NTAG) {
                        want = tag_bit[k];
                        if (n) flags &= (u8)~want;
                        else flags = (u8)((flags & ~(want & A_SCRIPT ? A_SCRIPT : 0)) | want);
                    }
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
    u8 c, fence = 0, n, q, sp, pet = 0;
    mem_put(SCRATCH_LIN + SCRATCH_MAX - 1, 0);  /* (an end, even for a file that fills it) */
    build_start();
    para_open = after_head = align = hard = flags = code = pk = base_attr = item = 0;
    while (in(i)) {
        /* the line ls..le-1 (without its CR / LF) */
        for (ls = i; (c = in(i)) != 0 && c != '\n' && c != '\r'; i++) ;
        le = i;
        if (in(i) == '\r') i++;
        if (in(i) == '\n') i++;
        j = skip(ls, ' ');
        sp = (u8)(j - ls);
        c = in(j);
        if (fence) {
            if (is_fence(j)) {
                fence = 0;
                after_head = 0;
                continue;
            }
            if (fence == 1) block_start();
            fence = 2;
            md_para((u8)(K_CODE | pet));
            code = 1;
            for (e = ls; e < le; e++) {
                c = in(e);
                if (c == '\t') puts_("    ");
                else if (pet) {
                    e += (u16)(utf8_get(e) - 1);
                    if (uv >= ' ') put(uni_pet(uv));
                } else if (c >= 0x80) e += (u16)(utf8(e) - 1);
                else if (c >= ' ') put(c);
            }
            end_para();
            continue;
        }
        if (j == le) {
            /* a blank line (between list items: the list goes on) */
            end_para();
            continue;
        }
        if (is_fence(j)) {
            end_para();
            fence = 1;
            pet = starts(skip(j, c), "petscii") ? A_PET : 0;
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
                    for (e = pstart; e < b_n; e++) atr_put(e, sty_find((u8)(kind_attr[pk] | (sty(atr_at(e)) & M_FLAGS))));
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
                md_para((u8)(n > 3 ? K_H3 : n));
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
        /* list items: a paragraph each, its level by its indent: deeper
         * than the last item's, a level in; else the level it is at */
        n = 0;
        if ((c == '-' || c == '*' || c == '+') && in(j + 1) == ' ') {
            n = 2;
        } else {
            for (e = j; (u8)(in(e) - '0') < 10; e++) ;
            if (e > j && (in(e) == '.' || in(e) == ')') && in(e + 1) == ' ') n = (u8)(e + 2 - j);
        }
        if (n) {
            end_para();
            if (!item) {
                block_start();
                lv = 0;
                lv_sp[0] = sp;
            } else if (sp > lv_sp[lv]) {
                if (lv < 3) lv++;
                lv_sp[lv] = sp;
            } else {
                while (lv && sp < lv_sp[lv]) lv--;
            }
            item = 1;
            md_para((u8)((c <= '9' && c >= '0' ? K_NUMBER : K_BULLET) | (lv << A_LEVEL_SHIFT)));
            j = skip((u16)(j + n), ' ');
        } else if (!para_open) {
            /* a new paragraph */
            block_start();
            md_para(q ? K_QUOTE : K_NORMAL);
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

/* MDIN.OVL's entry (mod_run) */
void mdin_main(void) {
    mod_ret = md_import();
}
