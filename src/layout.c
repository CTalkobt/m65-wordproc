/* layout.c — the document broken into lines (and pages)
 *
 * Page view: each character in its font (bold one pixel wider), words
 * wrapped at TEXT_W, each paragraph aligned, lines placed on pages of
 * TEXT_H with a page break (FF) starting a new one. A line's y is in
 * document pixels: page p's top is at PAGE_GAP + p * PAGE_STRIDE.
 *
 * Raw view: one 8 x 16 cell a character, the formatting codes shown as
 * tags before the characters where they change ("[B]", "[/I]", "[Serif]",
 * "[Center]" at a paragraph's start), a CR as a paragraph mark, an FF as
 * "[Page]"; words wrapped at 80 cells.
 */
#include "wp.h"

u16 nlines;
u16 view_y;
u8 npages;

const struct font *const fonts[NFONTS] = { &font_sans, &font_serif, &font_mono, &font_head };
const char *const font_names[NFONTS] = { "Sans", "Serif", "Mono", "Head" };
static const char *const align_tags[4] = { "", "[Center]", "[Right]", "[Just]" };

/* the paragraph kinds (wp.h): their characters' style, names, raw view tags */
const u8 kind_attr[KIND_TAB] = { F_SANS, F_HEAD | A_BOLD, F_HEAD, F_SERIF | A_BOLD, F_SERIF | A_ITAL, F_MONO };
const char *const kind_names[KIND_TAB] = { "Normal", "Heading 1", "Heading 2", "Heading 3", "Quote", "Code", "Bullet",
                                           "Numbered" };
static const char *const kind_tags[KIND_TAB] = { "", "[H1]", "[H2]", "[H3]", "[Quote]", "[Code]", "[List]", "[Num]" };
/* the indent of a quote, a code block */
static const u8 kind_indent[KIND_TAB] = { 0, 0, 0, 0, 24, 16, 24, 24 };

/* a paragraph's indent: its kind's, and a list item's 24 more a level */
u8 para_indent(u8 kl) { return (u8)(kind_indent[kl & A_KIND] + ((kl & A_LEVEL) >> A_LEVEL_SHIFT) * 24); }

static u8 is_break(u8 c) { return c == CH_CR || c == CH_FF; }

u8 char_w(u8 c, u8 a) {
    const struct font *f = fonts[a & A_FONT];
    u8 w;
    if (is_break(c)) return 0;
    if (c < FONT_FIRST || (c > 126 && c < FONT_HI)) c = '?';
    w = mem_get(f->offs + (u16)((c - FONT_FIRST) * 3));
    return (u8)(w + ((a & A_BOLD) ? 1 : 0));
}

/* ---- raw view tags ---- */
static u8 tag_put(char *out, u8 n, const char *s) {
    while (*s) out[n++] = *s++;
    return n;
}

/* the style of the last character shown before i (not a CR / FF; 0 at the start) */
u8 prev_style(u16 i) {
    while (i > 0) {
        i--;
        if (!is_break(txt_at(i))) return sty(atr_at(i));
    }
    return 0;
}

/* the tags shown before character i (into out, when not NULL); their length */
u8 raw_tag(u16 i, char *out) {
    u8 n = 0, prev, now, c = txt_at(i);
    if (!out) out = mod_area.r.tag_buf;
    if (i == 0 || is_break(txt_at(i - 1))) {
        u8 pa = atr_at(para_end(i));
        n = tag_put(out, n, kind_tags[pa & A_KIND]);
        if (pa & A_LEVEL) {             /* (a list item's level: [List2] ...) */
            out[n - 1] = (char)('1' + ((pa & A_LEVEL) >> A_LEVEL_SHIFT));
            out[n++] = ']';
        }
        n = tag_put(out, n, align_tags[(pa & A_ALIGN) >> A_ALIGN_SHIFT]);
    }
    if (c == CH_FF) return tag_put(out, n, "[Page]");
    if (c == CH_CR) return n;
    prev = prev_style(i);
    now = sty(atr_at(i));
    if (prev == now) return n;
    if ((prev & A_BOLD) && !(now & A_BOLD)) n = tag_put(out, n, "[/B]");
    if ((prev & A_ITAL) && !(now & A_ITAL)) n = tag_put(out, n, "[/I]");
    if ((prev & A_UNDER) && !(now & A_UNDER)) n = tag_put(out, n, "[/U]");
    if ((prev & A_FONT) != (now & A_FONT)) {
        out[n++] = '[';
        n = tag_put(out, n, font_names[now & A_FONT]);
        out[n++] = ']';
    }
    if (!(prev & A_BOLD) && (now & A_BOLD)) n = tag_put(out, n, "[B]");
    if (!(prev & A_ITAL) && (now & A_ITAL)) n = tag_put(out, n, "[I]");
    if (!(prev & A_UNDER) && (now & A_UNDER)) n = tag_put(out, n, "[U]");
    return n;
}

/* a character's width in the current view: pixels, or cells with its tags */
static u8 tok_w(u16 i) {
    u8 c = txt_at(i);
    if (view_mode == VIEW_RAW) {
        if (c == CH_FF) return raw_tag(i, 0);
        return (u8)(raw_tag(i, 0) + 1);
    }
    return char_w(c, sty(atr_at(i)));
}

/* ---- breaking lines ----
 *
 * The table is laid out lazily: lines 0..nlines-1 are valid; when
 * layout_done is 0 the rest of the document is not laid out yet, and lz is
 * where the next line starts (its position, page, place on the page, ...).
 * ensure_pos / ensure_y / layout_idle lay out more.
 *
 * An edit (layout_edit, from doc.c) is laid out by layout_update: from the
 * line before the change, into a small buffer, until a new line is an old
 * one moved by the edit (the same text after the change, at the same
 * place): then the old lines from there are kept (their starts moved).
 * When none is, the lines down to the screen's end are laid out and the
 * rest is left for later. */
u8 layout_done;
struct lstate {
    u16 pos, yin, row;
    u8 page, para, al, kind;
};
static struct lstate lz;

static u8 pend, pend_multi;     /* an edit to lay out; more than one */
static u16 pend_lo, pend_hi;    /* the first position changed; the end of the change (before it) */
static int16_t pend_delta;      /* characters added (removed: negative) */

#define tmp mod_area.r.lay_tmp     /* (the drawing's memory: not in use while laying out) */

/* The line table is in bank 4: a line is read and written whole (one DMA)
 * and its fields used near, as each far LINES[l].field costs ~60 bytes of
 * code (cc45 #585). */
static u32 line_addr(u16 l) { return LINES_LIN + (u32)l * sizeof(struct line); }

void get_line(u16 l, struct line *L) { mem_read((u8 *)L, line_addr(l), sizeof *L); }

static void store(u16 l, const struct line *L) { mem_write(line_addr(l), (const u8 *)L, sizeof *L); }

u16 line_start(u16 l) {
    u32 a = line_addr(l);
    return (u16)(mem_get(a) | (mem_get(a + 1) << 8));
}

/* the end of the line starting at pos (after its last character); *hard:
 * it ends a paragraph */
static u16 break_line(u16 pos, u16 width, u8 *hard) {
    u16 i = pos, x = 0, brk = 0;
    u8 c, w;
    for (;;) {
        c = txt_at(i);
        if (is_break(c)) {
            if (view_mode == VIEW_RAW && x + tok_w(i) > width && i > pos) break;
            *hard = 1;
            return (u16)(i + 1);
        }
        w = tok_w(i);
        if (x + w > width && i > pos) {
            if (c == ' ') {         /* spaces hang past the edge */
                while (txt_at(i) == ' ') i++;
                if (is_break(txt_at(i))) {
                    *hard = 1;
                    return (u16)(i + 1);
                }
                break;
            }
            if (brk > pos) i = brk;
            break;
        }
        x += w;
        if (c == ' ') brk = (u16)(i + 1);
        i++;
    }
    *hard = 0;
    return i;
}

/* the state at the start of laid-out line l (l = 0: the document's start) */
static void state_at(u16 l, struct lstate *st) {
    u16 p;
    struct line L;
    if (l == 0 || l >= nlines) {
        st->pos = st->yin = st->row = 0;
        st->page = 0;
        st->para = 1;
        st->al = st->kind = 0;
        return;
    }
    get_line(l, &L);
    p = L.start;
    st->pos = p;
    st->row = l;
    st->page = L.page;
    st->yin = (u16)(L.y - (PAGE_GAP + st->page * PAGE_STRIDE + MARGIN_TOP));
    st->para = (u8)(p == 0 || is_break(txt_at(p - 1)));
    st->al = para_al(p);
    st->kind = L.kind & (A_KIND | A_LEVEL);
}

/* the next line from st into *L, st moved past it; 0 at the document's end */
static u8 produce(struct lstate *st, struct line *L) {
    u16 pos = st->pos, end, i, width, tw, tmax;
    u8 hard, asc, desc, nsp, trail, x, ind, k;
    const struct font *f;
    if (pos >= doc_len) return 0;
    if (view_mode == VIEW_RAW) {
        end = break_line(pos, COLS, &hard);
        L->start = pos;
        L->y = (u16)(st->row * RAW_H);
        L->h = RAW_H;
        L->asc = 11;
        L->x = 0;
        L->extra = L->nsp = L->page = L->kind = 0;
        st->row++;
        st->pos = end;
        return 1;
    }
    if (st->para) {
        k = atr_at(para_end(pos));
        st->al = (u8)((k & A_ALIGN) >> A_ALIGN_SHIFT);
        st->kind = k & (A_KIND | A_LEVEL);
    }
    k = st->kind;
    ind = para_indent(k);
    tmax = TEXT_W - ind;
    end = break_line(pos, tmax, &hard);
    /* the line's metrics: its width without the spaces at its end, its
     * spaces, its tallest ascent and descent */
    asc = desc = 0;
    width = 0;
    nsp = trail = 0;
    tw = 0;
    for (i = pos; i < end; i++) {
        u8 c = txt_at(i), a = sty(atr_at(i));
        if (is_break(c)) {
            if (i > pos) continue;
            a = kind_attr[k & A_KIND];      /* (an empty paragraph: its kind's height) */
        }
        f = fonts[a & A_FONT];
        if (f->ascent > asc) asc = f->ascent;
        if (f->height - f->ascent > desc) desc = (u8)(f->height - f->ascent);
        if (is_break(c)) continue;
        x = char_w(c, a);
        width += x;
        if (c == ' ') {
            nsp++;
            trail++;
            tw += x;
        } else {
            trail = 0;
            tw = 0;
        }
    }
    width -= tw;
    nsp -= trail;
    if (st->yin + asc + desc > TEXT_H && st->yin > 0) {
        st->page++;
        st->yin = 0;
    }
    {
        u16 room = width < tmax ? (u16)(tmax - width) : 0;
        L->x = ind;
        L->extra = L->nsp = 0;
        if (st->al == 1) L->x += room / 2;
        else if (st->al == 2) L->x += room;
        else if (st->al == 3 && !hard && nsp && room < 256) {
            L->extra = (u8)room;
            L->nsp = nsp;
        }
    }
    L->start = pos;
    L->y = (u16)(PAGE_GAP + st->page * PAGE_STRIDE + MARGIN_TOP + st->yin);
    L->h = (u8)(asc + desc);
    L->asc = asc;
    L->page = st->page;
    L->kind = (u8)(k | (st->para ? L_FIRST : 0));
    st->yin += asc + desc;
    if (hard && txt_at(end - 1) == CH_FF) {
        st->page++;
        st->yin = 0;
    }
    st->para = hard;
    st->pos = end;
    st->row++;
    return 1;
}

static void finish(void) {
    struct line L;
    layout_done = 1;
    npages = 1;
    if (view_mode == VIEW_PAGE && nlines) {
        get_line(nlines - 1, &L);
        npages = (u8)(L.page + 1);
    }
}

/* up to n more lines laid out */
static void extend(u16 n) {
    struct line L;
    while (!layout_done && n--) {
        if (nlines >= MAX_LINES || !produce(&lz, &L)) {
            finish();
            return;
        }
        store(nlines++, &L);
    }
}

void ensure_pos(u16 p) {
    while (!layout_done && lz.pos <= p) extend(16);
}

void ensure_y(u16 y) {
    struct line L;
    while (!layout_done) {
        if (nlines) {
            get_line(nlines - 1, &L);
            if (L.y >= y) return;
        }
        extend(16);
    }
}

/* a little more of the document laid out (BASIC calls it while no key
 * waits); 1 while there is more to do */
u8 layout_idle(void) {
    if (layout_done) return 0;
    extend(12);
    return (u8)!layout_done;
}

/* everything laid out again (later, as needed) */
void layout_all(void) {
    pend = 1;
    pend_multi = 1;
    pend_lo = 0;
}

/* the document changed at lo..hi (hi before the change), delta characters
 * added; laid out by the next layout_update */
void layout_edit(u16 lo, u16 hi, int16_t delta) {
    if (pend) {
        pend_multi = 1;
        if (lo < pend_lo) pend_lo = lo;
        return;
    }
    pend = 1;
    pend_multi = 0;
    pend_lo = lo;
    pend_hi = hi;
    pend_delta = delta;
}

static u16 line_of_laid(u16 pos) {
    u16 lo = 0, hi = nlines - 1, mid;
    while (lo < hi) {
        mid = (u16)((lo + hi + 1) / 2);
        if (line_start(mid) <= pos) lo = mid;
        else hi = (u16)(mid - 1);
    }
    return lo;
}

/* the lines from..from+n-1 moved to line to (bounced through attic RAM
 * when they move up: the DMA copies upwards only without overlap; not the
 * scratch area, where undo's copy is) */
static void move_lines(u16 to, u16 from, u16 n) {
    u16 bytes = (u16)(n * sizeof(struct line));
    u32 dst = line_addr(to), src = line_addr(from);
    if (to == from || !n) return;
    if (to > from) {
        mem_copy(LBOUNCE_LIN, src, bytes);
        src = LBOUNCE_LIN;
    }
    mem_copy(dst, src, bytes);
}

void layout_update(void) {
    u16 l0 = 0, k, i, old_n = nlines, base, ks;
    u8 n = 0, old_done = layout_done, conv = !pend_multi;
    struct lstate st, old_lz = lz;
    struct line O;
    if (!pend) return;
    pend = 0;
    if (pend_lo > 0 && nlines) {
        if (!layout_done && pend_lo >= lz.pos) return;     /* not laid out yet anyway */
        l0 = line_of_laid(pend_lo);
        if (l0) l0--;
    }
    if (!nlines) conv = 0;
    state_at(l0, &st);
    k = l0;
    for (;;) {
        struct line *L = &tmp[n];
        if (l0 + n >= MAX_LINES || !produce(&st, L)) {
            for (i = 0; i < n; i++) store((u16)(l0 + i), &tmp[i]);
            nlines = (u16)(l0 + n);
            finish();
            return;
        }
        if (conv) {
            /* an old line, moved by the edit, starting where this one does */
            while (k < old_n && (int32_t)line_start(k) + pend_delta < (int32_t)L->start) k++;
            ks = 0;
            if (k < old_n) {
                get_line(k, &O);
                ks = O.start;
            }
            if (k < old_n && (int32_t)ks + pend_delta == (int32_t)L->start && ks > pend_hi && O.y == L->y &&
                O.page == L->page && (view_mode == VIEW_PAGE || !is_break(txt_at(L->start - 1))) &&
                (u16)(l0 + n + (old_n - k)) <= MAX_LINES) {
                /* the rest is as it was: the old lines from k, after the new ones */
                base = (u16)(l0 + n);
                move_lines(base, k, (u16)(old_n - k));
                for (i = 0; i < n; i++) store((u16)(l0 + i), &tmp[i]);
                {
                    /* their starts moved by the edit */
                    struct line FAR *D = &LINES[base];
                    for (i = base; i < base + (old_n - k); i++, D++) D->start = (u16)(D->start + pend_delta);
                }
                nlines = (u16)(base + (old_n - k));
                layout_done = old_done;
                if (!old_done) {
                    lz = old_lz;
                    lz.pos = (u16)(lz.pos + pend_delta);
                    lz.row = (u16)(lz.row + base - k);
                }
                return;
            }
        }
        n++;
        if (n == TMP_LINES || (L->start > cur && L->y > view_y + 2 * VIEW_H)) break;
    }
    /* no old line to keep: the new ones, the rest later */
    for (i = 0; i < n; i++) store((u16)(l0 + i), &tmp[i]);
    nlines = (u16)(l0 + n);
    lz = st;
    layout_done = 0;
    npages = 255;
}

/* the whole document laid out now (the tests; a full redraw) */
void layout(void) {
    layout_all();
    layout_update();
    while (!layout_done) extend(64);
}

u16 line_end(u16 l) {
    if (l + 1 < nlines) return line_start(l + 1);
    return layout_done ? doc_len : lz.pos;
}

/* the line holding pos (laid out first if need be) */
u16 line_of(u16 pos) {
    ensure_pos(pos);
    return line_of_laid(pos);
}

/* justify: the extra pixels after the k-th space of a line */
static u8 just_share(u8 extra, u8 nsp, u8 k) {
    return (u8)(((u16)extra * (k + 1)) / nsp - ((u16)extra * k) / nsp);
}

/* walking along line *L in the page view: each character's width with
 * the justification's share of a space */
void just_init(struct just *j, const struct line *L) {
    j->extra = L->extra;
    j->nsp = L->nsp;
    j->k = 0;
}

u8 just_w(u8 c, u8 a, struct just *j) {
    u8 w = char_w(c, a);
    if (c == ' ' && j->k < j->nsp) w += just_share(j->extra, j->nsp, j->k++);
    return w;
}

/* pos's x in line l: pixels from the text's left (page), cells (raw: after
 * the tags before pos, where the cursor is drawn) */
u16 x_of(u16 l, u16 pos) {
    struct line L;
    struct just j;
    u16 i, x;
    get_line(l, &L);
    i = L.start;
    if (view_mode == VIEW_RAW) {
        x = 0;
        for (; i < pos; i++) x += tok_w(i);
        return (u16)(x + raw_tag(pos, 0));
    }
    just_init(&j, &L);
    x = L.x;
    for (; i < pos; i++) x += just_w(txt_at(i), sty(atr_at(i)), &j);
    return x;
}

/* the position in line l nearest x */
u16 pos_at_x(u16 l, u16 x) {
    struct line L;
    struct just j;
    u16 i, e = (u16)(line_end(l) - 1), px, nx;
    get_line(l, &L);
    i = L.start;
    just_init(&j, &L);
    px = view_mode == VIEW_RAW ? raw_tag(i, 0) : L.x;
    for (; i < e; i++) {
        if (view_mode == VIEW_RAW) {
            if (x <= px) return i;
            nx = (u16)(px + 1 + raw_tag((u16)(i + 1), 0));
        } else {
            nx = px + just_w(txt_at(i), sty(atr_at(i)), &j);
            if (x < px + (nx - px) / 2) return i;
        }
        px = nx;
    }
    return e;
}

u16 doc_height(void) {
    if (!nlines) return 0;
    if (!layout_done) return 0xFFFF;
    if (view_mode == VIEW_RAW) return (u16)(nlines * RAW_H);
    return (u16)(npages * PAGE_STRIDE + PAGE_GAP);
}

void scroll_to_cursor(void) {
    u16 l = line_of(cur), top, bot, h;
    struct line L;
    get_line(l, &L);
    top = L.y;
    bot = top + L.h;
    u16 ctx = view_mode == VIEW_RAW ? 0 : 24;
    if (top < view_y + ctx) view_y = top > ctx ? (u16)(top - ctx) : 0;
    if (bot + ctx > view_y + VIEW_H) view_y = (u16)(bot + ctx - VIEW_H);
    ensure_y((u16)(view_y + VIEW_H));
    h = doc_height();
    if (h > VIEW_H && view_y > h - VIEW_H) view_y = (u16)(h - VIEW_H);
    if (h <= VIEW_H) view_y = 0;
    if (view_mode == VIEW_RAW) view_y = (u16)(view_y / RAW_H * RAW_H);
}
