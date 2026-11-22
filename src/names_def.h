/* names_def.h — finding and defining a named style (wp.h), for the code
 * that does (names.c's overlay, the imports' modules: htmlin.c, rtfin.c),
 * each with its own copy, out of bank 0 */

/* name s's slot + 1 (0: none) */
static u8 name_slot(const char *s) {
    u8 k, j;
    char t[NAME_LEN];
    for (k = 0; k < NNAMES && st_nidx[k]; k++) {
        mem_read((u8 *)t, NAMES_LIN + (u16)k * NAME_LEN, NAME_LEN);
        for (j = 0; s[j] && s[j] == t[j]; j++) ;
        if (s[j] == t[j]) return (u8)(k + 1);
    }
    return 0;
}

/* name s (at most NAME_LEN - 1 characters taken) as style e: a name there
 * already redefined (the file's wins), else a new entry; its entry (0: no
 * room) */
static u8 name_define(const char *s, u8 e) {
    u8 k = name_slot(s), j, n;
    char t[NAME_LEN];
    if (k) {
        k = st_nidx[k - 1];
    } else {
        n = names_count();
        if (n == NNAMES || st_n >= 256) return 0;
        k = (u8)st_n++;
        st_nidx[n] = k;
        for (j = 0; j < NAME_LEN - 1 && s[j]; j++) t[j] = s[j];
        t[j] = 0;
        mem_write(NAMES_LIN + (u16)n * NAME_LEN, (const u8 *)t, NAME_LEN);
    }
    mem_put(ST_LIN + k, e);
    sty_flush();
    return k;
}
