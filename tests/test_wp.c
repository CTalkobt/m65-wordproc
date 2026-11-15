/* test_wp.c — the C half on the host: editing, layout, files, and the
 * screens as pictures (build/page.ppm, build/raw.ppm, ...)
 *
 *   make test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wp.h"

void wp_init(void);
void wp_key(u8 k);
void wp_cmd(u8 c);
u16 wp_get(u8 w);
void wp_setmsg(void);
static int fails;
static int oplog[8], opn;
static void logop(int v) { oplog[opn++ & 7] = v; }
void screen_ppm(const char *path);
void wp_redraw(void);
u8 wp_idle(void);
extern const u8 *host_colram(void);
extern u8 host_mods, host_ctrl_crsr, host_frames;

/* the line table the edits left (laid out to the end) = the document laid out anew */
static void check_incremental(void) {
    static struct line saved[MAX_LINES];
    u16 n, i;
    while (layout_idle()) ;
    wp_redraw();                /* (as wp_idle does when the layout is done) */
    n = nlines;
    memcpy(saved, LINES, n * sizeof(struct line));
    layout();
    if (n != nlines) {
        printf("FAIL: incremental layout has %u lines, a full one %u\n", n, nlines);
        fails++;
        return;
    }
    for (i = 0; i < n; i++) {
        const struct line *a = &saved[i], *b = &LINES[i];
        if (a->start != b->start || a->y != b->y || a->h != b->h || a->asc != b->asc || a->x != b->x ||
            a->extra != b->extra || a->nsp != b->nsp || a->page != b->page) {
            printf("FAIL: incremental layout's line %u differs from a full one's, view %d, ops:", i, view_mode);
            { int q; for (q = 0; q < 8; q++) printf(" %d", oplog[(opn + q) & 7]); }
            printf("\n  inc:  start %u y %u h %u asc %u x %u extra %u nsp %u page %u\n", a->start, a->y, a->h, a->asc, a->x, a->extra, a->nsp, a->page);
            printf("  full: start %u y %u h %u asc %u x %u extra %u nsp %u page %u\n", b->start, b->y, b->h, b->asc, b->x, b->extra, b->nsp, b->page);
            fails++;
            return;
        }
    }
    wp_redraw();
}

/* the screen drawn bit by bit as the cache left it = the screen drawn anew */
static void check_cache(void) {
    static u8 bm[COLS * ROWS * 8], cr[COLS * ROWS * 2];
    memcpy(bm, MEM + BITMAP_LIN, sizeof bm);
    memcpy(cr, host_colram(), sizeof cr);
    wp_redraw();
    if (memcmp(bm, MEM + BITMAP_LIN, sizeof bm) || memcmp(cr, host_colram(), sizeof cr)) {
        int k, r0 = -1, r1 = -1;
        for (k = 0; k < (int)sizeof bm; k++)
            if (bm[k] != MEM[BITMAP_LIN + k]) { if (r0 < 0) r0 = k / 640; r1 = k / 640; }
        for (k = 0; k < 8; k++) printf("%d ", oplog[(opn + k) & 7]);
        printf("\nFAIL: the cached screen differs from a full redraw (view %d, cell rows %d-%d, colours %s)\n", view_mode, r0, r1,
               memcmp(cr, host_colram(), sizeof cr) ? "differ" : "same");
        memcpy(MEM + BITMAP_LIN, bm, sizeof bm);
        screen_ppm("build/cache_bad.ppm");
        wp_redraw();
        screen_ppm("build/cache_good.ppm");
        fails++;
    }
}

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void type(const char *s) {
    /* as GET gives them: lower case letters $41-$5A, capitals $C1-$DA */
    for (; *s; s++) {
        u8 c = (u8)*s;
        if (c >= 'a' && c <= 'z') c = (u8)(c - 32);
        else if (c >= 'A' && c <= 'Z') c = (u8)(c + 128);
        else if (c == '\n') c = 13;
        wp_key(c);
    }
}

static void text_of(char *out, u16 max) {
    u16 i;
    for (i = 0; i < doc_len && i + 1 < max; i++) out[i] = (char)TXT[i];
    out[i] = 0;
}

/* every line inside the text width, the lines covering the document in order */
static void check_layout(void) {
    u16 l;
    while (layout_idle()) ;
    CHECK(nlines > 0);
    CHECK(LINES[0].start == 0);
    for (l = 0; l < nlines; l++) {
        u16 e = line_end(l), i, w = LINES[l].x;
        CHECK(e > LINES[l].start);
        if (view_mode == VIEW_PAGE) {
            u16 trail = 0;
            for (i = LINES[l].start; i < e; i++) {
                u8 c = TXT[i];
                if (c == CH_CR || c == CH_FF) continue;
                w += char_w(c, ATR[i]);
                trail = c == ' ' ? trail + char_w(c, ATR[i]) : 0;
            }
            if (w - trail > TEXT_W && e - LINES[l].start > 1) {
                printf("line %u too wide: %u\n", l, w - trail);
                fails++;
            }
            if (l > 0) CHECK(LINES[l].y >= LINES[l - 1].y + LINES[l - 1].h || LINES[l].page > LINES[l - 1].page);
            {
                u16 ptop = PAGE_GAP + LINES[l].page * PAGE_STRIDE;
                CHECK(LINES[l].y >= ptop + MARGIN_TOP);
                CHECK(LINES[l].y + LINES[l].h <= ptop + MARGIN_TOP + TEXT_H);
            }
        } else {
            CHECK(x_of(l, (u16)(e - 1)) <= COLS);
        }
    }
    CHECK(line_end(nlines - 1) == doc_len);
}

static void check_doc(void) {
    CHECK(doc_len >= 1 && doc_len <= DOC_MAX);
    CHECK(TXT[doc_len - 1] == CH_CR);
    CHECK(cur < doc_len);
}

int main(void) {
    char buf[20000];
    u16 n, i;

    wp_init();
    doc_load_demo();            /* (as BASIC loads WELCOME) */
    wp_redraw();
    check_doc();
    check_layout();
    screen_ppm("build/page.ppm");
    printf("demo: %u characters, %u lines, %u pages\n", doc_len, nlines, npages);

    wp_cmd(7);                  /* raw view */
    CHECK(view_mode == VIEW_RAW);
    check_layout();
    screen_ppm("build/raw.ppm");
    wp_cmd(7);
    CHECK(view_mode == VIEW_PAGE);

    /* typing */
    wp_cmd(13);                 /* new */
    CHECK(doc_len == 1);
    type("Hello world");
    text_of(buf, sizeof buf);
    CHECK(strcmp(buf, "Hello world\r") == 0);
    CHECK(cur == 11);
    wp_key(20);                 /* DEL */
    wp_key(20);
    text_of(buf, sizeof buf);
    CHECK(strcmp(buf, "Hello wor\r") == 0);
    wp_key(19);                 /* HOME */
    CHECK(cur == 0);
    wp_key(19);                 /* again: the line's end */
    CHECK(cur == 9);
    wp_key(19);
    wp_key(148);                /* INST: delete forwards */
    text_of(buf, sizeof buf);
    CHECK(strcmp(buf, "ello wor\r") == 0);
    type("J");
    text_of(buf, sizeof buf);
    CHECK(strcmp(buf, "Jello wor\r") == 0);

    /* bold over a selection */
    wp_key(19);
    wp_cmd(8);                  /* mark */
    for (i = 0; i < 5; i++) wp_key(29);
    CHECK(sel_on && sel_lo() == 0 && sel_hi() == 5);
    wp_cmd(1);                  /* bold */
    CHECK(ATR[0] & A_BOLD);
    CHECK(ATR[4] & A_BOLD);
    CHECK(!(ATR[5] & A_BOLD));
    wp_cmd(9);
    {
        char tags[48];
        u8 k = raw_tag(5, tags);
        tags[k] = 0;
        CHECK(strcmp(tags, "[/B]") == 0);
        k = raw_tag(0, tags);
        tags[k] = 0;
        CHECK(strcmp(tags, "[B]") == 0);
    }
    /* the typing style follows the cursor */
    wp_key(157);
    CHECK(typing_attr & A_BOLD);

    /* CTRL + cursor keys select (read from the keyboard by wp_idle): a
     * selection is started, then extended; the key repeats while held */
    wp_cmd(9);
    wp_key(19);
    wp_key(19);                 /* the line's end: 9 */
    CHECK(cur == 9 && !sel_on);
    host_mods = KM_CTRL;
    host_ctrl_crsr = 157;       /* CTRL + left */
    host_frames = 10;
    wp_idle();
    CHECK(sel_on && sel_anchor == 9 && cur == 8);
    wp_key(157);                /* the same, from GET: left out */
    CHECK(cur == 8);
    host_ctrl_crsr = 0;
    wp_idle();
    host_ctrl_crsr = 157;
    wp_idle();
    host_ctrl_crsr = 0;
    wp_idle();
    host_ctrl_crsr = 157;
    wp_idle();
    CHECK(sel_on && sel_lo() == 6 && sel_hi() == 9);
    host_ctrl_crsr = 0;
    host_mods = 0;
    wp_idle();
    wp_key(157);                /* (a plain move goes on with it, as after F9) */
    CHECK(sel_on && sel_lo() == 5 && sel_hi() == 9);
    wp_cmd(9);
    host_mods = KM_CTRL;
    host_ctrl_crsr = 29;        /* CTRL + right held */
    host_frames = 100;
    wp_idle();
    CHECK(sel_on && sel_anchor == 5 && cur == 6);
    wp_idle();                  /* the same frame: no repeat yet */
    host_frames = 110;
    wp_idle();
    CHECK(cur == 6);
    host_frames = 120;          /* after the first delay: repeats */
    wp_idle();
    CHECK(cur == 7);
    wp_idle();
    CHECK(cur == 7);
    host_frames = 123;
    wp_idle();
    CHECK(cur == 8);
    host_ctrl_crsr = 0;
    host_mods = 0;
    host_frames = 200;
    wp_idle();
    CHECK(cur == 8 && sel_on && sel_lo() == 5 && sel_hi() == 8);
    wp_cmd(9);

    /* wrapping and pages */
    wp_cmd(13);
    for (i = 0; i < 60; i++) {
        type("The quick brown fox jumps over the lazy dog, again and again. ");
        if (i % 7 == 6) wp_key(13);
    }
    check_doc();
    check_layout();
    printf("long: %u characters, %u lines, %u pages\n", doc_len, nlines, npages);
    CHECK(npages >= 2);
    screen_ppm("build/long.ppm");
    /* justify and centre the paragraph */
    wp_cmd(5);
    wp_cmd(5);
    wp_cmd(5);
    check_layout();
    CHECK(((ATR[para_end(cur)] & A_ALIGN) >> A_ALIGN_SHIFT) == 3);
    wp_key(147);
    wp_cmd(5);
    wp_cmd(5);
    wp_cmd(5);
    check_layout();
    screen_ppm("build/justify.ppm");

    /* up and down keep their column */
    wp_key(147);
    if (cur) wp_key(147);       /* CLR: the start (or, there already, the end) */
    CHECK(cur == 0);
    for (i = 0; i < 6; i++) wp_key(29);
    {
        u16 x0 = x_of(line_of(cur), cur);
        wp_key(17);
        wp_key(17);
        CHECK(line_of(cur) == 2);
        CHECK(abs((int)x_of(2, cur) - (int)x0) < 10);
        wp_key(145);
        CHECK(line_of(cur) == 1);
    }

    /* page break */
    wp_cmd(15);
    wp_cmd(6);
    type("New page");
    check_layout();
    CHECK(LINES[line_of(cur)].page == npages - 1);
    CHECK(LINES[line_of(cur)].y == PAGE_GAP + (npages - 1) * PAGE_STRIDE + MARGIN_TOP);

    /* the file image */
    {
        u16 len = doc_len, sz = wp_get(2);
        static u8 t[DOC_MAX], a[DOC_MAX];
        memcpy(t, TXT, len);
        memcpy(a, ATR, len);
        CHECK(sz == 5 + 2 * len);
        wp_cmd(13);
        CHECK(doc_len == 1);
        CHECK(wp_get(4) == 0);
        CHECK(doc_len == len);
        CHECK(memcmp(t, TXT, len) == 0);
        CHECK(memcmp(a, ATR, len) == 0);
        MEM[SCRATCH_LIN] = 'X';
        CHECK(wp_get(4) == 1);
    }

    /* the clipboard */
    wp_cmd(13);
    type("abc def");
    wp_key(19);
    wp_cmd(8);
    wp_key(29);
    wp_key(29);
    wp_key(29);
    wp_cmd(11);                 /* cut */
    text_of(buf, sizeof buf);
    CHECK(strcmp(buf, " def\r") == 0);
    wp_cmd(14);                 /* line end */
    wp_cmd(12);                 /* paste */
    text_of(buf, sizeof buf);
    CHECK(strcmp(buf, " defabc\r") == 0);

    /* export */
    n = wp_get(3);
    CHECK(n == doc_len);
    CHECK(MEM[SCRATCH_LIN + 1] == 0x44);     /* 'd' in PETSCII */

    /* import: PETSCII, CRs past the end trimmed */
    {
        const char *t = "\xC8\x45\x4C\x4C\x4F\r\x57\x4F\x52\x4C\x44\r\r\r\r";
        u16 k;
        memset(MEM + SCRATCH_LIN, 0, DOC_MAX);
        for (k = 0; t[k]; k++) MEM[SCRATCH_LIN + k] = (u8)t[k];
        n = doc_import();
        text_of(buf, sizeof buf);
        CHECK(strcmp(buf, "Hello\rworld\r") == 0);
        CHECK(n == 11);         /* (the final CR not counted) */
    }

    /* random keys: the invariants hold */
    srand(getenv("SEED") ? (unsigned)atoi(getenv("SEED")) : 1);
    doc_load_demo();
    for (i = 0; i < 20000; i++) {
        static const u8 keys[] = {29, 157, 17, 145, 19, 147, 13, 20, 148, 9, 0x41, 0x42, 0x20, 0xC5, 0x2E};
        int r = rand() % 40;
        if (r < (int)sizeof keys) { logop(keys[r]); wp_key(keys[r]); }
        else if (r < 30) { logop(0x41 + r % 26); wp_key(0x41 + r % 26); }
        else { int c = 1 + rand() % 18; logop(-c); wp_cmd((u8)c); }
        check_doc();
        if (i % 13 == 0) check_cache();
        if (i % 7 == 0) check_incremental();
        if (fails > 20) break;
    }
    check_layout();
    wp_cmd(7);
    check_layout();
    screen_ppm("build/fuzz.ppm");

    /* the same on a document of several pages: edits far from the end, the
     * rest laid out later, page breaks moving */
    wp_cmd(13);
    if (view_mode != VIEW_PAGE) wp_cmd(7);
    for (i = 0; i < 1200; i++) {
        u16 k;
        static const char *w[] = {"lorem ", "ipsum ", "dolor ", "sit ", "amet, ", "consectetur ", "MEGA65 "};
        const char *t = w[rand() % 7];
        u8 a = (u8)(rand() % 9 == 0 ? A_BOLD : rand() % 13 == 0 ? 3 : 0);
        for (k = 0; t[k]; k++) {
            TXT[doc_len - 1 + k] = (u8)t[k];
            ATR[doc_len - 1 + k] = a;
        }
        doc_len += k;
        if (rand() % 12 == 0) TXT[doc_len++ - 1] = CH_CR;
        TXT[doc_len - 1] = CH_CR;
        ATR[doc_len - 1] = (u8)((rand() % 4) << A_ALIGN_SHIFT);
    }
    layout_all();
    wp_key(147);
    printf("big: %u characters\n", doc_len);
    for (i = 0; i < 3000; i++) {
        static const u8 keys[] = {29, 157, 17, 145, 13, 20, 148, 0x41, 0x20, 0xC5, 26, 25};
        int r = rand() % 30;
        if (r < (int)sizeof keys) {
            u8 kk = keys[r];
            logop(kk);
            if (kk == 26 || kk == 25) wp_cmd(kk == 26 ? 17 : 16);
            else wp_key(kk);
        } else if (r < 25) { logop(0x41 + r); wp_key((u8)(0x41 + r)); }
        else if (r < 27) { int c = 1 + rand() % 5; logop(-c); wp_cmd((u8)c); }
        else if (r == 27) { logop(-8); wp_cmd(8); }
        else { logop(-9); wp_cmd(9); }
        check_doc();
        if (i % 11 == 0) check_incremental();
        if (i % 17 == 0) check_cache();
        if (fails > 20) break;
    }
    check_layout();
    screen_ppm("build/fuzz2.ppm");

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
