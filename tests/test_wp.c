/* test_wp.c — the C half on the host: editing, layout, files, and the
 * screens as pictures (build/page.ppm, build/raw.ppm, ...)
 *
 *   make test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wp.h"
#include "msg.h"

void wp_init(void);
u8 wp_key(u8 k);
u16 wp_find(u8 op);
void wp_cmd(u8 c);
u16 wp_get(u8 w);
void wp_panel(u8 n);
void wp_say(u8 n);
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

/* the style of character i (a CR / FF: its paragraph's attribute), and
 * character i's set (the attribute byte: an index into the style table) */
static u8 sa(u16 i) {
    u8 a = atr_at(i), c = txt_at(i);
    return c == CH_CR || c == CH_FF ? a : sty(a);
}

static void sput(u16 i, u8 a) {
    u8 c = txt_at(i);
    atr_put(i, c == CH_CR || c == CH_FF ? a : sty_find(a));
}

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

/* the document as Markdown into out; its size */
static u16 md_out(char *out) {
    u16 n = md_export();
    memcpy(out, MEM + SCRATCH_LIN, n);
    out[n] = 0;
    return n;
}

/* Markdown s as the document (as FREAD# leaves it: the rest zero) */
/* RTF: the document exported (into out, 0-ended; its size), and s
 * imported as BASIC streams a file: 1 KB at a time, past its end CRs */
static u16 rtf_out(char *out) {
    u16 n = mod_run(MOD_RTF_EXPORT);
    memcpy(out, MEM + SCRATCH_LIN, n);
    out[n] = 0;
    return n;
}

static u16 rtf_in(const char *s) {
    size_t len = strlen(s), at = 0;
    mod_run(MOD_RTF_BEGIN);
    do {
        size_t k = len - at < 1024 ? len - at : 1024;
        memset(MEM + SCRATCH_LIN, CH_CR, 1024);
        memcpy(MEM + SCRATCH_LIN, s + at, k);
        mod_run(MOD_RTF_CHUNK);
        at += k;
    } while (at < len);
    return mod_run(MOD_RTF_END);
}

/* HTML s imported as BASIC streams a file (as rtf_in) */
static u16 html_in(const char *s) {
    size_t len = strlen(s), at = 0;
    mod_run(MOD_HTML_BEGIN);
    do {
        size_t k = len - at < 1024 ? len - at : 1024;
        memset(MEM + SCRATCH_LIN, CH_CR, 1024);
        memcpy(MEM + SCRATCH_LIN, s + at, k);
        mod_run(MOD_HTML_CHUNK);
        at += k;
    } while (at < len);
    return mod_run(MOD_HTML_END);
}

/* the document as it matters to a file: the text, each character's style,
 * each paragraph's kind and alignment */
static u16 doc_sig(u8 *t, u8 *a) {
    u16 i;
    for (i = 0; i < doc_len; i++) {
        t[i] = txt_at(i);
        a[i] = (u8)(sa(i) & (txt_at(i) == CH_CR || txt_at(i) == CH_FF ? A_PARA : A_STYLE));
    }
    return doc_len;
}

/* exported as RTF and imported again: the same document */
static void rtf_round(const char *what) {
    static u8 t1[DOC_MAX], a1[DOC_MAX], t2[DOC_MAX], a2[DOC_MAX];
    static char r[32000];
    u16 n1 = doc_sig(t1, a1), n2, k;
    rtf_out(r);
    rtf_in(r);
    n2 = doc_sig(t2, a2);
    if (n1 != n2 || memcmp(t1, t2, n1) || memcmp(a1, a2, n1)) {
        for (k = 0; k < n1 && k < n2 && t1[k] == t2[k] && a1[k] == a2[k]; k++) ;
        printf("FAIL: %s changed by an RTF round trip at %u (of %u, now %u): '%c' %02x, now '%c' %02x\n", what, k, n1,
               n2, t1[k], a1[k], t2[k], a2[k]);
        fails++;
    }
}

static void md_in(const char *s) {
    memset(MEM + SCRATCH_LIN, 0, SCRATCH_MAX);
    memcpy(MEM + SCRATCH_LIN, s, strlen(s));
    mod_run(MOD_MD_IMPORT);
}

static void show(const char *s) {
    int k;
    for (k = 0; k < 90 && s[k]; k++) {
        if (s[k] == '\n') printf("\\n");
        else putchar(s[k]);
    }
    putchar('\n');
}

/* m1 (the document's Markdown) imported and exported again: the same */
static void md_fixed_point(const char *m1, char *m2, const char *what) {
    md_in(m1);
    md_out(m2);
    if (strcmp(m1, m2) != 0) {
        u16 k = 0;
        while (m1[k] && m1[k] == m2[k]) k++;
        printf("FAIL: %s Markdown changed by an import at byte %u\n  was: ", what, k);
        show(m1 + (k > 40 ? k - 40 : 0));
        printf("  now: ");
        show(m2 + (k > 40 ? k - 40 : 0));
        fails++;
    }
}

static void text_of(char *out, u16 max) {
    u16 i;
    for (i = 0; i < doc_len && i + 1 < max; i++) out[i] = (char)txt_at(i);
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
                u8 c = txt_at(i);
                if (c == CH_CR || c == CH_FF) continue;
                w += char_w(c, sa(i));
                trail = c == ' ' ? trail + char_w(c, sa(i)) : 0;
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
    CHECK(txt_at(doc_len - 1) == CH_CR);
    CHECK(cur < doc_len);
}

/* msg (ASCII) set as BASIC pokes it (PETSCII), then wp_find(op) */
static u16 find_str(const char *t, u8 op) {
    u8 i;
    for (i = 0; t[i]; i++) {
        u8 c = (u8)t[i];
        if (c >= 'a' && c <= 'z') c = (u8)(c - 32);
        else if (c >= 'A' && c <= 'Z') c = (u8)(c + 128);
        msg[i] = (char)c;
    }
    msg[i] = 0;
    return wp_find(op);
}

static u8 ctrl(u8 k, u8 shift) {
    u8 r;
    host_mods = (u8)(KM_CTRL | (shift ? 1 : 0));
    r = wp_key(k);
    host_mods = 0;
    return r;
}

static int text_is(const char *t) {
    char b[200];
    text_of(b, sizeof b);
    return strlen(t) + 1 == doc_len && !memcmp(b, t, doc_len - 1);
}

/* undo (CTRL+Z), find and replace, the keys BASIC is given back */
static void test_edit(void) {
    static u8 t0[DOC_MAX], a0[DOC_MAX], t1[DOC_MAX], a1[DOC_MAX], t2[DOC_MAX], a2[DOC_MAX];
    u16 n0, n1, n2, i;
    u8 a;
    wp_cmd(13);
    /* typing undone, and done again */
    type("hello world");
    ctrl(0x1A, 0);
    CHECK(text_is("") && cur == 0);
    ctrl(0x1A, 0);
    CHECK(text_is("hello world"));
    /* a cursor move ends an edit: only the typing after it undone */
    wp_key(147);                /* (CLR: the start; again: the end) */
    wp_key(147);
    wp_key(157);
    type("XY");
    CHECK(text_is("hello worlXYd"));
    ctrl(0x1A, 0);
    CHECK(text_is("hello world") && cur == 10);
    /* DEL after a move: what it deleted back */
    wp_key(147);
    wp_key(147);
    wp_key(20);
    wp_key(20);
    wp_key(20);
    CHECK(text_is("hello wo"));
    ctrl(0x1A, 0);
    CHECK(text_is("hello world"));
    /* a style: undone */
    n0 = doc_sig(t0, a0);
    wp_cmd(18);                 /* all */
    ctrl(2, 0);                 /* CTRL+B */
    CHECK((sa(0) & A_BOLD) != 0);
    ctrl(0x1A, 0);
    n1 = doc_sig(t1, a1);
    CHECK(n0 == n1 && !memcmp(t0, t1, n0) && !memcmp(a0, a1, n0));
    /* cut and paste */
    wp_cmd(18);
    wp_cmd(11);
    CHECK(text_is(""));
    ctrl(0x1A, 0);
    CHECK(text_is("hello world"));
    /* nothing to undo after a new document */
    wp_cmd(13);
    ctrl(0x1A, 0);
    CHECK(!strcmp(msg, "Nothing to undo."));

    /* CTRL keys: B / I / U the typing style; F / H / W BASIC's; F10, F8, ESC
     * BASIC's without CTRL (F10's code is CTRL+U's); TAB without CTRL */
    a = sty(typing_attr);
    ctrl(2, 0);
    ctrl(9, 0);
    ctrl(0x15, 0);
    CHECK(sty(typing_attr) == (a ^ (A_BOLD | A_ITAL | A_UNDER)));
    ctrl(2, 0);
    ctrl(9, 0);
    ctrl(0x15, 0);
    CHECK(ctrl(6, 0) == 6 && ctrl(8, 0) == 7 && ctrl(0x17, 0) == 8);
    CHECK(wp_key(0x15) == 2 && wp_key(0x8C) == 1 && wp_key(0x84) == 1 && wp_key(0x1B) == 5 && wp_key(4) == 3);
    wp_key(9);
    CHECK(text_is("    "));
    wp_cmd(13);

    /* find: either case, round the end, back */
    type("one two one Two");
    CHECK(find_str("two", FIND_SETF) == 0);
    CHECK(wp_find(FIND_FWD) == 1 && sel_lo() == 4 && cur == 7);
    CHECK(wp_find(FIND_FWD) == 1 && sel_lo() == 12);
    CHECK(ctrl(6, 1) == 0 && sel_lo() == 4);            /* SHIFT+CTRL+F: round the end */
    CHECK(ctrl(2, 1) == 0 && sel_lo() == 12);           /* SHIFT+CTRL+B */
    find_str("three", FIND_SETF);
    CHECK(wp_find(FIND_FWD) == 0 && !strcmp(msg, "Not found."));
    /* replace: Y, N, then A */
    type("x");                  /* (the selection, "Two", typed over) */
    CHECK(text_is("one two one x"));
    wp_cmd(13);
    type("a cat, a Cat, a cat, a cat");
    wp_key(147);
    find_str("cat", FIND_SETF);
    CHECK(wp_find(FIND_FWD) == 1 && sel_lo() == 2);
    find_str("dog", FIND_SETR);
    CHECK(!strcmp(msg, "Replace? Y)es N)o A)ll"));
    CHECK(wp_find(FIND_REPL) == 1 && sel_lo() == 9);    /* Y */
    CHECK(text_is("a dog, a Cat, a cat, a cat"));
    CHECK(wp_find(FIND_FWD) == 1 && sel_lo() == 16);    /* N */
    CHECK(wp_find(FIND_ALL) == 0 && !strcmp(msg, "Replaced 3."));
    CHECK(text_is("a dog, a dog, a dog, a dog"));
    ctrl(0x1A, 0);              /* all of A undone */
    CHECK(text_is("a dog, a Cat, a cat, a cat"));
    find_str("cat", FIND_SETF);
    wp_find(FIND_FWD);
    find_str("", FIND_SETR);    /* (an empty replacement deletes) */
    wp_find(FIND_REPL);
    CHECK(text_is("a dog, a , a cat, a cat") && sel_lo() == 13);
    CHECK(wp_find(FIND_STOP) == 0 && msg[0] == 0 && !r_on);
    check_incremental();

    /* undo, at random: a run of keys (no cursor move: one edit) or a
     * command, undone (the document as before it), done again (as after) */
    wp_cmd(13);
    type("Undo me. The first paragraph\nA second, longer one, wider than a line is: it wraps on the page.\n");
    srand(getenv("SEED") ? (unsigned)atoi(getenv("SEED")) + 6 : 7);
    for (i = 0; i < 400 && fails < 20; i++) {
        int r = rand() % 4, k;
        wp_cmd(9);
        for (k = rand() % 6; k > 0; k--) wp_key(rand() % 2 ? 29 : 145);
        if (rand() % 3 == 0) {
            wp_cmd(8);
            for (k = rand() % 12; k > 0; k--) wp_key(rand() % 3 ? 29 : 17);
        }
        u_have = 0;             /* (an op that changes nothing: undo then undoes nothing) */
        n0 = doc_sig(t0, a0);
        opn = 0;
        logop(sel_on ? sel_lo() : cur);
        logop(sel_on ? sel_hi() : -1);
        if (r < 2) {
            for (k = 1 + rand() % 6; k > 0; k--) {
                static const u8 keys[] = {0x41, 0x20, 0xC5, 13, 20, 148, 0x42, 20};
                u8 kk = keys[rand() % sizeof keys];
                logop(kk);
                wp_key(kk);
            }
        } else {
            static const u8 cmds[] = {1, 2, 3, 4, 5, 6, 11, 12, 32, 33, 35, 36, 37, 38, 39};
            u8 c = cmds[rand() % sizeof cmds];
            if (c == 11) wp_cmd(10);
            logop(-c);
            wp_cmd(c);
        }
        n1 = doc_sig(t1, a1);
        wp_cmd(22);
        n2 = doc_sig(t2, a2);
        if (n0 != n2 || memcmp(t0, t2, n0) || memcmp(a0, a2, n0)) {
            int q;
            printf("FAIL: undo %d: not the document as before (%u, now %u characters); from, to, ops:", i, n0, n2);
            for (q = 0; q < opn && q < 8; q++) printf(" %d", oplog[q]);
            printf("\n");
            fails++;
        }
        wp_cmd(22);
        n2 = doc_sig(t2, a2);
        if (n1 != n2 || memcmp(t1, t2, n1) || memcmp(a1, a2, n1)) {
            printf("FAIL: redo %d: not the document as after (%u, now %u characters)\n", i, n1, n2);
            fails++;
        }
        check_doc();
        if (i % 7 == 0) check_incremental();
        if (doc_len > 2000) {
            wp_cmd(13);
            type("Again.\nAnd more.\n");
        }
    }
    /* a cut longer than undo can keep: nothing to undo */
    wp_cmd(13);
    for (i = 0; i < 210; i++) type("Fifty characters of text to fill the document up..");
    wp_cmd(18);
    wp_cmd(11);
    ctrl(0x1A, 0);
    CHECK(!strcmp(msg, "Nothing to undo.") && doc_len == 1);
    wp_cmd(13);
}

/* the gap (doc.c): edits at random places, long runs of typing (more than
 * the gap holds), deletions both ways, cuts and pastes, against a plain
 * copy of the text; the file image too, and undo across the gap */
static void test_gap(void) {
    static char ref[DOC_MAX + 1];
    u16 n = 0, i, k, p, q, bad = 0;
    wp_cmd(13);
    n = 0;
    srand(getenv("SEED") ? (unsigned)atoi(getenv("SEED")) + 11 : 12);
    for (i = 0; i < 1500 && !bad; i++) {
        int r = rand() % 10;
        p = (u16)(rand() % (n + 1));
        wp_cmd(9);
        cur = p;
        goal_valid = 0;
        if (r < 4) {            /* typing: now and then more than the gap */
            k = (u16)(rand() % 8 == 0 ? 2100 + rand() % 400 : 1 + rand() % 40);
            if (n + k > 12000) k = 0;
            for (q = 0; q < k; q++) {
                u8 c = (u8)(rand() % 5 ? 0x41 + rand() % 26 : 0x20);
                wp_key(c);
                memmove(ref + p + q + 1, ref + p + q, n - p - q);
                ref[p + q] = (char)(c + 0x20);
                if (c == 0x20) ref[p + q] = ' ';
                n++;
            }
        } else if (r < 6) {     /* DEL (backspace) */
            for (k = (u16)(1 + rand() % 30); k > 0 && cur > 0; k--) {
                wp_key(20);
                memmove(ref + cur, ref + cur + 1, n - cur);
                n--;
            }
        } else if (r < 7) {     /* INST (forwards) */
            for (k = (u16)(1 + rand() % 30); k > 0 && cur < n; k--) {
                wp_key(148);
                memmove(ref + cur, ref + cur + 1, n - cur - 1);
                n--;
            }
        } else if (r < 9 && n > 1) {    /* a cut, then a paste elsewhere */
            q = (u16)(p + rand() % (n - p + 1));
            if ((u16)(q - p) > CLIP_MAX) q = p + CLIP_MAX;
            if (q > p) {
                static char cut[CLIP_MAX];
                k = q - p;
                memcpy(cut, ref + p, k);
                sel_anchor = p;
                cur = q;
                sel_on = 1;
                wp_cmd(11);
                memmove(ref + p, ref + q, n - q);
                n -= k;
                p = (u16)(rand() % (n + 1));
                wp_cmd(9);
                cur = p;
                wp_cmd(12);
                memmove(ref + p + k, ref + p, n - p);
                memcpy(ref + p, cut, k);
                n += k;
            }
        } else {                /* undo, then redo (the gap closed for it) */
            wp_cmd(22);
            wp_cmd(22);
        }
        if (doc_len != n + 1 || (g_len && g_at > doc_len) || (u32)doc_len + g_len > DOC_MAX + GAP_SPAN) bad = 1;
        for (q = 0; q < n && !bad; q++)
            if (txt_at(q) != (u8)ref[q]) bad = 1;
        if (bad) printf("FAIL: gap %d (op %d at %u): doc_len %u (want %u), gap %u+%u, at %u\n", i, r, p, doc_len, n + 1, g_at, g_len, q);
        if (i % 50 == 0) check_doc();
    }
    fails += bad;
    /* the file image (round the gap) */
    {
        u16 sz = wp_get(2);
        CHECK(sz == 5 + 2 * doc_len + 3 + st_n);
        for (q = 0; q < n && MEM[SCRATCH_LIN + 5 + q] == (u8)ref[q]; q++) {}
        CHECK(q == n);
        CHECK(wp_get(4) == 0 && doc_len == n + 1);
        for (q = 0; q < n && txt_at(q) == (u8)ref[q]; q++) {}
        CHECK(q == n);
    }
    check_doc();
    check_layout();

    /* the limits: the scratch area's parts apart; a document near DOC_MAX,
     * of more lines than the old 1024, edited, undone, saved and loaded */
    CHECK(PARK_LIN + 2 * UNDO_MAX <= UNDO_LIN && UNDO_LIN + 2 * UNDO_MAX <= SCRATCH_LIN + SCRATCH_MAX);
    CHECK(5 + 2 * (u32)DOC_MAX <= SCRATCH_MAX && (u32)DOC_MAX + GAP_SPAN + CLIP_MAX <= AO);
    CHECK((u32)MAX_LINES * sizeof(struct line) <= LBOUNCE_LIN - LINES_LIN);
    {
        static const char w[] = "Line of text\n";
        wp_cmd(13);
        memset(MEM + SCRATCH_LIN, 0, SCRATCH_MAX);
        for (n = 0; n + 13 < DOC_MAX - 1000; n += 13) memcpy(MEM + SCRATCH_LIN + n, w, 13);
        for (q = 0; q < n; q++)
            if (MEM[SCRATCH_LIN + q] == '\n') MEM[SCRATCH_LIN + q] = 13;
        doc_import();
        layout();
        CHECK(doc_len > 28000 && nlines > 2000);
        check_layout();
        cur = doc_len / 2;
        type("Typed in the middle of a long document. ");
        check_doc();
        check_incremental();
        sel_anchor = 100;
        cur = 2400;
        sel_on = 1;
        wp_cmd(11);
        cur = doc_len - 50;
        wp_cmd(12);
        check_doc();
        k = doc_len;
        wp_cmd(22);
        CHECK(doc_len == k - 2300);
        wp_cmd(22);
        CHECK(doc_len == k);
        p = wp_get(2);
        CHECK(p == 5 + 2 * doc_len + 3 + st_n);
        CHECK(wp_get(4) == 0 && doc_len == k);
        layout();
        check_layout();
    }
    wp_cmd(13);
}

int main(void) {
    char buf[20000];
    u16 n, i;

    wp_init();
    doc_load_help();            /* (as BASIC loads WRITER.HELP) */
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

    /* list mode (LIST.OVL: on the host, list.c linked in) and the menu's
     * panel: each changes the screen, and leaves it as it was when off */
    {
        static u8 off[COLS * ROWS * 8], cr[COLS * ROWS * 2];
        u8 v;
        CHECK(wp_get(10) == 1);         /* GET_LISTOK */
        for (v = 0; v < 2; v++) {
            wp_redraw();
            memcpy(off, MEM + BITMAP_LIN, sizeof off);
            memcpy(cr, host_colram(), sizeof cr);
            wp_cmd(21);                 /* CMD_LIST */
            CHECK(list_on);
            CHECK(memcmp(off, MEM + BITMAP_LIN, sizeof off) != 0);
            check_cache();
            screen_ppm(v ? "build/list_raw.ppm" : "build/list_page.ppm");
            wp_cmd(21);
            CHECK(!list_on);
            CHECK(memcmp(off, MEM + BITMAP_LIN, sizeof off) == 0);
            wp_panel(PANEL_ROWS);
            CHECK(strncmp(msg, "View:  R)edraw  T)List on", 25) == 0);
            CHECK(memcmp(off, MEM + BITMAP_LIN, sizeof off) != 0);
            wp_panel(0);
            CHECK(memcmp(off, MEM + BITMAP_LIN, sizeof off) == 0);
            /* a message's place holders: @ a field (to a comma), % the rest
             * (msg as BASIC pokes it: PETSCII) */
            strcpy(msg, "NAME,12");
            wp_say(MSG_SAVED);
            CHECK(strcmp(msg, "Saved name (12 bytes).") == 0);
            strcpy(msg, "62,FILE NOT FOUND,00,00");
            wp_say(MSG_NOTIMP);
            CHECK(strcmp(msg, "Not imported: 62,file not found,00,00") == 0);
            wp_say(MSG_NONE);
            CHECK(msg[0] == 0);
            CHECK(memcmp(cr, host_colram(), sizeof cr) == 0);
            wp_cmd(7);
        }
        CHECK(view_mode == VIEW_PAGE);
    }

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
    CHECK(sa(0) & A_BOLD);
    CHECK(sa(4) & A_BOLD);
    CHECK(!(sa(5) & A_BOLD));
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
    CHECK(sty(typing_attr) & A_BOLD);

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
    CHECK(((sa(para_end(cur)) & A_ALIGN) >> A_ALIGN_SHIFT) == 3);
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
        u16 len = doc_len, sz = wp_get(2), i, same = 1;
        static u8 t[DOC_MAX], a[DOC_MAX];
        for (i = 0; i < len; i++) {
            t[i] = txt_at(i);
            a[i] = atr_at(i);
        }
        CHECK(sz == 5 + 2 * len + 3 + st_n);
        wp_cmd(13);
        CHECK(doc_len == 1);
        CHECK(wp_get(4) == 0);
        CHECK(doc_len == len);
        for (i = 0; i < len; i++)
            if (t[i] != txt_at(i) || a[i] != atr_at(i)) same = 0;
        CHECK(same);
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
    n = wp_get(17);           /* GET_XEXPORT: text */
    CHECK(n == doc_len);
    CHECK(MEM[SCRATCH_LIN + 1] == 0x44);     /* 'd' in PETSCII */

    /* the import's clearing stops at the clipboard */
    MEM[CLIP_LIN] = 0x5A;
    MEM[CLIP_LIN - 1] = 0x5A;
    CHECK(wp_get(6) == 0);                      /* GET_IMPORT_PREP */
    CHECK(MEM[CLIP_LIN] == 0x5A && MEM[CLIP_LIN - 1] == 0);
    CHECK(SCRATCH_LIN + SCRATCH_MAX == CLIP_LIN);

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

    /* ---- HTML (HTML.OVL): the help (build/help.html, for a look) ---- */
    {
        static char h[40000];
        doc_load_help();
        n = mod_run(MOD_HTML_EXPORT);
        CHECK(n > 0);
        memcpy(h, MEM + SCRATCH_LIN, n);
        h[n] = 0;
        {
            FILE *f = fopen("build/help.html", "wb");
            if (f) {
                fwrite(h, 1, n, f);
                fclose(f);
            }
        }
        CHECK(strncmp(h, "<!DOCTYPE html>\n", 16) == 0);
        CHECK(strstr(h, "<h1 style=\"text-align: center\">MEGA65 Writer</h1>") != NULL);
        CHECK(strstr(h, "This is the help, shown in <b>page layout</b> view") != NULL);
        CHECK(strstr(h, "<u>underlined</u>") != NULL);
        CHECK(strstr(h, "<pre><code>Typing     inserts; RETURN: a new paragraph\nDEL / INST") != NULL);
        CHECK(strstr(h, "<code>Mono</code>") != NULL);
        CHECK(strstr(h, "style=\"text-align: justify\"") != NULL);
        CHECK(strstr(h, "<ul>\n<li>") != NULL && strstr(h, "<ol>\n<li>") != NULL);
        CHECK(strcmp(h + n - 16, "</body>\n</html>\n") == 0);
        /* & < > and Latin-1 */
        doc_new();
        type("a < b & c > d");
        doc_type(0xE9);
        n = mod_run(MOD_HTML_EXPORT);
        memcpy(h, MEM + SCRATCH_LIN, n);
        h[n] = 0;
        CHECK(strstr(h, "<p>a &lt; b &amp; c &gt; d\xC3\xA9</p>") != NULL);
        gap_close();            /* (doc_load_help writes the text as it is) */
    }

    /* ---- HTML import ---- */
    {
        static char h1[40000], h2[40000];
        u16 k;
        /* the help's HTML imported and exported again: the same */
        doc_load_help();
        n = mod_run(MOD_HTML_EXPORT);
        memcpy(h1, MEM + SCRATCH_LIN, n);
        h1[n] = 0;
        html_in(h1);
        n = mod_run(MOD_HTML_EXPORT);
        memcpy(h2, MEM + SCRATCH_LIN, n);
        h2[n] = 0;
        if (strcmp(h1, h2) != 0) {
            k = 0;
            while (h1[k] && h1[k] == h2[k]) k++;
            printf("FAIL: the help's HTML changed by an import at byte %u\n  was: ", k);
            show(h1 + (k > 40 ? k - 40 : 0));
            printf("  now: ");
            show(h2 + (k > 40 ? k - 40 : 0));
            fails++;
        }
        text_of(buf, sizeof buf);
        CHECK(strncmp(buf, "MEGA65 Writer\rA graphical word processor", 40) == 0);

        /* HTML from elsewhere */
        n = html_in("<!DOCTYPE html>\n<html><head><title>T</title><style>p { x: 1 }</style>\n"
                    "<script>if (a<b) x = \"</p>\";</script></head>\n<body>\n"
                    "<p>Hello <strong>bold</strong>\n  and <em>it</em> <u>u</u>, <code>c</code>.</p>\n"
                    "<!-- a <p>comment</p> -->\n<p class=x>two  spaces</p></body></html>\n");
        text_of(buf, sizeof buf);
        CHECK(strcmp(buf, "Hello bold and it u, c.\r\rtwo  spaces\r") == 0);
        CHECK(n == 36);
        CHECK(sa(6) == A_BOLD && sa(15) == A_ITAL && sa(18) == A_UNDER && sa(21) == F_MONO &&
              sa(0) == 0 && sa(10) == 0);
        html_in("<h1>Title</h1><p>para</p><h2>Sub</h2>\n<h4>deep</h4><p align=\"center\">mid</p>"
                "<div style=\"color: red; text-align: right\">r</div>");
        text_of(buf, sizeof buf);
        CHECK(strcmp(buf, "Title\rpara\r\rSub\rdeep\rmid\r\rr\r") == 0);
        CHECK(para_kind(0) == K_H1 && para_kind(6) == K_NORMAL && para_kind(12) == K_H2 && para_kind(16) == K_H3 &&
              ((sa(24) & A_ALIGN) >> A_ALIGN_SHIFT) == 1 && ((sa(27) & A_ALIGN) >> A_ALIGN_SHIFT) == 2);
        html_in("<ul>\n  <li>one</li>\n  <li><p>two</p>\n    <ol start=3><li>a<li>b</ol>\n  </li>\n</ul>\n<p>after</p>");
        text_of(buf, sizeof buf);
        CHECK(strcmp(buf, "one\rtwo\ra\rb\r\rafter\r") == 0);
        CHECK(para_kind(0) == K_BULLET && para_kind(4) == K_BULLET && para_kind(8) == K_NUMBER &&
              (sa(9) & A_LEVEL) == 1 << A_LEVEL_SHIFT && item_num(10) == 2 && para_kind(13) == K_NORMAL);
        html_in("<pre>\nint *p;\n\n  x &lt; 1\n</pre><blockquote><p>q1</p><p>q2<br>q3</p></blockquote>");
        text_of(buf, sizeof buf);
        CHECK(strcmp(buf, "int *p;\r\r  x < 1\r\rq1\r\rq2\rq3\r") == 0);
        CHECK(para_kind(0) == K_CODE && para_kind(8) == K_CODE && sa(10) == F_MONO && para_kind(20) == K_QUOTE &&
              para_kind(26) == K_QUOTE);
        html_in("caf&eacute; &Uuml;ber &szlig; &AElig; &yuml; &#233;&#xE9; &copy; &mdash; &bogus; a & b &lt\n"
                "caf\xc3\xa9 \xe2\x80\x9cq\xe2\x80\x9d \xe9t\xe9 \xf0\x9f\x98\x80!");
        text_of(buf, sizeof buf);
        CHECK(strcmp(buf, "caf\xe9 \xdc" "ber \xdf \xc6 \xff \xe9\xe9 \xa9 -- &bogus; a & b < caf\xe9 \"q\" \xe9t\xe9 ?!\r") ==
              0);
        html_in("<p>one</p><div style=\"page-break-after: always\"></div><p>two</p>"
                "<table><tr><td>a</td><td>b</td></tr><tr><th>c</th></tr></table>");
        text_of(buf, sizeof buf);
        CHECK(strcmp(buf, "one\r\ftwo\r\ra  b\rc\r") == 0);
        html_in("<p>x <span class=\"r\">serif <code>mono</code></span> <span>y</span></p>");
        CHECK(sa(0) == F_SANS && sa(2) == F_SERIF && sa(8) == F_MONO && sa(13) == F_SANS);
        html_in("");
        CHECK(doc_len == 1);
    }

    /* ---- Markdown ---- */
    {
        static char m1[40000], m2[40000];
        u16 k;
        /* the help as Markdown (build/help.md, for a look) */
        doc_load_help();
        n = md_out(m1);
        {
            FILE *f = fopen("build/help.md", "wb");
            if (f) {
                fwrite(m1, 1, n, f);
                fclose(f);
            }
        }
        CHECK(strstr(m1, "<div align=\"center\">\n\n# MEGA65 Writer\n\n</div>") != NULL);
        CHECK(strstr(m1, "This is the help, shown in **page layout** view") != NULL);
        CHECK(strstr(m1, "**_bold_**") == NULL);
        CHECK(strstr(m1, "<u>underlined</u>") != NULL);
        CHECK(strstr(m1, "***<u>all three</u>***") != NULL || strstr(m1, "<u>***all three***</u>") != NULL);
        CHECK(strstr(m1, "\n```\nTyping     inserts; RETURN: a new paragraph\nDEL / INST") != NULL);
        CHECK(strstr(m1, "`Mono`") != NULL);
        CHECK(strstr(m1, "<div align=\"justify\">") != NULL);
        /* imported, it exports the same again */
        md_fixed_point(m1, m2, "help");
        text_of(buf, sizeof buf);
        CHECK(strncmp(buf, "MEGA65 Writer\rA graphical word processor", 40) == 0);
        CHECK((sa(0) & A_FONT) == 3 && ((sa(13) & A_ALIGN) >> A_ALIGN_SHIFT) == 1);

        /* Markdown from elsewhere */
        md_in("Hello *world* and **bold**, `code` <u>under</u>.\n");
        text_of(buf, sizeof buf);
        CHECK(strcmp(buf, "Hello world and bold, code under.\r") == 0);
        CHECK(sa(6) == A_ITAL && sa(16) == A_BOLD && sa(22) == 2 && sa(27) == A_UNDER && sa(0) == 0);
        md_in("# Title\n\nPara one\nwraps here.\n\n- item 1\n- item 2\n\n1. first\n2) second\n");
        text_of(buf, sizeof buf);
        CHECK(strcmp(buf, "Title\rPara one wraps here.\r\ritem 1\ritem 2\rfirst\rsecond\r") == 0);
        CHECK(para_kind(28) == K_BULLET && para_kind(35) == K_BULLET && para_kind(42) == K_NUMBER && item_num(42) == 1 &&
              para_kind(48) == K_NUMBER && item_num(48) == 2);
        CHECK((sa(0) & A_FONT) == 3 && (sa(6) & A_FONT) == 0);
        md_in("Setext\n======\nnext\n");
        text_of(buf, sizeof buf);
        CHECK(strcmp(buf, "Setext\rnext\r") == 0 && (sa(0) & A_FONT) == 3);
        md_in("See [the site](http://x.y) and ![a picture](p.png); <http://a.b>.\n");
        text_of(buf, sizeof buf);
        CHECK(strcmp(buf, "See the site (http://x.y) and a picture; http://a.b.\r") == 0);
        md_in("caf\xc3\xa9 \xe2\x80\x94 \xe2\x80\x9cq\xe2\x80\x9d Stra\xc3\x9f" "e \xc3\xbc\xc3\xb1 \xe2\x80\xa6 \xf0\x9f\x98\x80!\r\n");
        text_of(buf, sizeof buf);
        CHECK(strcmp(buf, "caf\xe9 -- \"q\" Stra\xdf" "e \xfc\xf1 ... ?!\r") == 0);
        md_in("a\\*b\\* 5 &lt; 6 &amp; 7&nbsp;8 &copy; 2 * 3 = 6 snake_case_name\n");
        text_of(buf, sizeof buf);
        CHECK(strcmp(buf, "a*b* 5 < 6 & 7 8 &copy; 2 * 3 = 6 snake_case_name\r") == 0);
        md_in("```c\nint *p = 0;\n  x\n```\nafter\n");
        text_of(buf, sizeof buf);
        CHECK(strcmp(buf, "int *p = 0;\r  x\r\rafter\r") == 0 && sa(0) == 2 && sa(5) == 2 && sa(17) == 0);
        md_in("one\n\n<div style=\"page-break-after: always\"></div>\n\ntwo\n");
        text_of(buf, sizeof buf);
        CHECK(strcmp(buf, "one\r\ftwo\r") == 0);
        md_in("<div align=\"center\">\n\nMid\nline\\\nnext  \nlast\n\n</div>\n\n> quoted\n");
        text_of(buf, sizeof buf);
        CHECK(strcmp(buf, "Mid line\rnext\rlast\r\rquoted\r") == 0);
        CHECK(((sa(8) & A_ALIGN) >> A_ALIGN_SHIFT) == 1 && ((sa(18) & A_ALIGN) >> A_ALIGN_SHIFT) == 1 &&
              sa(19) == 0 && ((sa(26) & A_ALIGN) >> A_ALIGN_SHIFT) == 0);
        md_in("**a***b* ***c*** *d **e** f*\n");
        text_of(buf, sizeof buf);
        CHECK(strcmp(buf, "ab c d e f\r") == 0);
        CHECK(sa(0) == A_BOLD && sa(1) == A_ITAL && sa(3) == (A_BOLD | A_ITAL) && sa(5) == A_ITAL &&
              sa(7) == (A_BOLD | A_ITAL) && sa(9) == A_ITAL);
        md_in("");
        CHECK(doc_len == 1 && txt_at(0) == CH_CR);

        /* random documents: exported, imported, exported again: the same */
        srand(getenv("SEED") ? (unsigned)atoi(getenv("SEED")) : 1);
        for (k = 0; k < 300; k++) {
            static const char *w[] = {"word", "*star*", "a_b", "[x]", "#1", "<tag>", "&amp;", "1.", "- ", "`q`",
                                      "  ", "\\", "Mono", "~", ">", "!", "x+y", "=", "C64", "''",
                                      "caf\xe9", "\xc4\xdf\xff", "\xa0", "\xbf\xa1"};
            u16 nw = (u16)(1 + rand() % 60), q;
            doc_len = 0;
            for (q = 0; q < nw; q++) {
                const char *t = w[rand() % 24];
                u8 a = (u8)(rand() % 4 == 0 ? rand() % 32 : 0), r = (u8)(rand() % 14);
                for (; *t; t++) {
                    txt_put(doc_len, (u8)*t);
                    sput(doc_len++, a);
                }
                if (r < 9) {
                    txt_put(doc_len, ' ');
                    sput(doc_len++, (u8)(rand() % 3 == 0 ? a : 0));
                } else {
                    txt_put(doc_len, r == 13 ? CH_FF : CH_CR);
                    atr_put(doc_len++, (u8)((rand() % 4) << A_ALIGN_SHIFT));
                }
            }
            txt_put(doc_len, CH_CR);
            atr_put(doc_len++, 0);
            n = md_out(m1);
            md_fixed_point(m1, m2, "random");
            if (fails > 10) break;
        }
    }

    /* RTF (RTF.OVL): Writer's documents round trip exactly */
    {
        static char r[32000];
        u16 k, n;
        doc_load_help();
        rtf_round("the help");
        n = rtf_out(r);
        CHECK(n > 0 && strncmp(r, "{\\rtf1\\ansi", 11) == 0 && r[n - 1] == '}');
        /* escapes, page breaks (after text, and an empty paragraph's), every
         * style and alignment */
        wp_cmd(13);
        for (k = 0; k < 40; k++) {
            txt_put(k, (u8)("a{b}c\\d e  f\rg\f\fh  \ri j\rk\r"[k % 20]));
            sput(k, (u8)(txt_at(k) == CH_CR || txt_at(k) == CH_FF ? (k % 4) << A_ALIGN_SHIFT : k % 32));
        }
        txt_put(40, CH_CR);
        atr_put(40, 0);
        doc_len = 41;
        rtf_round("styles and breaks");
        /* random documents */
        srand(getenv("SEED") ? (unsigned)atoi(getenv("SEED")) : 1);
        for (n = 0; n < 200 && fails < 10; n++) {
            u16 len = (u16)(1 + rand() % 3000);
            for (k = 0; k < len; k++) {
                int r2 = rand() % 40;
                txt_put(k, (u8)(r2 == 0 ? CH_CR : r2 == 1 ? CH_FF : r2 < 8 ? ' ' : r2 < 12 ? 0xA0 + rand() % 96 : 32 + rand() % 95));
                sput(k, (u8)(txt_at(k) == CH_CR || txt_at(k) == CH_FF ? (rand() % 4) << A_ALIGN_SHIFT
                                                                 : rand() % 3 ? sa(k ? k - 1 : 0) & A_STYLE : rand() % 32));
            }
            txt_put(len, CH_CR);
            atr_put(len, 0);
            doc_len = (u16)(len + 1);
            rtf_round("a random document");
        }

        /* as other word processors write it: a font table by family,
         * destinations left out, Unicode, Windows-1252, \bin, tables, a
         * page break after text, a bigger size as Head, more than 1 KB */
        {
            static char w[4000];
            static const char *body =
                "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1"
                "{\\fonttbl{\\f0\\froman\\fcharset0 Times New Roman{\\*\\panose 02020603}" ";}"
                "{\\f1\\fswiss Arial;}{\\f2\\fmodern\\fprq1 Courier New;}}"
                "{\\colortbl;\\red0\\green0\\blue0;}"
                "{\\stylesheet{\\s0 Normal;}{\\s1\\fs32 heading 1;}}"
                "{\\info{\\title Ignored}{\\author Nobody}}"
                "{\\*\\generator Some Writer 1.0;}"
                "\\pard\\plain\\s1\\qc\\fs32\\b Title\\par\r\n"
                "\\pard\\plain\\f1\\fs24 Plain {\\b bold {\\i both}} {\\ul under\\ulnone  no}"
                " {\\f2 mono} {\\f0 serif}\\par\r\n"
                "\\pard\\qj caf\\'e9 \\'93q\\'94 \\u8212\\'97 \\u8216?x\\u-4064? {\\pict\\pngblip 89504e47}"
                "{\\*\\bkmkstart b}{\\field{\\*\\fldinst HYPERLINK x}{\\fldrslt link}} \\{\\}\\\\\\par\r\n"
                "\\pard a\\tab b\\line c\\page d\\par\r\n"
                "\\trowd\\cellx1000\\cellx2000 x\\cell y\\cell\\row\r\n"
                "\\pard{\\object\\objemb{\\*\\objdata 0102}}e\\bin3 }{\\f\\par\r\n";
            u16 len;
            static const char *want =
                "Title\rPlain bold both under no mono serif\rcaf\xe9 \"q\" -- 'x? link {}\\\ra b\rc\fd\rx  y  \ref\r";
            strcpy(w, body);
            len = (u16)strlen(w);
            while (len < 1300) w[len++] = '\n';    /* (line ends: nothing in RTF) */
            strcpy(w + len, "{\\pard\\qr Last}\\par}");
            rtf_in(w);
            text_of(r, sizeof r);
            if (strncmp(r, want, strlen(want)) != 0 || strcmp(r + strlen(want), "Last\r") != 0) {
                printf("FAIL: RTF import:\n  got:  ");
                show(r);
                printf("  want: ");
                show(want);
                fails++;
            }
            CHECK((sa(0) & A_FONT) == 3 && (sa(0) & A_BOLD));                 /* Title: 16 pt, Head */
            CHECK(((sa(5) & A_ALIGN) >> A_ALIGN_SHIFT) == 1);                  /* centred */
            CHECK((sa(6) & A_STYLE) == 0);                                     /* Plain: Arial, Sans */
            CHECK((sa(12) & A_STYLE) == A_BOLD && (sa(17) & A_STYLE) == (A_BOLD | A_ITAL));
            CHECK((sa(22) & A_UNDER) && !(sa(28) & A_UNDER));
            CHECK((sa(31) & A_FONT) == 2 && (sa(36) & A_FONT) == 1 && (sa(35) & A_FONT) == 0);
        }
    }

    /* paragraph kinds (#4): the styles applied, typed after, laid out,
     * shown, exported and imported */
    {
        static char m1[4000], m2[4000], tags[48];
        struct line L;
        u8 r;
        wp_cmd(13);
        if (view_mode != VIEW_PAGE) wp_cmd(7);
        type("Title\nBody text here\nmore");
        wp_key(147);                                    /* (the start) */
        wp_cmd(32 + K_H1);                              /* no selection: the cursor's paragraph */
        CHECK(para_kind(0) == K_H1 && sa(0) == (F_HEAD | A_BOLD) && sa(4) == (F_HEAD | A_BOLD) && para_kind(6) == 0);
        CHECK(wp_get(16) == K_H1);                      /* GET_KIND */
        CHECK(raw_tag(0, tags) >= 4 && memcmp(tags, "[H1]", 4) == 0);
        /* RETURN at a heading's end: a normal paragraph after it */
        wp_cmd(14);
        type("\nx");
        CHECK(para_kind(0) == K_H1 && para_kind(6) == K_NORMAL && sa(6) == 0);
        wp_key(20);
        wp_key(20);                                     /* (the empty paragraph gone: the heading kept) */
        text_of(buf, sizeof buf);
        CHECK(strcmp(buf, "Title\rBody text here\rmore\r") == 0 && para_kind(0) == K_H1);
        /* part of a paragraph: a character style */
        sel_on = 1;
        sel_anchor = 11;
        cur = 15;
        wp_cmd(32 + K_CODE);
        CHECK(sa(10) == 0 && sa(11) == F_MONO && sa(14) == F_MONO && sa(15) == 0 && para_kind(6) == K_NORMAL);
        /* part of one, all of the next: each its way */
        sel_anchor = 16;
        cur = 25;
        wp_cmd(32 + K_QUOTE);
        CHECK(sa(16) == (F_SERIF | A_ITAL) && para_kind(6) == K_NORMAL);
        CHECK(para_kind(21) == K_QUOTE && sa(21) == (F_SERIF | A_ITAL) && sa(24) == (F_SERIF | A_ITAL));
        /* a whole paragraph: the characters in the old kind's style take
         * the new one's, others keep their font (and what was added) */
        sel_on = 0;
        cur = 8;
        wp_cmd(32 + K_QUOTE);
        CHECK(para_kind(6) == K_QUOTE && sa(6) == (F_SERIF | A_ITAL) && sa(11) == (F_MONO | A_ITAL));
        wp_cmd(32 + K_NORMAL);
        CHECK(para_kind(6) == K_NORMAL && sa(6) == 0 && sa(11) == F_MONO && sa(16) == 0);
        CHECK(para_kind(21) == K_QUOTE);
        /* a quote laid out with its indent */
        get_line(line_of(21), &L);
        CHECK(L.x == 24 && (L.kind & A_KIND) == K_QUOTE);
        /* the top bar's style field, the drop-down */
        cur = 22;
        wp_key(29);
        CHECK(strstr(mod_area.r.bar_text[0], "Quote") != NULL);
        wp_drop(K_H2);
        for (r = 0, i = 0; i < 6 * 24; i++) r |= MEM[BITMAP_LIN + (u32)((16 + i) >> 3) * 640 + ((16 + i) & 7) + 8];
        CHECK(r != 0);
        wp_drop(0xFF);

        /* Markdown: each kind, and back */
        md_in("# One\n\n## Two\n\n### Three\n\n#### Four\n\ntext *it*\n\n> q1 *it* **b**\n> q2\\\n> q3\n>\n> q4\n\n"
              "```\ncode\n\n  more\n```\n\nSetext\n------\n");
        text_of(buf, sizeof buf);
        /* (no empty paragraph after a heading; q1's next line is the same
         * paragraph, q2's \ a hard break, > alone a paragraph's end) */
        CHECK(strcmp(buf, "One\rTwo\rThree\rFour\rtext it\r\rq1 it b q2\rq3\r\rq4\r\rcode\r\r  more\r\rSetext\r") == 0);
        CHECK(para_kind(0) == K_H1 && para_kind(4) == K_H2 && para_kind(8) == K_H3 && para_kind(14) == K_H3 &&
              para_kind(19) == K_NORMAL);
        CHECK(sa(0) == (F_HEAD | A_BOLD) && sa(8) == (F_SERIF | A_BOLD) && sa(24) == A_ITAL);
        CHECK(para_kind(28) == K_QUOTE && sa(28) == (F_SERIF | A_ITAL) && sa(34) == (F_SERIF | A_ITAL | A_BOLD));
        CHECK(para_kind(39) == K_QUOTE && para_kind(42) == K_NORMAL && para_kind(43) == K_QUOTE);
        CHECK(para_kind(47) == K_CODE && para_kind(52) == K_CODE && para_kind(53) == K_CODE && sa(47) == F_MONO);
        CHECK(para_kind(61) == K_H2 && sa(61) == F_HEAD);
        md_out(m1);
        CHECK(strstr(m1, "# One\n\n## Two\n\n### Three\n\n### Four\n\ntext *it*\n\n> q1 it **b** q2\\\n> q3\n\n> q4") != NULL);
        CHECK(strstr(m1, "```\ncode\n\n  more\n```\n\n## Setext") != NULL);
        md_fixed_point(m1, m2, "kinds");
        rtf_round("kinds (Markdown)");
        wp_redraw();
        screen_ppm("build/kinds_page.ppm");
        wp_cmd(7);
        screen_ppm("build/kinds_raw.ppm");
        wp_cmd(7);

        /* RTF from elsewhere: the stylesheet's names, \outlinelevel */
        rtf_in("{\\rtf1\\ansi{\\stylesheet{\\s0 Normal;}{\\s2\\sbasedon0\\cf1 heading 1;}{\\s7 Intense Quote;}"
               "{\\s9 HTML Preformatted;}{\\*\\cs10 Strong;}{\\s11 Title;}{\\s12 Subtitle;}}"
               "\\pard\\s2 A\\par\\pard\\s7 B\\par\\pard\\s9 C\\par\\pard\\outlinelevel1 D\\par"
               "\\pard\\s11 E\\par\\pard\\s12 F\\par\\pard G\\par}");
        text_of(buf, sizeof buf);
        CHECK(strcmp(buf, "A\rB\rC\rD\rE\rF\rG\r") == 0);
        CHECK(para_kind(0) == K_H1 && para_kind(2) == K_QUOTE && para_kind(4) == K_CODE && para_kind(6) == K_H2 &&
              para_kind(8) == K_H1 && para_kind(10) == K_NORMAL && para_kind(12) == K_NORMAL);

        /* lists: bulleted and numbered, nested, numbered as they are */
        md_in("- one\n- two\n    1. a\n    2. b\n        - deep\n    3. c\n- three\n\n1. x\n\n2. y\n3) z\n\ntext\n");
        text_of(buf, sizeof buf);
        CHECK(strcmp(buf, "one\rtwo\ra\rb\rdeep\rc\rthree\rx\ry\rz\r\rtext\r") == 0);
        CHECK(sa(3) == K_BULLET && sa(7) == K_BULLET && sa(9) == (K_NUMBER | 1 << A_LEVEL_SHIFT) &&
              sa(16) == (K_BULLET | 2 << A_LEVEL_SHIFT) && sa(18) == (K_NUMBER | 1 << A_LEVEL_SHIFT) &&
              sa(24) == K_BULLET && sa(25) == 0);
        CHECK(item_num(8) == 1 && item_num(10) == 2 && item_num(17) == 3);  /* (deep passed over) */
        CHECK(item_num(25) == 1 && item_num(27) == 2 && item_num(29) == 3); /* (a loose list: one list) */
        CHECK(para_kind(32) == K_NORMAL);
        md_out(m1);
        CHECK(strstr(m1, "- one\n- two\n    1. a\n    2. b\n        - deep\n    3. c\n- three\n1. x\n2. y\n3. z\n\ntext") != NULL);
        md_fixed_point(m1, m2, "lists");
        rtf_round("lists");
        rtf_out(m1);
        CHECK(strstr(m1, "\\pard\\s7\\ilvl1\\li720\\fi-360\\ql{\\listtext 2.\\tab}") != NULL);
        /* drawn: a number in the indent; renumbered when one is taken out */
        wp_redraw();
        get_line(line_of(10), &L);
        CHECK(L.x == 48 && (L.kind & L_FIRST));
        screen_ppm("build/lists_page.ppm");
        cur = 8;                                    /* "a": out of the list (level 1, then 0, then normal) */
        wp_key(20);
        CHECK(para_kind(8) == K_NUMBER && (sa(9) & A_LEVEL) == 0 && strcmp((text_of(buf, sizeof buf), buf), "one\rtwo\ra\rb\rdeep\rc\rthree\rx\ry\rz\r\rtext\r") == 0);
        CHECK(item_num(10) == 1);                   /* (b: the first at level 1 now) */
        wp_key(9);                                  /* TAB: back in */
        CHECK((sa(9) & A_LEVEL) == 1 << A_LEVEL_SHIFT && item_num(10) == 2);
        wp_key(20);
        wp_key(20);
        CHECK(para_kind(8) == K_NORMAL && sa(9) == 0 && item_num(10) == 1);
        /* RETURN on an empty item: out of the list */
        wp_cmd(13);
        wp_cmd(32 + K_BULLET);
        type("p\n");
        CHECK(para_kind(0) == K_BULLET && para_kind(2) == K_BULLET);
        type("\n");
        CHECK(doc_len == 3 && para_kind(2) == K_NORMAL);
        type("q");
        wp_key(147);
        wp_key(9);
        wp_cmd(32 + K_NUMBER);                      /* (a list's level kept as it changes kind) */
        CHECK(sa(1) == (K_NUMBER | 1 << A_LEVEL_SHIFT));
        wp_cmd(32 + K_NORMAL);
        CHECK(sa(1) == 0);
        wp_drop(K_NUMBER);
        screen_ppm("build/style_drop.ppm");
        wp_drop(0xFF);

        /* RTF lists as Word writes them: \listtext before \pard, \ilvl */
        rtf_in("{\\rtf1\\ansi{\\stylesheet{\\s0 Normal;}{\\s20 List Paragraph;}}"
               "{\\listtext\\pard\\plain\\f3 \\'b7\\tab}\\pard\\s20\\ls1\\ilvl0 B1\\par"
               "{\\listtext\\pard\\plain 1.\\tab}\\pard\\s20\\ls2\\ilvl1 N1\\par"
               "{\\listtext\\pard\\plain 2.\\tab}\\pard\\s20\\ls2\\ilvl1 N2\\par\\pard After\\par}");
        text_of(buf, sizeof buf);
        CHECK(strcmp(buf, "B1\rN1\rN2\rAfter\r") == 0);
        CHECK(sa(2) == K_BULLET && sa(5) == (K_NUMBER | 1 << A_LEVEL_SHIFT) && item_num(6) == 2 && sa(11) == 0);

        /* a version 1 file: its paragraph ends' style bits cleared */
        wp_cmd(13);
        type("ab");
        atr_put(2, (u8)(A_BOLD | 3 | (2 << A_ALIGN_SHIFT)));
        n = doc_pack();
        MEM[SCRATCH_LIN + 2] = 1;
        CHECK(doc_unpack() == 0 && sa(2) == (2 << A_ALIGN_SHIFT) && para_kind(0) == K_NORMAL);
        MEM[SCRATCH_LIN + 2] = 4;
        CHECK(doc_unpack() == 1);                       /* (a later version: refused) */
        /* the style table: a version 2 file (a character's attribute: its
         * style), and a version 3 file's table merged into the session's */
        {
        u16 k;
        wp_cmd(13);
        type("abcd");
        sput(1, A_BOLD);
        sput(2, F_SERIF | A_UNDER);
        n = doc_pack();
        CHECK(n == 5 + 2 * 5 + 3 + st_n && MEM[SCRATCH_LIN + 15] == ST_SIZE);
        for (k = 0; k < 4; k++) MEM[SCRATCH_LIN + 10 + k] = sa(k);
        MEM[SCRATCH_LIN + 2] = 2;
        CHECK(doc_unpack() == 0 && sa(0) == 0 && sa(1) == A_BOLD && sa(2) == (F_SERIF | A_UNDER) && sa(3) == 0);
        k = st_n;
        n = doc_pack();
        MEM[SCRATCH_LIN + 16] = 41;                     /* (the file's table: 41 entries, 40 bold italic) */
        for (n = 32; n < 40; n++) MEM[SCRATCH_LIN + 18 + n] = 0;
        MEM[SCRATCH_LIN + 18 + 40] = A_BOLD | A_ITAL;
        MEM[SCRATCH_LIN + 10 + 1] = 40;
        MEM[SCRATCH_LIN + 10 + 3] = 200;                /* (past the table: plain) */
        CHECK(doc_unpack() == 0 && sa(1) == (A_BOLD | A_ITAL) && sa(2) == (F_SERIF | A_UNDER) && sa(3) == 0 &&
              atr_at(1) == sty_find(A_BOLD | A_ITAL) && atr_at(1) < 32 && st_n == k);
        MEM[SCRATCH_LIN + 15] = 2;                      /* (another entry size: refused) */
        CHECK(doc_unpack() == 1);
        }
        (void)n;
    }

    /* Latin-1 (#1): typed with Compose, measured, exported and imported */
    {
        static char m[2000];
        static const u8 keys[] = {
            1, '"', 0x41,               /* "a: a with diaeresis */
            1, 0x41, '"',               /* either order */
            1, 0x53, 0x53,              /* ss: sharp s */
            1, '^', 0xC5,               /* ^E: E with circumflex */
            1, 0xDE, 0x4E,              /* ~n (GET gives ~ as $DE) */
            1, 0x51, 0x51,              /* qq: nothing */
        };
        wp_cmd(13);
        for (i = 0; i < (int)sizeof keys; i++) {
            wp_key(keys[i]);
            if (keys[i] == 1) CHECK(msg[0] != 0);
        }
        CHECK(msg[0] != 0);                                         /* "Nothing to compose" */
        CHECK(doc_len == 6 && txt_at(0) == 0xE4 && txt_at(1) == 0xE4 && txt_at(2) == 0xDF && txt_at(3) == 0xCA &&
              txt_at(4) == 0xF1 && cur == 5);
        CHECK(char_w(0xE4, 0) == char_w('a', 0) && char_w(0xE4, A_BOLD) == char_w(0xE4, 0) + 1);
        CHECK(char_w(0xC4, 3) > 8 && char_w(0xA0, 2) == 8);
        check_layout();
        screen_ppm("build/latin_page.ppm");
        wp_cmd(7);
        screen_ppm("build/latin_raw.ppm");
        wp_cmd(7);
        /* Markdown: UTF-8 */
        md_out(m);
        CHECK(strcmp(m, "\xc3\xa4\xc3\xa4\xc3\x9f\xc3\x8a\xc3\xb1\n\n") == 0);
        md_in(m);
        CHECK(doc_len == 6 && txt_at(0) == 0xE4 && txt_at(3) == 0xCA && txt_at(4) == 0xF1);
        /* RTF: \'hh */
        rtf_out(m);
        CHECK(strstr(m, "\\'e4\\'e4\\'df\\'ca\\'f1") != NULL);
        rtf_round("Latin-1");
        /* PETSCII text: as ASCII */
        CHECK(doc_export() == 6 && MEM[SCRATCH_LIN] == 'A' && MEM[SCRATCH_LIN + 2] == 'S' &&
              MEM[SCRATCH_LIN + 3] == 0xC5);
    }

    /* random keys: the invariants hold */
    srand(getenv("SEED") ? (unsigned)atoi(getenv("SEED")) : 1);
    doc_load_help();
    for (i = 0; i < 20000; i++) {
        static const u8 keys[] = {29, 157, 17, 145, 19, 147, 13, 20, 148, 9, 0x41, 0x42, 0x20, 0xC5, 0x2E, 1, '"'};
        int r = rand() % 40;
        if (i == 10000) wp_cmd(21);     /* the second half in list mode */
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
    if (list_on) wp_cmd(21);

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
            txt_put(doc_len - 1 + k, (u8)t[k]);
            sput(doc_len - 1 + k, a);
        }
        doc_len += k;
        if (rand() % 12 == 0) txt_put(doc_len++ - 1, CH_CR);
        txt_put(doc_len - 1, CH_CR);
        atr_put(doc_len - 1, (u8)((rand() % 4) << A_ALIGN_SHIFT));
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

    test_edit();
    test_gap();

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
