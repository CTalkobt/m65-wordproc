/* helpdoc.c — the help document (WRITER.HELP on the disk): shown at the
 * start, and again with F8 / HELP
 *
 * Built on the host: tools/mkhelp.c lays it out with the C half and writes
 * it as a Writer document (doc_pack's image), which the BASIC program loads
 * (BLOAD). Without the file the program starts with an empty document. The
 * host tests use it too.
 */
#include "wp.h"

struct run { u8 attr; const char *s; };
#define CENTRE (1 << 5)
#define RIGHT (2 << 5)
#define JUST (3 << 5)
#define MONO 2
#define HEAD 3
static const struct run help[] = {
    { HEAD | CENTRE, "MEGA65 Writer" },
    { CENTRE, "\r" },
    { 1 | A_ITAL, "A graphical word processor in BASIC 65 and C" },
    { CENTRE, "\r" },
    { 0, "\r" },
    { 0, "This is the help, shown in " },
    { A_BOLD, "page layout" },
    { 0, " view: every character in its own font and style, the lines wrapped and the pages broken as they will print. Press " },
    { A_BOLD, "F1" },
    { 0, " for the " },
    { A_BOLD, "raw" },
    { 0, " view, which shows the text in one plain font with its formatting codes revealed. Press " },
    { A_BOLD, "F8" },
    { 0, " (or HELP) to see it again; ESC N starts a new document.\r" },
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
    { MONO, "Mono" },
    { 0, " and " },
    { HEAD, "Heading" },
    { 0, ".\r" },
    { 0, "\r" },
    { 1 | A_ITAL, "This paragraph is centred, in the serif font." },
    { CENTRE, "\r" },
    { 0, "\r" },
    { 0, "This paragraph is right aligned." },
    { RIGHT, "\r" },
    { 0, "\r" },
    { 0, "And this one is justified: the spaces between its words are stretched so that each line but the last fills the width of the text exactly, as in a book. Select text with F9 (or CTRL and the cursor keys), then press F3, F5 or F7 to style it, F2 to change its font or F4 to align its paragraph." },
    { JUST, "\r" },
    { 0, "\r" },
    { HEAD, "Editing" },
    { 0, "\r" },
    { MONO, "Typing     inserts; RETURN: a new paragraph\r"
            "DEL / INST deletes left / right\r"
            "Cursors    move; HOME line start / end;\r"
            "           CLR document start / end\r"
            "F13 / F14  page up / down\r"
            "TAB        four spaces\r" },
    { 0, "\r" },
    { HEAD, "Formatting" },
    { 0, "\r" },
    { 0, "The selection, or what you type next:\r" },
    { MONO, "F3 bold    F5 italic    F7 underline\r"
            "F2 font: Sans, Serif, Mono, Head\r"
            "F4 paragraph: left, centre, right, justify\r"
            "F6 page break           F1 page / raw view\r" },
    { 0, "\r" },
    { HEAD, "Selection and clipboard" },
    { 0, "\r" },
    { MONO, "F9         start / end a selection\r"
            "CTRL+crsr  start or extend a selection\r"
            "ESC        end the selection\r"
            "F10 copy   F11 cut   F12 paste\r" },
    { 0, "Typing replaces the selection.\r" },
    { 0, "\r" },
    { HEAD, "Menu (ESC)" },
    { 0, "\r" },
    { MONO, "N new     L load     S save\r"
            "I import  E export   (PETSCII text, or\r"
            "          Markdown for a name ending .md)\r"
            "D directory   H this help   Q quit\r"
            "R redraw the screen (also CTRL+L)\r"
            "T list mode on / off\r" },
    { 0, "Saved Writer documents keep their fonts and styles. "
         "Markdown is read and written by MARKDOWN.OVL, when it is on the disk. "
         "List mode (LIST.OVL) marks spaces, paragraph ends, wrapped lines "
         "and trailing spaces.\r" },
};

void doc_load_help(void) {
    u16 i;
    u8 a;
    const char *s;
    doc_len = 0;
    for (i = 0; i < sizeof help / sizeof help[0]; i++) {
        a = help[i].attr;
        for (s = help[i].s; *s; s++) {
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
