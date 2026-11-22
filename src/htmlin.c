/* htmlin.c — HTML import (UTF-8, or Latin-1 where it is not): the module
 * HTMLIN.OVL (issue #10; the export is HTML.OVL, html.c)
 *
 * Streamed as RTF's import (rtf.c): BASIC reads the file 1 KB at a time
 * into SCRATCH_LIN and calls MOD_HTML_CHUNK for each (MOD_HTML_BEGIN first,
 * MOD_HTML_END last: the document's length); the parser's state stays here
 * between the calls, and the text goes straight into the document.
 *
 * It reads what the export writes, and the tags around it:
 *   blocks       <p> <div> and the other block elements (section, table,
 *                ...): paragraphs, an empty one between blocks (not after
 *                a heading or a page break, nor between a list's items:
 *                as the Markdown import); <br> ends a paragraph
 *   kinds        <h1>-<h3> (<h4>-<h6>: heading 3), <blockquote>, <pre>
 *                (code; its lines are paragraphs; class="petscii": a
 *                PETSCII line's, its characters back to PETSCII:
 *                petuni.h), <ul> / <ol> and <li>
 *                (nested: the level, up to 4)
 *   styles       <b> <strong>, <i> <em> <cite> <var> <dfn>, <u> <ins>,
 *                <code> <tt> <kbd> <samp> (Mono), <s> <strike> <del>,
 *                <sup>, <sub>, <span class="s|r|h|p">
 *                (the export's Sans, Serif, Head, PETSCII)
 *   alignment    style="text-align: ...", align="...", <center>
 *   page break   style="page-break-after: always" (or break-before / -after)
 *   tables       a row a paragraph, two spaces between cells
 *   left out     <title> <script> <style> <svg> <template> (to their end
 *                tag), comments, <!...> and <?...>; other tags (their text
 *                kept)
 *   white space  a run of it with a line end: one space (spaces alone are
 *                kept, as the export writes them); none at a paragraph's
 *                start or end; in <pre> as it is
 *   characters   &name; (the common ones and Latin-1's letters), &#N;,
 *                &#xH; and UTF-8 (build_uni: Latin-1, or ASCII for some);
 *                a byte that is not UTF-8 as Latin-1
 */
#include "wp.h"
#define PET_IN
#include "petuni.h"

enum {
    S_TEXT, S_LT, S_NAME, S_ATTR, S_ANAME, S_AEQ, S_AVAL0, S_AVAL, S_ENT, S_BANG, S_COMMENT, S_DECL, S_RAW
};
static u8 st;
static u8 is_letter(u8 c) { return (u8)((c | 0x20) - 'a') < 26; }
static u8 is_digit(u8 c) { return (u8)(c - '0') < 10; }
static u8 is_space(u8 c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f'; }
static u8 lower(u8 c) { return is_letter(c) ? (u8)(c | 0x20) : c; }

/* the tag: its name (lower case), a closing one; an attribute's name and
 * value (lower case; quoted with q, 0: not); what the attributes say */
static char nm[12], an[8], av[64], en[10];
static u8 nl, closing, anl, avl, q, enl, dash;
static u8 t_align, t_pb, t_font, t_pet;
#define NONE 0xFF

/* the text's context */
static u8 popen, want_gap, no_gap;      /* a paragraph started; an empty one due before the next; not */
static u8 pk, pal;                      /* the paragraph's kind (and level), alignment */
static u8 hk, nq, npre, pre_first;      /* in a heading (its kind); quotes, pre open; just after <pre> */
static u8 pet;                          /* the <pre> a PETSCII line's (A_PET) */
static u8 ld, lt[4];                    /* lists open; each one's kind (K_BULLET / K_NUMBER) */
static u8 n_on[7];                      /* <b> <i> <u> <code> <s> <sup> <sub> (and the like) open */
static u8 scr;                          /* the script of the innermost <sup> / <sub> open */
static u8 sd, sfont[8];                 /* <span>s open; each one's font (NONE: the kind's) */
static u8 sp_n, sp_nl, sp_attr;         /* spaces held (to a paragraph's end: dropped); a line end among them; their style */
static u8 u_need, u_lead;               /* UTF-8: continuation bytes due; the first byte */
static u16 u_val;

/* the style of the text now: the paragraph kind's, with what is open */
static u8 attr(void) {
    u8 b = kind_attr[pk & A_KIND], f = b & A_FONT;
    if (n_on[3]) f = F_MONO;
    else if (sd && sfont[sd - 1] != NONE) f = sfont[sd - 1];
    if (n_on[0]) b |= A_BOLD;
    if (n_on[1]) b |= A_ITAL;
    if (n_on[2]) b |= A_UNDER;
    if (n_on[4]) b |= A_STRIKE;
    if (f == F_PET) return (u8)((b & A_FX) | S_PETF);
    b |= scr;
    return (u8)((b & (u8)~A_FONT) | f);
}

/* a paragraph starts (if none has): the empty one due before it, its kind */
static void begin(void) {
    u8 lv;
    if (popen) return;
    if (want_gap && b_n && !no_gap) {
        b_attr = 0;
        build_put(CH_CR);
    }
    want_gap = no_gap = 0;
    if (npre) pk = (u8)(K_CODE | pet);
    else if (hk) pk = hk;
    else if (ld) {
        lv = ld > 4 ? 3 : (u8)(ld - 1);
        pk = (u8)(lt[lv] | (lv << A_LEVEL_SHIFT));
    } else pk = nq ? K_QUOTE : K_NORMAL;
    popen = 1;
}

static void end_para(u8 c) {
    begin();
    b_attr = (u8)(((pk & A_KIND) >= K_CODE ? 0 : pal << A_ALIGN_SHIFT) | pk);
    build_put(c);
    popen = sp_n = sp_nl = 0;
}

/* a block's edge: the paragraph ends; gap: an empty one before the next */
static void brk(u8 gap) {
    if (popen) end_para(CH_CR);
    if (gap) want_gap = 1;
}

/* a character of the text (the spaces held before it) */
static void ch(u16 u) {
    pre_first = 0;              /* (a UTF-8 one too: a line end after it counts) */
    begin();
    if (sp_n) {
        b_attr = sp_attr;
        if (sp_nl) sp_n = 1;
        while (sp_n) {
            build_put(' ');
            sp_n--;
        }
        sp_nl = 0;
    }
    b_attr = attr();
    if (pk & A_PET) build_put(uni_pet(u));
    else build_uni(u);
}

/* a byte of the text: white space (as it is in <pre>), UTF-8; a broken
 * sequence's bytes as Latin-1 */
static void text_byte(u8 c) {
    if (u_need) {
        if ((c & 0xC0) == 0x80) {
            u_val = (u16)((u_val << 6) | (c & 0x3F));
            if (!--u_need) ch(u_lead > 0xF0 || (u_lead == 0xF0 && (u_val >> 8) != 0xFB) ? '?' : u_val);
            return;
        }
        u_need = 0;
        ch(u_lead);
    }
    if (c >= 0xC0) {
        u_lead = c;
        u_need = c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
        u_val = c & (c < 0xE0 ? 0x1F : 0x0F);
        return;
    }
    if (npre) {
        if (c == '\r') return;
        if (c == '\n') {
            if (!pre_first) end_para(CH_CR);
            pre_first = 0;
            return;
        }
        pre_first = 0;
        ch(c == '\t' ? ' ' : c);
        return;
    }
    if (is_space(c)) {
        /* (a run with a line end: one space; spaces alone: as they are) */
        if (!popen) return;
        if (c != ' ') sp_nl = 1;
        if (!sp_n) sp_attr = attr();
        if (sp_n < 255) sp_n++;
        return;
    }
    ch(c);
}

/* ---- the tags ---- */
/* which word of w (each ended by a space) s is: 1, 2, ...; 0: none */
static u8 word_at(const char *w, const char *s) {
    u8 k = 1, i;
    while (*w) {
        for (i = 0; w[i] == s[i] && w[i] != ' '; i++) ;
        if (w[i] == ' ' && !s[i]) return k;
        while (*w++ != ' ') ;
        k++;
    }
    return 0;
}

static const char tags[] =
    "p h1 h2 h3 h4 h5 h6 blockquote pre ul ol li br b strong i em cite var dfn u ins code tt kbd samp "
    "s strike del sup sub span "
    "title script style svg template td th tr "
    "div center table hr dl dt dd section article header footer nav main aside figure figcaption address form "
    "fieldset details summary caption ";
enum {
    T_P, T_H1, T_H6 = T_H1 + 5, T_QUOTE, T_PRE, T_UL, T_OL, T_LI, T_BR, T_BOLD, T_ITAL = T_BOLD + 2,
    T_UNDER = T_ITAL + 5, T_MONO = T_UNDER + 2, T_STRIKE = T_MONO + 4, T_SUP = T_STRIKE + 3, T_SUB, T_SPAN, T_SKIP, T_CELL = T_SKIP + 5, T_TR = T_CELL + 2,
    T_DIV, T_CENTER, T_NONE = 0xFF
};
/* (each style tag group's end: bold, italic, underline, Mono, strike, sup, sub) */
static const u8 grp_end[7] = { T_ITAL, T_UNDER, T_MONO, T_STRIKE, T_SUP, T_SUB, T_SPAN };

/* is s in the attribute's value? */
static u8 has(const char *s) {
    u8 i, k;
    for (i = 0; av[i]; i++) {
        for (k = 0; s[k] && av[i + k] == s[k]; k++) ;
        if (!s[k]) return 1;
    }
    return 0;
}

static u8 align_of(void) { return has("center") ? 1 : has("right") ? 2 : has("justify") ? 3 : 0; }

/* an attribute read: style (text-align, a page break), align, class (a
 * <span>'s font: the export's s, r, h; a <pre>'s petscii) */
static void attr_done(void) {
    u8 k = word_at("style align class ", an);
    av[avl] = 0;
    if (k == 1) {
        if (has("text-align")) t_align = align_of();
        if ((has("break-after") || has("break-before")) && (has("always") || has("page"))) t_pb = 1;
    } else if (k == 2) {
        t_align = align_of();
    } else if (k == 3) {
        if (has("petscii")) t_pet = 1;
        if (avl == 1) t_font = av[0] == 's' ? F_SANS : av[0] == 'r' ? F_SERIF : av[0] == 'h' ? F_HEAD : av[0] == 'p' ? F_PET : NONE;
    }
}

/* a tag read (nm, closing, the attributes') */
static void tag_end(void) {
    u8 t = (u8)(word_at(tags, nm) - 1), k;    /* (T_NONE: not one) */
    st = S_TEXT;
    if (t >= T_SKIP && t < T_CELL) {
        /* title, script, style, svg, template: left out, to their end tag */
        if (!closing) {
            st = S_RAW;
            dash = 0;
        }
        return;
    }
    if (t == T_BR) {
        end_para(CH_CR);
        return;
    }
    if (t >= T_BOLD && t < T_SPAN) {
        for (k = 0; t >= grp_end[k]; k++) ;
        if (!closing) n_on[k]++;
        else if (n_on[k]) n_on[k]--;
        if (k >= 5) scr = !closing ? (k == 5 ? A_SUP : A_SUB) : n_on[5] ? A_SUP : n_on[6] ? A_SUB : 0;
        return;
    }
    if (t == T_SPAN) {
        if (closing) {
            if (sd) sd--;
        } else if (sd < sizeof sfont) {
            sfont[sd] = t_font != NONE ? t_font : sd ? sfont[sd - 1] : NONE;
            sd++;
        }
        return;
    }
    if (t >= T_CELL && t < T_TR) {
        /* (a table's cells: two spaces after one) */
        if (!closing && popen) {
            if (!sp_n) sp_attr = attr();
            sp_n = 2;
            sp_nl = 0;
        }
        return;
    }
    if (t == T_NONE) return;
    /* the blocks */
    if (t == T_TR || t == T_LI) {
        brk(0);
    } else if (t == T_UL || t == T_OL) {
        brk(!ld);
        if (closing) {
            if (ld && !--ld) want_gap = 1;
        } else {
            if (ld < 4) lt[ld] = t == T_UL ? K_BULLET : K_NUMBER;
            if (ld < 255) ld++;
        }
        return;
    } else {
        brk(!ld);
    }
    if (t >= T_H1 && t <= T_H6) {
        hk = closing ? 0 : t < T_H1 + 3 ? (u8)(K_H1 + t - T_H1) : K_H3;
        if (closing) no_gap = 1;
    } else if (t == T_QUOTE) {
        if (!closing) nq++;
        else if (nq) nq--;
    } else if (t == T_PRE) {
        if (!closing) {
            if (!npre++) pet = t_pet ? A_PET : 0;
            pre_first = 1;
        } else if (npre) npre--;
    }
    if (closing) {
        pal = 0;
        return;
    }
    if (t == T_CENTER) pal = 1;
    else if (t_align != NONE) pal = t_align;
    if (t_pb) {
        /* a page break: an empty paragraph ended by FF */
        if (b_n) {
            b_attr = 0;
            build_put(CH_FF);
        }
        want_gap = 0;
        no_gap = 1;
    }
}

/* ---- entities: &name; &#N; &#xH; ---- */
static const char ent_names[] =
    "amp lt gt quot apos nbsp copy reg deg middot laquo raquo shy hellip mdash ndash lsquo rsquo ldquo rdquo bull "
    "euro times divide pound sect para plusmn iquest iexcl frac12 ";
static const u16 ent_uni[] = { '&',    '<',    '>',    '"',    '\'',   0xA0,   0xA9,   0xAE,   0xB0,   0xB7,
                               0xAB,   0xBB,   0xAD,   0x2026, 0x2014, 0x2013, 0x2018, 0x2019, 0x201C, 0x201D,
                               0x2022, 0x20AC, 0xD7,   0xF7,   0xA3,   0xA7,   0xB6,   0xB1,   0xBF,   0xA1,
                               0xBD };
/* the Latin-1 letters $C0-$DF (and $E0-$FF): their accent's name, as the
 * entity has it after the letter (eacute: e + acute) */
static const char accents[] = "grave acute circ tilde uml ring elig cedil slash zlig ";
static const char acc_of[] = "\1\2\3\4\5\6\7\10\1\2\3\5\1\2\3\5\0\4\1\2\3\4\5\0\11\1\2\3\5\2\0\12";

/* the entity in en (enl long): its character; 0: not one */
static u16 entity(void) {
    u16 v = 0;
    u8 i, c, a;
    en[enl] = 0;
    if (en[0] == '#') {
        if ((en[1] | 0x20) == 'x') {
            for (i = 2; en[i]; i++) {
                c = (u8)en[i];
                v = (u16)(v * 16 + (is_digit(c) ? c - '0' : (c | 0x20) - 'a' + 10));
            }
        } else {
            for (i = 1; en[i]; i++) v = (u16)(v * 10 + (en[i] - '0'));
        }
        return v ? v : '?';
    }
    if ((i = word_at(ent_names, en)) != 0) return ent_uni[i - 1];
    if (enl > 2) {
        /* a letter with an accent */
        en[1] = (char)lower((u8)en[1]);
        a = word_at(accents, en + 1);
        if (a)
            for (v = 0xC0; v <= 0xFF; v++)
                if (as_ascii((u8)v) == (u8)en[0] && (v == 0xFF ? 5 : acc_of[v & 0x1F]) == a) return v;
    }
    return 0;
}

/* an entity's end (c: what ended it); not one: as it was written */
static void ent_end(u8 c) {
    u16 u = enl ? entity() : 0;
    u8 i;
    if (u) {
        if (u < 32) u = ' ';
        ch(u);
        return;
    }
    ch('&');
    for (i = 0; i < enl; i++) ch((u8)en[i]);
    if (c == ';') ch(';');
}

/* one byte of the file; 1: it is to be read again */
static u8 feed(u8 c) {
    switch (st) {
    case S_TEXT:
        if (c == '<') {
            st = S_LT;
            closing = 0;
        } else if (c == '&') {
            st = S_ENT;
            enl = 0;
        } else text_byte(c);
        return 0;
    case S_LT:
        if (is_letter(c)) {
            nm[0] = (char)lower(c);
            nl = 1;
            st = S_NAME;
        } else if (closing) {
            st = S_DECL;                /* (</ and no name) */
        } else if (c == '/') {
            closing = 1;
        } else if (c == '!') {
            st = S_BANG;
            dash = 0;
        } else if (c == '?') {
            st = S_DECL;
        } else {
            st = S_TEXT;                /* (a < in the text) */
            text_byte('<');
            return 1;
        }
        return 0;
    case S_NAME:
        if (is_letter(c) || is_digit(c)) {
            if (nl < sizeof nm - 1) nm[nl++] = (char)lower(c);
            return 0;
        }
        nm[nl] = 0;
        t_align = t_font = NONE;
        t_pb = t_pet = 0;
        st = S_ATTR;
        return 1;
    case S_ATTR:
        if (c == '>') tag_end();
        else if (!is_space(c) && c != '/') {
            an[0] = (char)lower(c);
            anl = 1;
            st = S_ANAME;
        }
        return 0;
    case S_ANAME:
        if (!is_space(c) && c != '=' && c != '>' && c != '/') {
            if (anl < sizeof an - 1) an[anl++] = (char)lower(c);
            return 0;
        }
        an[anl] = 0;
        st = S_AEQ;
        return 1;
    case S_AEQ:
        if (c == '=') st = S_AVAL0;
        else if (!is_space(c)) {
            st = S_ATTR;                /* (no value) */
            return 1;
        }
        return 0;
    case S_AVAL0:
        if (is_space(c)) return 0;
        avl = 0;
        st = S_AVAL;
        if (c == '"' || c == '\'') {
            q = c;
            return 0;
        }
        q = 0;
        return 1;
    case S_AVAL:
        if (q ? c == q : is_space(c) || c == '>') {
            attr_done();
            st = S_ATTR;
            return !q;
        }
        if (avl < sizeof av - 1) av[avl++] = (char)lower(c);
        return 0;
    case S_ENT:
        if ((is_letter(c) || is_digit(c) || (c == '#' && !enl)) && enl < sizeof en - 1) {
            en[enl++] = (char)c;
            return 0;
        }
        st = S_TEXT;
        ent_end(c);
        return c != ';';
    case S_BANG:
        if (c == '-' && ++dash == 2) st = S_COMMENT;
        else if (c != '-') st = c == '>' ? S_TEXT : S_DECL;
        if (st == S_COMMENT) dash = 0;
        return 0;
    case S_COMMENT:
        if (c == '>' && dash >= 2) st = S_TEXT;
        else dash = c == '-' ? (u8)(dash + 1) : 0;
        return 0;
    case S_DECL:
        if (c == '>') st = S_TEXT;
        return 0;
    default:
        /* (S_RAW: to "</" and the tag's name) */
        if (dash < 2 ? c == (u8)"</"[dash] : lower(c) == (u8)nm[dash - 2]) {
            if (++dash == nl + 2) st = S_DECL;
        } else dash = c == '<';
        return 0;
    }
}

static void html_begin(void) {
    u8 k;
    build_start();
    st = S_TEXT;
    popen = want_gap = no_gap = pal = hk = nq = npre = ld = sd = sp_n = sp_nl = u_need = 0;
    for (k = 0; k < sizeof n_on; k++) n_on[k] = 0;
    scr = 0;
}

static void html_chunk(void) {
    u16 i;
    u8 c;
    for (i = 0; i < 1024; i++) {
        c = mem_get(SCRATCH_LIN + i);
        while (feed(c)) ;
    }
}


/* HTMLIN.OVL's entry (mod_run) */
void htmlin_main(void) {
    mod_ret = 0;
    if (mod_cmd == MOD_HTML_BEGIN) html_begin();
    else if (mod_cmd == MOD_HTML_CHUNK) html_chunk();
    else {
        if (st == S_ENT) ent_end(0);
        brk(0);
        mod_ret = build_end();
    }
}
