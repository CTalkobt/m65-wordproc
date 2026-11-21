/* mkhelp.c — writes WRITER.HELP, the help document (shown at the start), as the
 * BASIC program's BSAVE would: a PRG (load address $0000, SCRATCH_LIN's in
 * bank 5) holding doc_pack's image
 *
 *   build/mkhelp build/help.prg
 */
#include <stdio.h>
#include "wp.h"

int main(int argc, char **argv) {
    FILE *f;
    u16 n;
    if (argc != 2) {
        fprintf(stderr, "usage: mkhelp out.prg\n");
        return 2;
    }
    sty_init();
    doc_load_help();
    n = doc_pack();
    f = fopen(argv[1], "wb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    fputc((int)(SCRATCH_LIN & 0xFF), f);
    fputc((int)((SCRATCH_LIN >> 8) & 0xFF), f);
    fwrite(MEM + SCRATCH_LIN, 1, n, f);
    fclose(f);
    printf("%s: %u characters\n", argv[1], doc_len - 1);
    return 0;
}
