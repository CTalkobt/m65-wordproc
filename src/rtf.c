/* rtf.c — RTF export and import (RTF.OVL, a run-once module; issue #6)
 *
 * Export (MOD_RTF_EXPORT: the document to SCRATCH_LIN, its size; 0 when it
 * does not fit) is lossless for Writer's model:
 *   a font table      \f0 Sans (\fswiss), \f1 Serif (\froman), \f2 Mono
 *                     (\fmodern), \f3 Head (\fswiss, at \fs36; the others
 *                     \fs24)
 *   styles            \b \b0, \i \i0, \ul \ulnone, written where they change
 *   a paragraph       \pard, its alignment (\ql \qc \qr \qj), its text, \par
 *   a page break      \par\page (the paragraph ends the page)
 *   \ { }             escaped with "\"
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
 *   \par \line \row   a paragraph's end; \sect too after text
 *   \page             a page break (ending the paragraph, or the one that
 *                     just ended)
 *   \tab \cell        a space; two after a table's cell
 *   \uN, \'hh         as ASCII where there is one (Windows-1252 for \'hh;
 *                     \ucN characters after \u skipped), else "?"
 *   \binN             N bytes of binary data skipped
 * and leaves out \colortbl, \stylesheet, \info, pictures, headers,
 * footers, footnotes and every {\* ...} group (as the spec says a reader
 * should for those it does not know). Unknown control words are ignored.
 */
#include "wp.h"

#define F_SERIF 1
#define F_MONO 2
#define F_HEAD 3
#define RTF_MAX 30720u                  /* the scratch area */
#define CHUNK 1024

static u8 is_break(u8 c) { return c == CH_CR || c == CH_FF; }

/* ==== export ==== */
static u16 on;                          /* bytes written */

static void out(u8 c) {
    if (on < RTF_MAX) mem_put(SCRATCH_LIN + on, c);
    on++;
}

static void outs(const char *s) {
    while (*s) out((u8)*s++);
}

static const char rtf_head[] =
    "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fswiss Sans;}{\\f1\\froman Serif;}"
    "{\\f2\\fmodern Mono;}{\\f3\\fswiss Head;}}\r\n";
static const char *const q_word[4] = { "\\ql", "\\qc", "\\qr", "\\qj" };

static u16 rtf_export(void) {
    u16 i = 0, pe;
    u8 now = 0xFF, a, c, d, sp;         /* (now: none yet, so all written) */
    on = 0;
    outs(rtf_head);
    while (i < doc_len) {
        for (pe = i; !is_break(TXT[pe]); pe++) ;
        outs("\\pard");
        outs(q_word[(ATR[pe] & A_ALIGN) >> A_ALIGN_SHIFT]);
        sp = 1;                         /* the control words' ending space */
        for (; i < pe; i++) {
            a = ATR[i] & A_STYLE;
            c = TXT[i];
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
            if (c == '\\' || c == '{' || c == '}') out('\\');
            out(c);
        }
        outs(TXT[pe] == CH_FF ? "\\par\\page\r\n" : "\\par\r\n");
        i = (u16)(pe + 1);
    }
    out('}');
    return on <= RTF_MAX ? on : 0;
}

/* ==== import ==== */
enum { S_TEXT, S_ESC, S_WORD, S_PARAM, S_HEX1, S_HEX2, S_DONE };
enum { D_TEXT, D_SKIP, D_FONTS };      /* what a group's text is */
#define DEPTH 24                        /* groups whose state is kept (deeper: not) */
#define NFONT 24

static u8 st, wl, neg, has_param, hexv;
static char word[16];
static int16_t param;
static u16 rn;                          /* the document's characters so far */
static u16 binskip;                     /* \bin's bytes left */
static u8 uskip;                        /* characters left to skip after \u */
static u8 pempty;                       /* nothing since the last paragraph's end */
static u8 align;

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

static void put(u8 c) {
    if (rn >= DOC_MAX - 1) return;      /* (room kept for the final CR) */
    TXT[rn] = c;
    ATR[rn] = (u8)((fs >= 28 ? F_HEAD : font) | flags);
    rn++;
    pempty = 0;
}

static void end_para(u8 c) {
    if (rn >= DOC_MAX - 1) return;
    TXT[rn] = c;
    ATR[rn] = (u8)(align << A_ALIGN_SHIFT);
    rn++;
    pempty = 1;
}

/* Unicode (\u, and \'hh by way of Windows-1252) as ASCII */
static const u16 uni[] = { 0xA0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2013, 0x2014, 0x2026, 0x2022, 0xDF, 0xA9 };
static const char *const uni_as[] = { " ", "'", "'", "\"", "\"", "-", "--", "...", "*", "ss", "(c)" };
static const char latin[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
/* Windows-1252's $80-$9F that have an ASCII form: their Unicode */
static const u8 cp_byte[] = { 0x85, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97 };
static const u16 cp_uni[] = { 0x2026, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014 };

static void uchar(u16 u) {
    u8 k;
    const char *s;
    if (u < 128) {
        if (u >= 32) put((u8)u);
        return;
    }
    if (u >= 0xC0 && u <= 0xFF && u != 0xDF) {
        put((u8)latin[u - 0xC0]);
        return;
    }
    for (k = 0; k < sizeof uni / sizeof uni[0]; k++)
        if (uni[k] == u) {
            for (s = uni_as[k]; *s; s++) put((u8)*s);
            return;
        }
    put('?');
}

/* a character of the text (c: a byte of the file, or a \'hh) */
static void text(u8 c, u8 hex) {
    u8 k;
    if (uskip) {
        uskip--;
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
        if (c >= 32) put(c);
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
    "colortbl stylesheet info pict nonshppict header headerl headerr headerf footer footerl footerr "
    "footerf footnote object fldinst themedata listtable listoverridetable rsidtbl xe tc";
/* the control words read (W_...: their place in the list) */
static const char words[] =
    "par line row sect page tab cell pard plain ql qc qr qj b i ul ulnone uld ulw uldb "
    "f fs u uc bin deff fonttbl froman fmodern emdash endash lquote rquote ldblquote rdblquote bullet";
enum { W_PAR = 1, W_LINE, W_ROW, W_SECT, W_PAGE, W_TAB, W_CELL, W_PARD, W_PLAIN, W_QL, W_QC, W_QR, W_QJ,
       W_B, W_I, W_UL, W_ULNONE, W_ULD, W_ULW, W_ULDB, W_F, W_FS, W_U, W_UC, W_BIN, W_DEFF, W_FONTTBL,
       W_FROMAN, W_FMODERN, W_EMDASH, W_ENDASH, W_LQUOTE, W_RQUOTE, W_LDBLQUOTE, W_RDBLQUOTE, W_BULLET };
static const u16 named[] = { 0x2014, 0x2013, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022 };

static void ctrl_word(void) {
    u8 w = lookup(words), on1 = !has_param || param != 0;
    if (w == W_BIN) {
        binskip = has_param ? (u16)param : 0;
        return;
    }
    if (dest == D_SKIP) return;
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
        if (pempty && rn && TXT[rn - 1] == CH_CR) TXT[rn - 1] = CH_FF;
        else end_para(CH_FF);
        break;
    case W_CELL: put(' '); /* fall through */
    case W_TAB: put(' '); break;
    case W_PARD: align = 0; break;
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
    rn = binskip = 0;
    uskip = depth = over = nfont = 0;
    pempty = 1;
    align = font = flags = deff_fam = 0;
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
    doc_len = rn;
    doc_loaded(1);
    return (u16)(doc_len - 1);
}

/* RTF.OVL's entry (mod_run) */
void rtf_main(void) {
    mod_ret = 0;
    if (mod_cmd == MOD_RTF_EXPORT) mod_ret = rtf_export();
    else if (mod_cmd == MOD_RTF_BEGIN) rtf_begin();
    else if (mod_cmd == MOD_RTF_CHUNK) rtf_chunk();
    else mod_ret = rtf_end();
}
