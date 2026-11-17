/* rtf.c — RTF export and import (RTF.OVL, a run-once module; issue #6)
 *
 * Export (MOD_RTF_EXPORT: the document to SCRATCH_LIN, its size; 0 when it
 * does not fit) is lossless for Writer's model:
 *   a font table      \f0 Sans (\fswiss), \f1 Serif (\froman), \f2 Mono
 *                     (\fmodern), \f3 Head (\fswiss, at \fs36; the others
 *                     \fs24)
 *   styles            \b \b0, \i \i0, \ul \ulnone, written where they change
 *   a paragraph       \pard, its kind (\sN of a stylesheet: Normal, heading
 *                     1-3 with \outlinelevel, Quote and Code with an indent,
 *                     \li, List Bullet and List Number; given in the
 *                     paragraph too, as readers want; a list item also its
 *                     \ilvl, indent and {\listtext marker\tab}), its
 *                     alignment (\ql \qc \qr \qj), its text, \par
 *   a page break      \par\page (the paragraph ends the page)
 *   \ { }             escaped with "\"; Latin-1 characters as \'hh
 *                     (\ansicpg1252: Windows-1252 has them)
 *
 * Import is streamed, as files from other word processors are often larger
 * than the scratch area: BASIC reads the file 1 KB at a time into
 * SCRATCH_LIN and calls MOD_RTF_CHUNK for each (MOD_RTF_BEGIN first,
 * MOD_RTF_END last: the document's length). The parser's state stays in
 * the module between the calls (MOD_MORE: mod_run leaves it there), and the
 * text goes straight into the document, whose length is set at the end.
 *
 * It reads control words (with a numeric parameter and the space that may
 * end it), control symbols, groups (which keep the character formatting)
 * and text:
 *   \fonttbl          each font's family: \froman Serif, \fmodern Mono,
 *                     others Sans; a size of 14 pt or more (\fs28) is Head
 *   \b \i \ul (\ulnone, \ul0, the other underlines), \plain, \pard
 *   \ql \qc \qr \qj   alignment
 *   \outlinelevelN    heading N + 1 (from 3: heading 3)
 *   \sN               the kind of the stylesheet's style N, by its name:
 *                     "heading N" (as \outlinelevel), "Title" heading 1,
 *                     one with "quot" a quote, with "code", "preformatted"
 *                     or "source" code, "bullet" / "number" a list;
 *                     others normal
 *   {\listtext ...}   a list item (a digit in it: numbered), as Word
 *                     writes it, before the \pard; \ilvlN its level
 *   \par \line \row   a paragraph's end; \sect too after text
 *   \page             a page break (ending the paragraph, or the one that
 *                     just ended)
 *   \tab \cell        a space; two after a table's cell
 *   \uN, \'hh         Latin-1 (Windows-1252 for \'hh), or ASCII for some
 *                     punctuation, else "?" (build_uni; \ucN characters
 *                     after \u skipped)
 *   \binN             N bytes of binary data skipped
 * and leaves out \colortbl, \info, pictures, headers,
 * footers, footnotes and every {\* ...} group (as the spec says a reader
 * should for those it does not know). Unknown control words are ignored.
 */
#include "wp.h"

#define CHUNK 1024

/* ==== export ==== */
#define out o_put
#define outs o_puts

static const char rtf_head[] =
    "{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fswiss Sans;}{\\f1\\froman Serif;}"
    "{\\f2\\fmodern Mono;}{\\f3\\fswiss Head;}}\r\n"
    "{\\stylesheet{\\s0 Normal;}{\\s1\\outlinelevel0 heading 1;}{\\s2\\outlinelevel1 heading 2;}"
    "{\\s3\\outlinelevel2 heading 3;}{\\s4\\li540 Quote;}{\\s5\\li360 Code;}"
    "{\\s6\\li360\\fi-360 List Bullet;}{\\s7\\li360\\fi-360 List Number;}}\r\n";
/* a paragraph kind's \sN and the rest of its properties */
static const char *const kind_rtf[KIND_TAB] = { "\\s0", "\\s1\\outlinelevel0", "\\s2\\outlinelevel1",
                                                "\\s3\\outlinelevel2", "\\s4\\li540", "\\s5\\li360", "\\s6", "\\s7" };
static const char *const q_word[4] = { "\\ql", "\\qc", "\\qr", "\\qj" };
static const char hex[] = "0123456789abcdef";

/* a number in decimal */
static void out_dec(u16 v) {
    char t[6];
    u8 k = 0;
    do {
        t[k++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (k) out((u8)t[--k]);
}

static u16 rtf_export(void) {
    u16 i;
    u8 ev, now = 0xFF, a, c, d, sp = 0; /* (now: none yet, so all written) */
    o_start();
    outs(rtf_head);
    walk_start();
    while ((ev = walk_next()) != EV_END) {
        if (ev == EV_PARA) {
            outs("\\pard");
            a = w_attr & A_KIND;
            outs(kind_rtf[a]);
            if (a >= K_BULLET) {
                /* a list item: its level (\ilvl), indent, a hanging first
                 * line, then its marker as text for readers (\listtext) */
                d = (u8)((w_attr & A_LEVEL) >> A_LEVEL_SHIFT);
                outs("\\ilvl");
                out_dec(d);
                outs("\\li");
                out_dec((u16)(d + 1) * 360);
                outs("\\fi-360");
            }
            outs(q_word[(w_attr & A_ALIGN) >> A_ALIGN_SHIFT]);
            sp = 1;                     /* the control words' ending space */
            if (a >= K_BULLET) {
                outs("{\\listtext ");
                if (a == K_BULLET) {
                    outs("\\bullet");
                } else {
                    out_dec(item_num(w_pos));
                    out('.');
                }
                outs("\\tab}");
                sp = 0;
            }
        } else if (ev == EV_RUN) {
            a = w_attr;
            if (a != now) {
                d = now == 0xFF ? 0xFF : a ^ now;
                if (d & A_FONT) {
                    outs("\\f");
                    out((u8)('0' + (a & A_FONT)));
                    outs((a & A_FONT) == F_HEAD ? "\\fs36" : "\\fs24");
                }
                if (d & A_BOLD) outs(a & A_BOLD ? "\\b" : "\\b0");
                if (d & A_ITAL) outs(a & A_ITAL ? "\\i" : "\\i0");
                if (d & A_UNDER) outs(a & A_UNDER ? "\\ul" : "\\ulnone");
                now = a;
                sp = 1;
            }
            if (sp) out(' ');
            sp = 0;
            for (i = w_pos; i < w_to; i++) {
                c = txt_at(i);
                if (c >= 0x80) {
                    /* Latin-1, as Windows-1252 has it: \'hh */
                    outs("\\'");
                    out(hex[c >> 4]);
                    c = hex[c & 15];
                } else if (c == '\\' || c == '{' || c == '}') {
                    out('\\');
                }
                out(c);
            }
        } else {
            outs(w_attr == CH_FF ? "\\par\\page\r\n" : "\\par\r\n");
        }
    }
    out('}');
    return o_end();
}

/* ==== import ==== */
enum { S_TEXT, S_ESC, S_WORD, S_PARAM, S_HEX1, S_HEX2, S_DONE };
enum { D_TEXT, D_SKIP, D_FONTS, D_STYLES, D_LIST };     /* what a group's text is (D_LIST: \listtext) */
#define NSTYLE 16
#define DEPTH 24                        /* groups whose state is kept (deeper: not) */
#define NFONT 24

static u8 st, wl, neg, has_param, hexv;
static char word[16];
static int16_t param;
static u16 binskip;                     /* \bin's bytes left */
static u8 uskip;                        /* characters left to skip after \u */
static u8 pempty;                       /* nothing since the last paragraph's end */
static u8 align, pkind;                 /* the paragraph's alignment, kind */
static u8 lkind, llevel;                /* its \listtext (bullet, or a number in it), \ilvl */

/* the stylesheet: a style's number, the paragraph kind its name gives */
static int16_t s_num[NSTYLE], s_cur;
static u8 s_kind[NSTYLE], nstyle, s_len;
static char s_name[24];                 /* (lower case) */

/* the group's state (saved at {, put back at }) */
static u8 font, flags, fs, uc, dest;
static u8 depth, over;
static u8 g_font[DEPTH], g_flags[DEPTH], g_fs[DEPTH], g_uc[DEPTH], g_dest[DEPTH];

/* the font table: an RTF font's number, its Writer font */
static int16_t f_num[NFONT];
static u8 f_ours[NFONT], nfont, deff_fam;
static int16_t f_cur, deff;
static u8 f_fam;

static u8 is_letter(u8 c) { return (u8)((c | 0x20) - 'a') < 26; }
static u8 is_digit(u8 c) { return (u8)(c - '0') < 10; }

static u8 font_of(int16_t n) {
    u8 k;
    for (k = 0; k < nfont; k++)
        if (f_num[k] == n) return f_ours[k];
    return 0;
}

/* a Unicode character of the text (\u, \'hh, a byte of the file) */
static void uchar(u16 u) {
    b_attr = (u8)((fs >= 28 ? F_HEAD : font) | flags);
    build_uni(u);
    if (u >= 32) pempty = 0;
}

static void put(u8 c) { uchar(c); }

static void end_para(u8 c) {
    u8 k = pkind ? pkind : lkind;
    b_attr = (u8)((align << A_ALIGN_SHIFT) | k | (k >= K_BULLET ? llevel << A_LEVEL_SHIFT : 0));
    build_put(c);
    pempty = 1;
    lkind = llevel = 0;
}

/* Windows-1252's $80-$9F that have an ASCII form: their Unicode */
static const u8 cp_byte[] = { 0x85, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97 };
static const u16 cp_uni[] = { 0x2026, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014 };

static void style_end(void);

/* a character of the text (c: a byte of the file, or a \'hh) */
static void text(u8 c, u8 hex) {
    u8 k;
    if (uskip) {
        uskip--;
        return;
    }
    if (dest == D_LIST) {
        if (is_digit(c)) lkind = K_NUMBER;
        return;
    }
    if (dest == D_STYLES) {
        if (c == ';') style_end();
        else if (s_len < sizeof s_name - 1) s_name[s_len++] = (char)(c | (is_letter(c) ? 0x20 : 0));
        return;
    }
    if (dest == D_FONTS) {
        if (c == ';' && nfont < NFONT) {
            f_num[nfont] = f_cur;
            f_ours[nfont++] = f_fam;
            if (f_cur == deff) deff_fam = f_fam;
        }
        return;
    }
    if (dest != D_TEXT) return;
    if (!hex && c < 128) {
        put(c);
        return;
    }
    if (c >= 0x80 && c < 0xA0) {
        for (k = 0; k < sizeof cp_byte; k++)
            if (cp_byte[k] == c) {
                uchar(cp_uni[k]);
                return;
            }
        put('?');
        return;
    }
    uchar(c);
}

/* does the style's name have s in it? */
static u8 has(const char *s) {
    u8 i, k;
    for (i = 0; i < s_len; i++) {
        for (k = 0; s[k] && s[k] == s_name[i + k]; k++) ;
        if (!s[k]) return 1;
    }
    return 0;
}

/* a heading's kind from its outline level (0: heading 1) */
static u8 level_kind(int16_t n) { return n < 0 ? K_NORMAL : n >= 2 ? K_H3 : (u8)(K_H1 + n); }

/* the stylesheet's style s_cur ended (its name in s_name): its kind */
static void style_end(void) {
    u8 k = pkind;
    s_name[s_len] = 0;
    if (has("heading ")) k = level_kind(s_name[8] - '1');
    else if (has("title") && !has("sub")) k = K_H1;
    else if (has("quot")) k = K_QUOTE;
    else if (has("code") || has("preformatted") || has("source")) k = K_CODE;
    else if (has("bullet")) k = K_BULLET;
    else if (has("number")) k = K_NUMBER;
    if (k && s_cur >= 0 && nstyle < NSTYLE) {
        s_num[nstyle] = s_cur;
        s_kind[nstyle++] = k;
    }
    s_len = 0;
    s_cur = -1;
    pkind = 0;
}

static void push(void) {
    if (depth < DEPTH) {
        g_font[depth] = font;
        g_flags[depth] = flags;
        g_fs[depth] = fs;
        g_uc[depth] = uc;
        g_dest[depth] = dest;
        depth++;
    } else {
        over++;
    }
}

static void pop(void) {
    uskip = 0;
    if (over) {
        over--;
        return;
    }
    if (!depth) {
        st = S_DONE;                    /* (past the document's last }) */
        return;
    }
    depth--;
    font = g_font[depth];
    flags = g_flags[depth];
    fs = g_fs[depth];
    uc = g_uc[depth];
    dest = g_dest[depth];
    if (!depth) st = S_DONE;
}

/* the index of word in list (words separated by spaces), from 1; 0: none */
static u8 lookup(const char *list) {
    u8 n = 1, k;
    while (*list) {
        for (k = 0; list[k] && list[k] != ' ' && list[k] == word[k]; k++) ;
        if ((!list[k] || list[k] == ' ') && !word[k]) return n;
        while (*list && *list != ' ') list++;
        if (*list) list++;
        n++;
    }
    return 0;
}

/* the destinations left out */
static const char skips[] =
    "colortbl info pict nonshppict header headerl headerr headerf footer footerl footerr "
    "footerf footnote object fldinst themedata listtable listoverridetable rsidtbl xe tc";
/* the control words read (W_...: their place in the list) */
static const char words[] =
    "par line row sect page tab cell pard plain ql qc qr qj b i ul ulnone uld ulw uldb "
    "f fs u uc bin deff fonttbl froman fmodern s outlinelevel stylesheet listtext ilvl "
    "emdash endash lquote rquote ldblquote rdblquote bullet";
enum { W_PAR = 1, W_LINE, W_ROW, W_SECT, W_PAGE, W_TAB, W_CELL, W_PARD, W_PLAIN, W_QL, W_QC, W_QR, W_QJ,
       W_B, W_I, W_UL, W_ULNONE, W_ULD, W_ULW, W_ULDB, W_F, W_FS, W_U, W_UC, W_BIN, W_DEFF, W_FONTTBL,
       W_FROMAN, W_FMODERN, W_S, W_OUTLINELEVEL, W_STYLESHEET, W_LISTTEXT, W_ILVL, W_EMDASH, W_ENDASH, W_LQUOTE, W_RQUOTE, W_LDBLQUOTE, W_RDBLQUOTE, W_BULLET };
static const u16 named[] = { 0x2014, 0x2013, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022 };

static void ctrl_word(void) {
    u8 w = lookup(words), on1 = !has_param || param != 0, k;
    if (w == W_BIN) {
        binskip = has_param ? (u16)param : 0;
        return;
    }
    if (dest == D_SKIP || dest == D_LIST) return;
    if (dest == D_FONTS) {
        if (w == W_F) {
            f_cur = param;
            f_fam = 0;
        } else if (w == W_FROMAN) {
            f_fam = F_SERIF;
        } else if (w == W_FMODERN) {
            f_fam = F_MONO;
        }
        return;
    }
    if (dest == D_STYLES) {
        /* a style's number (a character or table style's: none), its level */
        if (w == W_S) s_cur = param;
        else if ((word[0] == 'c' || word[0] == 't' || word[0] == 'd') && word[1] == 's' && !word[2]) s_cur = -1;
        else if (w == W_OUTLINELEVEL) pkind = level_kind(param);
        return;
    }
    if (depth >= 2 && lookup(skips)) {
        dest = D_SKIP;
        return;
    }
    switch (w) {
    case W_SECT:
        if (pempty) break;
        /* fall through */
    case W_PAR:
    case W_LINE:
    case W_ROW: end_para(CH_CR); break;
    case W_PAGE:
        if (pempty && b_n && txt_at(b_n - 1) == CH_CR) txt_put(b_n - 1, CH_FF);
        else end_para(CH_FF);
        break;
    case W_CELL: put(' '); /* fall through */
    case W_TAB: put(' '); break;
    case W_PARD: align = pkind = 0; break;
    case W_S:
        pkind = 0;
        for (k = 0; k < nstyle; k++)
            if (s_num[k] == param) pkind = s_kind[k];
        break;
    case W_OUTLINELEVEL: pkind = level_kind(param); break;
    case W_LISTTEXT:
        dest = D_LIST;
        lkind = K_BULLET;
        break;
    case W_ILVL: llevel = param > 3 ? 3 : (u8)param; break;
    case W_STYLESHEET:
        dest = D_STYLES;
        s_len = 0;
        s_cur = -1;
        break;
    case W_PLAIN:
        font = deff_fam;
        flags = 0;
        fs = 24;
        break;
    case W_QL: align = 0; break;
    case W_QC: align = 1; break;
    case W_QR: align = 2; break;
    case W_QJ: align = 3; break;
    case W_B: flags = on1 ? flags | A_BOLD : flags & (u8)~A_BOLD; break;
    case W_I: flags = on1 ? flags | A_ITAL : flags & (u8)~A_ITAL; break;
    case W_UL:
    case W_ULD:
    case W_ULW:
    case W_ULDB: flags = on1 ? flags | A_UNDER : flags & (u8)~A_UNDER; break;
    case W_ULNONE: flags &= (u8)~A_UNDER; break;
    case W_F: font = font_of(param); break;
    case W_FS: fs = param > 255 ? 255 : (u8)param; break;
    case W_U:
        uchar((u16)param);
        uskip = uc;
        break;
    case W_UC: uc = (u8)param; break;
    case W_DEFF:
        deff = param;
        break;
    case W_FONTTBL: dest = D_FONTS; break;
    default:
        if (w >= W_EMDASH) uchar(named[w - W_EMDASH]);
    }
}

static void symbol(u8 c) {
    if (c == '\\' || c == '{' || c == '}') text(c, 0);
    else if (c == '~') text(' ', 0);
    else if (c == '_') text('-', 0);
    else if (c == '*') dest = D_SKIP;
    else if ((c == '\r' || c == '\n') && dest == D_TEXT) end_para(CH_CR);
}

static u8 hexval(u8 c) {
    if (is_digit(c)) return (u8)(c - '0');
    return (u8)(((c | 0x20) - 'a' + 10) & 15);
}

/* one byte of the file; 1: it is to be read again (it ended a control word) */
static u8 feed(u8 c) {
    if (binskip) {
        binskip--;
        return 0;
    }
    switch (st) {
    case S_TEXT:
        if (c == '\\') st = S_ESC;
        else if (c == '{') push();
        else if (c == '}') pop();
        else if (c != '\r' && c != '\n') text(c, 0);
        return 0;
    case S_ESC:
        if (is_letter(c)) {
            word[0] = (char)c;
            wl = 1;
            has_param = neg = 0;
            param = 0;
            st = S_WORD;
        } else if (c == '\'') {
            st = S_HEX1;
        } else {
            st = S_TEXT;
            symbol(c);
        }
        return 0;
    case S_WORD:
        if (is_letter(c)) {
            if (wl < sizeof word - 1) word[wl++] = (char)c;
            return 0;
        }
        word[wl] = 0;
        if (c == '-' || is_digit(c)) {
            if (c == '-') {
                neg = 1;
            } else {
                param = (int16_t)(c - '0');
                has_param = 1;
            }
            st = S_PARAM;
            return 0;
        }
        break;
    case S_PARAM:
        if (is_digit(c)) {
            param = (int16_t)(param * 10 + (c - '0'));
            has_param = 1;
            return 0;
        }
        if (neg) param = (int16_t)-param;
        break;
    case S_HEX1:
        hexv = (u8)(hexval(c) << 4);
        st = S_HEX2;
        return 0;
    case S_HEX2:
        st = S_TEXT;
        text((u8)(hexv | hexval(c)), 1);
        return 0;
    default:
        return 0;                       /* (S_DONE) */
    }
    /* a control word's end */
    st = S_TEXT;
    ctrl_word();
    return c != ' ';
}

static void rtf_begin(void) {
    st = S_TEXT;
    build_start();
    binskip = 0;
    uskip = depth = over = nfont = nstyle = s_len = 0;
    pempty = 1;
    align = font = flags = deff_fam = pkind = lkind = llevel = 0;
    deff = 0;
    fs = 24;
    uc = 1;
    dest = D_TEXT;
}

static void rtf_chunk(void) {
    u16 i;
    u8 c;
    for (i = 0; i < CHUNK; i++) {
        c = mem_get(SCRATCH_LIN + i);
        while (feed(c)) ;
    }
}

static u16 rtf_end(void) {
    if (!pempty) end_para(CH_CR);
    return build_end();
}

/* RTF.OVL's entry (mod_run) */
void rtf_main(void) {
    mod_ret = 0;
    if (mod_cmd == MOD_RTF_EXPORT) mod_ret = rtf_export();
    else if (mod_cmd == MOD_RTF_BEGIN) rtf_begin();
    else if (mod_cmd == MOD_RTF_CHUNK) rtf_chunk();
    else mod_ret = rtf_end();
}
