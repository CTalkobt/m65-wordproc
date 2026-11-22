/* rtf.c — RTF export (RTF.OVL, a run-once module; issue #6; the import is
 * RTFIN.OVL, rtfin.c: apart, as together they no longer fit MOD_SIZE)
 *
 * Export (MOD_RTF_EXPORT: the document to SCRATCH_LIN, its size; 0 when it
 * does not fit) is lossless for Writer's model:
 *   a font table      \f0 Sans (\fswiss), \f1 Serif (\froman), \f2 Mono
 *                     (\fmodern), \f3 Head (\fswiss, at \fs36; the others
 *                     \fs24), \f4 PETSCII (\fmodern: the PETSCII font)
 *   styles            \b \b0, \i \i0, \ul \ulnone, \strike \strike0, \super
 *                     \sub \nosupersub, written where they change
 *   a paragraph       \pard, its kind (\sN of a stylesheet: Normal, heading
 *                     1-3 with \outlinelevel, Quote and Code with an indent,
 *                     \li, List Bullet and List Number; given in the
 *                     paragraph too, as readers want; a list item also its
 *                     \ilvl, indent and {\listtext marker\tab}), its
 *                     alignment (\ql \qc \qr \qj), its text, \par
 *   a page break      \par\page (the paragraph ends the page)
 *   \ { }             escaped with "\"; Latin-1 characters as \'hh
 *                     (\ansicpg1252: Windows-1252 has them)
 *   a PETSCII line    \s8 "PETSCII Code" (read back as one), its
 *                     characters as Unicode (petuni.h): \uN? (U+1FBxx a
 *                     UTF-16 pair, as Word writes it)
 */
#include "wp.h"
#define PET_OUT
#include "petuni.h"

/* ==== export ==== */
#define out o_put
#define outs o_puts

static const char rtf_head[] =
    "{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fswiss Sans;}{\\f1\\froman Serif;}"
    "{\\f2\\fmodern Mono;}{\\f3\\fswiss Head;}{\\f4\\fmodern PETSCII;}}\r\n"
    "{\\stylesheet{\\s0 Normal;}{\\s1\\outlinelevel0 heading 1;}{\\s2\\outlinelevel1 heading 2;}"
    "{\\s3\\outlinelevel2 heading 3;}{\\s4\\li540 Quote;}{\\s5\\li360 Code;}"
    "{\\s6\\li360\\fi-360 List Bullet;}{\\s7\\li360\\fi-360 List Number;}"
    "{\\s8\\li360 PETSCII Code;}}\r\n";
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

/* \u and a number (-32768..32767: u as a signed 16-bit), its one
 * character for readers without Unicode */
static void out_u(u16 u) {
    outs("\\u");
    if (u & 0x8000) {
        out('-');
        u = (u16)(0 - u);
    }
    out_dec(u);
    out('?');
}

static u16 rtf_export(void) {
    u16 i, u;
    u8 ev, now = 0xFF, a, c, d, sp = 0, pet = 0, nf = 0xFF, ns = 0xFF, f;  /* (now: none yet, so all written) */
    o_start();
    outs(rtf_head);
    walk_start();
    while ((ev = walk_next()) != EV_END) {
        if (ev == EV_PARA) {
            outs("\\pard");
            a = w_attr & A_KIND;
            pet = a == K_CODE ? w_attr & A_PET : 0;
            outs(pet ? "\\s8\\li360" : kind_rtf[a]);
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
                f = font_id(a);
                if (f != nf) {
                    outs("\\f");
                    out((u8)('0' + f));
                    outs(f == F_HEAD ? "\\fs36" : "\\fs24");
                    nf = f;
                }
                if (d & A_BOLD) outs(a & A_BOLD ? "\\b" : "\\b0");
                if (d & A_ITAL) outs(a & A_ITAL ? "\\i" : "\\i0");
                if (d & A_UNDER) outs(a & A_UNDER ? "\\ul" : "\\ulnone");
                if (d & A_STRIKE) outs(a & A_STRIKE ? "\\strike" : "\\strike0");
                f = SCRIPT(a);
                if (f != ns) {
                    outs(f == A_SUP ? "\\super" : f ? "\\sub" : "\\nosupersub");
                    ns = f;
                }
                now = a;
                sp = 1;
            }
            if (sp) out(' ');
            sp = 0;
            for (i = w_pos; i < w_to; i++) {
                c = txt_at(i);
                if (pet) {
                    u = pet_to_uni(c);
                    if (u >= 0xFB00) {
                        out_u(0xD83E);          /* (U+1FBxx: D83E, DF00 + xx) */
                        out_u((u16)(0xDF00 | (u & 0xFF)));
                        continue;
                    }
                    if (u >= 0x80) {
                        out_u(u);
                        continue;
                    }
                    if (!u) continue;
                    c = (u8)u;
                }
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

/* RTF.OVL's entry (mod_run) */
void rtf_main(void) {
    mod_ret = rtf_export();
}
