/* cold.c — code run seldom, or at most once a key (a file saved, loaded,
 * imported or exported as text; the fonts installed; the screen set up or
 * given back; a key or BASIC's text as ASCII, a message filled in): on the
 * MEGA65 a code overlay, out of bank 0 (cc45 #pragma cc45 overlay, ln45:
 * M65Compiler #685)
 *
 * The overlay is kept in bank 4 (WRITER.OV1 on the disk, loaded by the
 * first SYS into the C half) and copied by DMA, for each call into it, to
 * the start of mod_area: the drawing's buffers (MOD_SCRATCH bytes, which
 * the layout and the drawing use only while they work, and which BASIC's
 * modules and DEFAULT0.OVL overwrite). So:
 *   - only api.c's SYS entries call it, each first marking the region as
 *     not holding it (ovl_fresh: it is copied in again);
 *   - nothing here may write mod_area: no layout_update, no drawing, no
 *     module (mod_run, edit_run); what it calls (doc_loaded, layout_all,
 *     the sty_ routines, mem_copy) does not.
 * Its variables are resident, as an overlay's are (m65.c's video state).
 */
#include "wp.h"
#include "msg.h"
#ifdef __cc45__
#pragma cc45 overlay(cold)
/* (the room: MOD_SCRATCH, which the Makefile gives ln45 as OVL_ROOM) */
typedef char ovl_room_is_scratch[OVL_ROOM == MOD_SCRATCH ? 1 : -1];
#endif

/* ---- moved out of bank 0 from doc.c and render.c (called after
 * ovl_fresh, before anything draws) ---- */
void doc_new(void) {
    g_len = 0;
    txt_put(0, CH_CR);
    atr_put(0, 0);
    doc_len = 1;
    cur = 0;
    sel_on = 0;
    typing_attr = 0;
    dirty = 0;
    view_y = 0;
    goal_valid = 0;
    u_have = 0;
    layout_all();
}

/* the selection to the clipboard */
void doc_copy(void) {
    u16 lo, n;
    if (!sel_on) return;
    lo = sel_lo();
    n = sel_hi() - lo;
    if (n > CLIP_MAX) n = CLIP_MAX;
    doc_out((u16)(CLIP_LIN - SCRATCH_LIN), CLIP_MAX, lo, n);
    clip_len = n;
}

/* a document just loaded or imported (doc_len characters): its final CR
 * made sure of, the cursor at its start, laid out anew; d: changed */
void doc_loaded(u8 d) {
    if (!doc_len || txt_at(doc_len - 1) != CH_CR) {
        if (doc_len < DOC_MAX) {
            atr_put(doc_len, 0);
            doc_len++;
        }
        txt_put(doc_len - 1, CH_CR);
    }
    cur = 0;
    sel_on = 0;
    view_y = 0;
    goal_valid = 0;
    dirty = d;
    u_have = 0;
    typing_attr = attr_at_cursor();
    layout_all();
}

/* the drawing's row patterns (render.c's init_rows: render_invalidate
 * made them unknown), the cells' colours unknown */
void rows_init(void) {
    u8 i;
    for (i = 0; i < COLS; i++) {
        mod_area.r.row_desk[i] = 0xFF;
        mod_area.r.row_blank[i] = 0;
        mod_area.r.row_page[i] = (i < PAGE_X / 8 || i >= (PAGE_X + PAGE_W) / 8) ? 0xFF : 0;
    }
    for (i = 0; i < ROWS; i++) mod_area.r.rowcol[i] = 0xFF;
}

/* the text as PETSCII (lower-case set) for a SEQ file (a PETSCII line's
 * as it is); the size */
u16 doc_export(void) {
    u16 i;
    u8 pet = 0, c;
    o_start();
    for (i = 0; i < doc_len; i++) {
        c = txt_at(i);
        if (i == 0 || txt_at(i - 1) == CH_CR || txt_at(i - 1) == CH_FF) pet = atr_at(para_end(i)) & A_PET;
        if (pet && c != CH_CR && c != CH_FF) {
            o_put(c);
            continue;
        }
        c = as_ascii(c);
        if (c >= 'a' && c <= 'z') c = (u8)(c - 32);
        else if (c >= 'A' && c <= 'Z') c = (u8)(c + 128);
        else if (c == CH_FF) c = CH_CR;
        else if (c == '_') c = 0xA4;
        o_put(c);
    }
    return o_n;
}

/* a PETSCII text file read (FREAD#) to SCRATCH_LIN, after GET_IMPORT_PREP
 * zeroed it, as the document; its length */
u16 doc_import(void) {
    u16 i;
    u8 c;
    build_start();
    for (i = 0; i < DOC_MAX - 1; i++) {
        c = mem_get(SCRATCH_LIN + i);
        if (!c) break;
        if (c != CH_CR) {
            if (c == 0xA0) c = ' ';
            c = petscii_to_ascii(c);
            if (!c) continue;
        }
        build_put(c);
    }
    /* past the file's end the drive gives CRs: one CR at the end */
    while (b_n > 1 && txt_at(b_n - 1) == CH_CR && txt_at(b_n - 2) == CH_CR) b_n--;
    i = build_end();
    doc_built();
    return i;
}

/* writer.fnt, BLOADed to the scratch area: its parts to FONT_LIN, HI_LIN */
void font_install(void) {
    mem_copy(FONT_LIN, SCRATCH_LIN, font_lo_size);
    mem_copy(HI_LIN, SCRATCH_LIN + font_lo_size, font_hi_size);
}

/* ---- text from BASIC and the keyboard (api.c's SYS entries) ---- */

/* PETSCII (a GET's code, the lower-case set) as ASCII; 0 if none */
u8 petscii_to_ascii(u8 c) {
    if (c >= 0x41 && c <= 0x5A) return (u8)(c + 32);
    if (c >= 0xC1 && c <= 0xDA) return (u8)(c - 128);
    if (c >= 0x61 && c <= 0x7A) return (u8)(c - 32);
    if (c >= 0x20 && c <= 0x40) return c;
    if (c == 0x5B || c == 0x5D) return c;
    if (c == 0x5C) return '\\';         /* the pound sign's key */
    if (c == 0x5E) return '^';
    if (c == 0x5F) return '_';          /* the left arrow */
    if (c == 0xA4) return '_';
    if (c == 0xDB) return '{';
    if (c == 0xDD) return '}';
    if (c == 0xDC) return '|';
    if (c == 0xDE) return '~';
    if (c == 0xBA) return '`';          /* SHIFT + @ */
    return 0;
}

/* msg: the words, characters (not the paragraph ends) and paragraphs
 * (those with a word) of the selection, or of the document */
void doc_count(void) {
    u16 i = 0, e = doc_len, w = 0, ch = 0, pa = 0;
    u8 c, in = 0, txt = 0, k;
    char b[18], t[6];
    if (sel_on) {
        i = sel_lo();
        e = sel_hi();
    }
    for (; i <= e; i++) {
        c = i < e ? txt_at(i) : CH_CR;
        if (c == CH_CR || c == CH_FF) {
            if (txt) pa++;
            txt = in = 0;
            continue;
        }
        ch++;
        if (c == ' ' || c == 0xA0) in = 0;
        else if (!in) {
            in = txt = 1;
            w++;
        }
    }
    /* "w,ch,pa" for the message's fields */
    e = 0;
    for (k = 0; k < 3; k++) {
        i = k == 0 ? w : k == 1 ? ch : pa;
        c = 0;
        do {
            t[c++] = (char)('0' + i % 10);
            i /= 10;
        } while (i);
        while (c) b[e++] = t[--c];
        b[e++] = k < 2 ? ',' : 0;
    }
    msg_fill(sel_on ? MSG_COUNTSEL : MSG_COUNT, b);
}

/* key k (its PETSCII code) as the cursor's paragraph has it: in a PETSCII
 * line the code as it is (but a control code), else as ASCII; 0: none */
u8 key_char(u8 k) {
    if (atr_at(para_end(cur)) & A_PET) return (k & 0x7F) < 0x20 && (k & 0x7F) != RVS_ON ? 0 : k;   /* (RVS ON / OFF typed) */
    return petscii_to_ascii(k);
}

/* s (PETSCII, at most max characters) as ASCII, in place ("?": none) */
void to_ascii(char *s, u8 max) {
    u8 i;
    for (i = 0; i < max && s[i]; i++) {
        u8 c = petscii_to_ascii((u8)s[i]);
        s[i] = (char)(c ? c : '?');
    }
    s[i] = 0;
}

/* Compose (CTRL+A, then two characters): the Latin-1 character the compose
 * table (writer.fnt: compose_at) gives a and b, in either order: "a -> a
 * with its diaeresis, 'e, `e, ^e, ~n, oa, ,c, ss, ae, o/, << ... (issue
 * #1); 0: none */
u8 compose_find(u8 a, u8 b) {
    u8 e[3];
    u16 i = 0;
    do {
        mem_read(e, compose_at + i, 3);
        if (e[0] == a && e[1] == b) return e[2];
        i += 3;
    } while (e[0]);
    return 0;
}

/* msg: message n, its place holders filled from p: 1 a field of p (up to
 * a comma), 2 all the rest */
void msg_fill(u8 n, const char *p) {
    const char *t = msg_get(n);
    u8 i = 0;
    char c;
    while ((c = *t++) != 0 && i < 78) {
        if ((u8)c > 2) msg[i++] = c;
        else {
            while (*p && i < 78 && (c == 2 || *p != ',')) msg[i++] = *p++;
            if (*p) p++;
        }
    }
    msg[i] = 0;
}

/* RUN/STOP ignored while editing (on 1), or not (0): the KERNAL's STOP
 * vector ($0328) pointed at a routine that answers "not pressed" as the
 * KERNAL's own nstop does (A = $FF, the flags of CMP #$7F). It must be in
 * RAM that BASIC's memory map shows (BASIC 65 runs with its ROM over
 * $2000-$BFFF), so it goes to $0378, free between the KERNAL's file tables
 * (to $0369) and its RAM code ($0380). */
#define NOSTOP 0x0378
static u16 old_stop;
void stopvec(u8 on) {
#ifdef __cc45__
    static const u8 code[5] = { 0xA9, 0xFF, 0xC9, 0x7F, 0x60 };     /* lda #$ff : cmp #$7f : rts */
    volatile u16 *v = (volatile u16 *)0x0328;
    u8 i;
    if (on) {
        for (i = 0; i < 5; i++) ((volatile u8 *)NOSTOP)[i] = code[i];
        if (*v != NOSTOP) old_stop = *v;
        *v = NOSTOP;
    } else if (old_stop) {
        *v = old_stop;
    }
#else
    (void)on;
    (void)old_stop;
#endif
}

#ifdef __cc45__
/* ---- the screen (m65.c's layer: the VIC-IV's 640 x 400 screen of 16-bit
 * characters, set up and given back) ---- */
#include <dma.h>
#define VIC(r) (*(volatile u8 *)(r))
#define COLRAM (0xFF80000UL + COLOR_OFS)

void vic_unlock(void);          /* $D02F unlocked, interrupts masked (vicunlock.s45) */

/* the registers we change, saved for video_off */
static const u8 saved_regs[] = {
    0x31, 0x54, 0x58, 0x59, 0x5D, 0x5E, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65,
    0x68, 0x69, 0x6A, 0x7B, 0x20, 0x21,
};
#define NSAVED (sizeof saved_regs)
static u8 saved[NSAVED];
static u8 is_on, screen_made;

static void make_screen(void) {
    u16 i;
    u8 __far *p = (u8 __far *)SCREEN_LIN;
    /* cell n shows character n */
    for (i = 0; i < COLS * ROWS; i++) {
        *p++ = (u8)i;
        *p++ = (u8)(i >> 8);
    }
    /* colour RAM: the first byte of every cell 0, the colour black */
    dma_fill_far_step(COLRAM, 2, COLS * ROWS, 0);
    dma_fill_far_step(COLRAM + 1, 2, COLS * ROWS, C_BLACK);
    dma_fill_far(BITMAP_LIN, COLS * ROWS * 8, 0);
    screen_made = 1;
}

void video_on(void) {
    u8 i;
    vic_unlock();               /* the VIC-IV's registers (vicunlock.s45) */
    if (!is_on) {
        for (i = 0; i < NSAVED; i++) saved[i] = VIC(0xD000 + saved_regs[i]);
        is_on = 1;
    }
    if (!screen_made) make_screen();
    VIC(0xD05D) &= 0x7F;        /* no hot registers: the KERNAL's writes leave ours */
    VIC(0xD031) |= 0x88;        /* H640, V400 (first: it resets others) */
    VIC(0xD031) &= (u8)~0x30;   /* no bitplanes, no VIC-III attributes */
    VIC(0xD011) &= (u8)~0x60;   /* text mode (no bitmap, no extended colour) */
    VIC(0xD016) &= (u8)~0x10;   /* no multicolour */
    VIC(0xD054) = (u8)((VIC(0xD054) & ~0x06) | 0x01);   /* 16-bit characters, none full-colour */
    VIC(0xD058) = COLS * 2;     /* bytes a row */
    VIC(0xD059) = 0;
    VIC(0xD05E) = COLS;         /* characters a row */
    VIC(0xD063) = 0;            /* (their high bits; the screen address's bits 24-27) */
    VIC(0xD060) = (u8)SCREEN_LIN;
    VIC(0xD061) = (u8)(SCREEN_LIN >> 8);
    VIC(0xD062) = (u8)(SCREEN_LIN >> 16);
    VIC(0xD064) = (u8)COLOR_OFS;
    VIC(0xD065) = (u8)(COLOR_OFS >> 8);
    VIC(0xD068) = (u8)BITMAP_LIN;
    VIC(0xD069) = (u8)(BITMAP_LIN >> 8);
    VIC(0xD06A) = (u8)(BITMAP_LIN >> 16);
    VIC(0xD07B) = ROWS - 1;
    VIC(0xD020) = C_DGREY;
    VIC(0xD021) = C_WHITE;
}

void video_off(void) {
    u8 i;
    if (!is_on) return;
    vic_unlock();
    VIC(0xD031) = saved[0];     /* first: it resets others */
    for (i = 1; i < NSAVED; i++) VIC(0xD000 + saved_regs[i]) = saved[i];
    is_on = 0;
}
#endif
