/* welcome.c — the document shown at the start (WELCOME on the disk)
 *
 * Built on the host: tools/mkwelcome.c lays it out with the C half and
 * writes it as a Writer document (doc_pack's image), which the BASIC
 * program loads (BLOAD) the first time it runs. The host tests use it too.
 */
#include "wp.h"

/* ---- the document shown at the start ---- */
struct run { u8 attr; const char *s; };
static const struct run demo[] = {
    { 3 | (1 << 5), "MEGA65 Writer" },
    { 0 | (1 << 5), "\r" },
    { 1 | A_ITAL, "A graphical word processor in BASIC 65 and C" },
    { 0 | (1 << 5), "\r" },
    { 0, "\r" },
    { 0, "This document is shown in " },
    { A_BOLD, "page layout" },
    { 0, " view: every character in its own font and style, the lines wrapped and the pages broken as they will print. Press " },
    { A_BOLD, "F1" },
    { 0, " for the " },
    { A_BOLD, "raw" },
    { 0, " view, which shows the text in one plain font with its formatting codes revealed.\r" },
    { 0, "\r" },
    { 0, "Styles: " },
    { A_BOLD, "bold" },
    { 0, ", " },
    { A_ITAL, "italic" },
    { 0, ", " },
    { A_UNDER, "underlined" },
    { 0, ", " },
    { A_BOLD | A_ITAL | A_UNDER, "all three" },
    { 0, ". Fonts: Sans, " },
    { 1, "Serif" },
    { 0, ", " },
    { 2, "Mono" },
    { 0, " and " },
    { 3, "Heading" },
    { 0, ".\r" },
    { 0, "\r" },
    { 1 | A_ITAL, "This paragraph is centred, in the serif font." },
    { 0 | (1 << 5), "\r" },
    { 0, "\r" },
    { 0, "This paragraph is right aligned." },
    { 0 | (2 << 5), "\r" },
    { 0, "\r" },
    { 0, "And this one is justified: the spaces between its words are stretched so that each line but the last fills the width of the text exactly, as in a book. Select text with F9 and the cursor keys, then press F3, F5 or F7 to style it, F2 to change its font or F4 to align its paragraph." },
    { 0 | (3 << 5), "\r" },
    { 0, "\r" },
    { 3, "Keys" },
    { 0, "\r" },
    { 2, "F1 view  F3 bold  F5 italic  F7 underline\rF2 font  F4 align  F6 page break  F8 help\rF9 mark  F10 copy  F11 cut  F12 paste\rESC menu: new, load, save, export, directory, quit" },
    { 0, "\r" },
};

void doc_load_demo(void) {
    u8 i, a;
    const char *s;
    doc_len = 0;
    for (i = 0; i < sizeof demo / sizeof demo[0]; i++) {
        a = demo[i].attr;
        for (s = demo[i].s; *s; s++) {
            TXT[doc_len] = (u8)*s;
            ATR[doc_len] = a;
            doc_len++;
        }
    }
    cur = 0;
    sel_on = 0;
    view_y = 0;
    goal_valid = 0;
    dirty = 0;
    typing_attr = attr_at_cursor();
    layout_all();
}
