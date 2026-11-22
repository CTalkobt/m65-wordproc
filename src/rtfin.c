/* rtfin.c — RTF import (RTFIN.OVL, issue #6; the export is RTF.OVL, rtf.c)
 *
 * It is streamed, as files from other word processors are often larger
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
 *   \b \i \ul (\ulnone, \ul0, the other underlines), \strike (\striked),
 *   \super \sub \nosupersub (\upN \dnN: superscript, subscript), \plain,
 *   \pard
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
#include <string.h>
#include "wp.h"
#define PET_IN
#include "petuni.h"
#include "names_def.h"

#define CHUNK 1024

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
static u16 u_hi;                        /* a UTF-16 pair's first half */
static u8 pempty;                       /* nothing since the last paragraph's end */
static u8 align, pkind;                 /* the paragraph's alignment, kind */
static u8 lkind, llevel;                /* its \listtext (bullet, or a number in it), \ilvl */

/* the stylesheet: a style's number, the paragraph kind its name gives */
static int16_t s_num[NSTYLE], s_cur;
static u8 s_kind[NSTYLE], nstyle, s_len;
static char s_name[24];                 /* (lower case) */

/* the group's state (saved at {, put back at }) */
static u8 font, flags, fs, uc, dest;
/* character styles (\csN, named styles): the stylesheet's (their numbers,
 * names as they are); the one in effect (its place + 1, 0: none), the last
 * named style made of it (cs_k: its entry, for that one and style cs_a) */
#define NCS 16
static int16_t cs_num[NCS], cs_cur;
static char cs_name[NCS][NAME_LEN], s_raw[NAME_LEN];
static u8 ncs, cur_cs, cs_last, cs_a, cs_k;
static u8 depth, over;
static u8 g_font[DEPTH], g_flags[DEPTH], g_fs[DEPTH], g_uc[DEPTH], g_dest[DEPTH], g_cs[DEPTH];

/* the font table: an RTF font's number, its Writer font */
static int16_t f_num[NFONT];
static u8 f_ours[NFONT], nfont, deff_fam;
static int16_t f_cur, deff;
static u8 f_fam, f_pm;                  /* (f_pm: how much of "PETSCII" the font's name has) */

static u8 is_letter(u8 c) { return (u8)((c | 0x20) - 'a') < 26; }
static u8 is_digit(u8 c) { return (u8)(c - '0') < 10; }

static u8 rtf_font(int16_t n) {
    u8 k;
    for (k = 0; k < nfont; k++)
        if (f_num[k] == n) return f_ours[k];
    return 0;
}

/* a Unicode character of the text (\u, \'hh, a byte of the file) */
static void uchar(u16 u) {
    u8 f = fs >= 28 ? F_HEAD : font;
    b_attr = f == F_PET ? (u8)((flags & A_FX) | S_PETF) : (u8)(f | flags);
    /* (in a character style: that named style, defined by its look) */
    b_named = 0;
    if (cur_cs) {
        if (cur_cs != cs_last || b_attr != cs_a) {
            cs_last = cur_cs;
            cs_a = b_attr;
            cs_k = name_define(cs_name[cur_cs - 1], b_attr);
        }
        b_named = cs_k;
    }
    if (!(pkind & A_PET)) build_uni(u);
    else if (u >= 32) build_put(uni_pet(u));        /* (a PETSCII line's: back to PETSCII) */
    if (u >= 32) pempty = 0;
}

static void put(u8 c) { uchar(c); }

static void end_para(u8 c) {
    u8 k = pkind ? pkind : lkind;
    b_attr = (u8)((align << A_ALIGN_SHIFT) | k | ((k & A_KIND) >= K_BULLET ? llevel << A_LEVEL_SHIFT : 0));
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
        else if (s_len < sizeof s_name - 1) {
            if (s_len < NAME_LEN - 1) s_raw[s_len] = (char)c;
            s_name[s_len++] = (char)(c | (is_letter(c) ? 0x20 : 0));
        }
        return;
    }
    if (dest == D_FONTS) {
        if (c == ';' && nfont < NFONT) {
            if (f_pm == 7) f_fam = F_PET;   /* (one named PETSCII: the PETSCII font) */
            f_num[nfont] = f_cur;
            f_ours[nfont++] = f_fam;
            if (f_cur == deff) deff_fam = f_fam;
        } else {
            f_pm = c == (u8)"PETSCII"[f_pm] ? (u8)(f_pm + 1) : c == 'P';
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
    else if (has("petscii")) k = K_CODE | A_PET;
    else if (has("code") || has("preformatted") || has("source")) k = K_CODE;
    else if (has("bullet")) k = K_BULLET;
    else if (has("number")) k = K_NUMBER;
    if (k && s_cur >= 0 && nstyle < NSTYLE) {
        s_num[nstyle] = s_cur;
        s_kind[nstyle++] = k;
    }
    if (cs_cur >= 0 && ncs < NCS) {
        /* (a character style: its name as it is) */
        s_raw[s_len < NAME_LEN - 1 ? s_len : NAME_LEN - 1] = 0;
        cs_num[ncs] = cs_cur;
        memcpy(cs_name[ncs++], s_raw, NAME_LEN);
    }
    cs_cur = -1;
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
        g_cs[depth] = cur_cs;
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
    cur_cs = g_cs[depth];
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
    "strike striked super sub nosupersub up dn emdash endash lquote rquote ldblquote rdblquote bullet cs";
enum { W_PAR = 1, W_LINE, W_ROW, W_SECT, W_PAGE, W_TAB, W_CELL, W_PARD, W_PLAIN, W_QL, W_QC, W_QR, W_QJ,
       W_B, W_I, W_UL, W_ULNONE, W_ULD, W_ULW, W_ULDB, W_F, W_FS, W_U, W_UC, W_BIN, W_DEFF, W_FONTTBL,
       W_FROMAN, W_FMODERN, W_S, W_OUTLINELEVEL, W_STYLESHEET, W_LISTTEXT, W_ILVL, W_STRIKE, W_STRIKED, W_SUPER, W_SUB, W_NOSUPERSUB, W_UP, W_DN, W_EMDASH, W_ENDASH, W_LQUOTE, W_RQUOTE, W_LDBLQUOTE, W_RDBLQUOTE, W_BULLET, W_CS };
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
            f_fam = f_pm = 0;
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
        else if ((word[0] == 'c' || word[0] == 't' || word[0] == 'd') && word[1] == 's' && !word[2]) {
            s_cur = -1;
            if (word[0] == 'c') cs_cur = param;
        }
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
    case W_CS: {
        u8 k;
        cur_cs = 0;
        for (k = 0; k < ncs; k++)
            if (cs_num[k] == param) cur_cs = (u8)(k + 1);
        break;
    }
    case W_PLAIN:
        cur_cs = 0;
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
    case W_STRIKE:
    case W_STRIKED: flags = on1 ? flags | A_STRIKE : flags & (u8)~A_STRIKE; break;
    case W_SUPER: flags = (u8)((flags & ~A_SCRIPT) | A_SUP); break;
    case W_SUB: flags = (u8)((flags & ~A_SCRIPT) | A_SUB); break;
    case W_NOSUPERSUB: flags &= (u8)~A_SCRIPT; break;
    case W_UP:                          /* (raised, lowered: \up0 \dn0 neither) */
    case W_DN: flags = (u8)((flags & ~A_SCRIPT) | (on1 ? (w == W_UP ? A_SUP : A_SUB) : 0)); break;
    case W_F: font = rtf_font(param); break;
    case W_FS: fs = param > 255 ? 255 : (u8)param; break;
    case W_U: {
        /* (a UTF-16 pair: U+1FBxx, Legacy Computing, as 0xFBxx; others "?") */
        u16 v = (u16)param;
        if ((v & 0xFC00) == 0xD800) u_hi = v;
        else uchar((v & 0xFC00) == 0xDC00 ? (u_hi == 0xD83E ? (u16)(0xFB00 | (v & 0xFF)) : '?') : v);
        uskip = uc;
        break;
    }
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
    else if (c == '*' && dest != D_STYLES) dest = D_SKIP;     /* (in the stylesheet: a character style's, \*\csN) */
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
    uskip = depth = over = nfont = nstyle = s_len = ncs = cur_cs = cs_last = 0;
    cs_cur = -1;
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
/* RTFIN.OVL's entry (mod_run) */
void rtfin_main(void) {
    mod_ret = 0;
    if (mod_cmd == MOD_RTF_BEGIN) rtf_begin();
    else if (mod_cmd == MOD_RTF_CHUNK) rtf_chunk();
    else mod_ret = rtf_end();
}
