/* petuni.h — a PETSCII line's characters and Unicode (issue #11), for the
 * converter modules: each that includes it defines PET_OUT first (an
 * export: pet_to_uni; PET_UTF8 too: out_uni) or PET_IN (an import: uni_pet)
 *
 * A PETSCII line (a paragraph whose CR / FF has A_PET) holds PETSCII codes
 * as they are typed, shown in the upper-case / graphics set. The map is
 * Unicode's own (L2/19-025, its C64IPRI.TXT): the graphics are box drawing,
 * block elements, card suits and Symbols for Legacy Computing, which are
 * U+1FB00-U+1FBFF and kept here as 0xFBxx (the set has no U+FBxx). $60-$7F
 * have $C0-$DF's glyphs and $E0-$FF $A0-$BF's; the control codes ($00-$1F,
 * $80-$9F) have none.
 */

/* $A0-$DF */
static const u16 pet_uni[64] = {
    0x00A0, 0x258C, 0x2584, 0x2594, 0x2581, 0x258F, 0x2592, 0x2595,
    0xFB8F, 0x25E4, 0xFB87, 0x251C, 0x2597, 0x2514, 0x2510, 0x2582,
    0x250C, 0x2534, 0x252C, 0x2524, 0x258E, 0x258D, 0xFB88, 0xFB82,
    0xFB83, 0x2583, 0xFB7F, 0x2596, 0x259D, 0x2518, 0x2598, 0x259A,
    0x2500, 0x2660, 0xFB72, 0xFB78, 0xFB77, 0xFB76, 0xFB7A, 0xFB71,
    0xFB74, 0x256E, 0x2570, 0x256F, 0xFB7C, 0x2572, 0x2571, 0xFB7D,
    0xFB7E, 0x2022, 0xFB7B, 0x2665, 0xFB70, 0x256D, 0x2573, 0x25CB,
    0x2663, 0xFB75, 0x2666, 0x253C, 0xFB8C, 0x2502, 0x03C0, 0x25E5,
};

#ifdef PET_OUT
/* PETSCII c as Unicode (0xFBxx: U+1FBxx); 0: none (a control code) */
static u16 pet_to_uni(u8 c) {
    if ((c & 0x7F) < 0x20) return 0;
    if (c >= 0xE0) c -= 0x40;
    else if (c >= 0x60 && c < 0x80) c += 0x60;
    if (c >= 0xA0) return pet_uni[c - 0xA0];
    if (c == 0x5C) return 0xA3;
    if (c == 0x5E) return 0x2191;
    if (c == 0x5F) return 0x2190;
    return c;
}
#endif

#ifdef PET_UTF8
/* a Unicode character as UTF-8 (0xFBxx: U+1FBxx, four bytes; 0: none) */
static void out_uni(u16 u) {
    if (!u) return;
    if (u >= 0xFB00) {
        o_put(0xF0);
        o_put(0x9F);
        o_put((u8)(0xAC | ((u >> 6) & 3)));
    } else if (u >= 0x800) {
        o_put((u8)(0xE0 | (u >> 12)));
        o_put((u8)(0x80 | ((u >> 6) & 0x3F)));
    } else if (u >= 0x80) {
        o_put((u8)(0xC0 | (u >> 6)));
    } else {
        o_put((u8)u);
        return;
    }
    o_put((u8)(0x80 | (u & 0x3F)));
}
#endif

#ifdef PET_IN
/* Unicode character u (0xFBxx: U+1FBxx) as PETSCII: lower case as upper
 * (the set has none), other characters it has not "?" */
static u8 uni_pet(u16 u) {
    u8 k;
    if (u >= 'a' && u <= 'z') return (u8)(u - 0x20);
    if (u >= ' ' && u <= ']' && u != '\\') return (u8)u;
    if (u == 0xA3) return 0x5C;
    if (u == 0x2191) return 0x5E;
    if (u == 0x2190) return 0x5F;
    if (u == 0x25CF) return 0xD1;           /* (the map's alternatives: black circle, white bullet) */
    if (u == 0x25E6) return 0xD7;
    for (k = 0; k < 64; k++)
        if (pet_uni[k] == u) return (u8)(0xA0 + k);
    return '?';
}
#endif
