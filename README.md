# MEGA65 Writer

A graphical word processor for the MEGA65, written in **BASIC 65 and C**.
Text is shown on a 640 × 400 bitmap in proportional fonts, so fonts, **bold**,
*italic* and underline appear as they will print. Two views:

- **Page layout** — WYSIWYG: each character in its font and style, words
  wrapped, paragraphs aligned (left, centre, right, justified), pages of
  8.5 × 11 in. at 64 pixels an inch with margins, page breaks and page
  numbers, on a grey desk.
- **Raw** — the text in one plain 8 × 13 font, 80 columns, with the
  formatting codes revealed as coloured tags (`[B]` … `[/B]`, `[Serif]`,
  `[Center]`, `[Page]`) and paragraph marks (¶). You can edit in either view.

## Building and running

Needs the cc45 toolchain (M65Compiler: `basic45`, `cc45`, `disk45`), by
default checked out next to this directory (`CC45_ROOT`).

```bash
make            # build/writer.prg and build/writer.d81
make run        # run it in xemu
make test       # the C half on the host (gcc): editing, layout, files; build/*.png
```

On a MEGA65: copy `build/writer.d81` to the SD card, mount it, then
`DLOAD "WRITER"` and `RUN`. The disk holds `WRITER` (the program),
`WRITER.FNT` (the fonts' glyphs, loaded by the program) and `WELCOME` (the
document shown the first time it runs; without it you get an empty one).

`tools/xemu_run.py` runs it headless in xemu, types keys into the KERNAL's
keyboard buffer and saves a screenshot, e.g.
`python3 tools/xemu_run.py build/writer.prg build/writer.d81 out.png wait:8 keys:133 wait:1`
(F1: the raw view).

## Keys

| Key | Action |
|-----|--------|
| typing, RETURN | insert text, a new paragraph |
| DEL / INST | delete left / right |
| cursor keys | move; HOME line start/end; CLR document start/end |
| F13 / F14 | page up / down |
| F1 | page layout / raw view |
| F3 / F5 / F7 | bold / italic / underline |
| F2 | font: Sans, Serif, Mono, Head |
| F4 | paragraph alignment: left, centre, right, justify |
| F6 | page break |
| F9 | start / end a selection (move the cursor to extend it) |
| F10 / F11 / F12 | copy / cut / paste |
| F8 or HELP | help |
| ESC | menu: New, Load, Save, Import text, Export text, Directory, Help, Quit (ESC also ends a selection) |

Styles and fonts apply to the selection, or to what you type next.

## How it is split

| Part | Language | Does |
|------|----------|------|
| `src/wp.bas` | BASIC 65 | the main loop (GET / GETKEY and what each key means; idle time handed to C), the menu, file-name prompts and confirmations, all disk work (`BLOAD` / `BSAVE` documents and fonts, `DOPEN` + `FREAD#` / `FWRITE#` text import and export, `DIR`), the help screen, error trapping |
| `src/doc.c` | C | the document: two parallel arrays in bank 5 (text, an attribute byte per character), insert and delete by DMA, styles, alignment, clipboard, the file image |
| `src/layout.c` | C | line breaking for both views, justification, pagination, raw-view tags, cursor ↔ position mapping; incremental and lazy (below) |
| `src/render.c` | C | drawing: glyphs blitted (synthetic bold, italic shear, underline) into a band buffer and copied to the bitmap by DMA; only what changed is drawn again; status bars |
| `src/m65.c` | C | the VIC-IV screen mode, DMA and colour RAM |
| `src/api.c` | C | the functions BASIC calls with `SYS {name}, a` (and reads with `RREG`) |
| `tools/welcome.c`, `tools/mkwelcome.c` | C (host) | the welcome document, written as a Writer file by the C half itself |
| `tests/` | C (host) | the C half on the PC: editing, files, layout (incremental = from scratch), drawing (cached = full redraw), fuzzed |

`basic45 --link` compiles the C files with cc45 and links them right after
the BASIC program in one `.prg`. BASIC passes strings (messages, the file
name) by `POKE`ing them into C buffers (`{msg}`, `{fname}`).

### Speed

- **Layout** is incremental: an edit is laid out from the line before it
  (from its paragraph's start when a paragraph mark is added or removed)
  until a new line matches an old one moved by the edit, and the rest of
  the line table is kept. When the edit changes how many lines follow (so
  every later line moves), only the lines down to the screen's end are laid
  out; the rest is laid out while no key is waiting (BASIC polls with `GET`
  and calls `wp_idle`), and the page count reads `?` until it is done.
- **Drawing** is cached: each band of screen rows (a line, blank page, desk,
  a page number) has a CRC of what it shows; a band the same as in the last
  frame, in the same place, is not drawn again. The bars are redrawn only
  when their text changes.

About 10 ms a key in a short document and 80 ms in one of 10,000
characters (40 MHz, measured in xemu).

### The screen

A 640 × 400 one-bit bitmap made from the VIC-IV's text mode: H640 + V400 give
80 × 50 cells, 16-bit character numbers let every cell show its own
character (cell *n* shows character *n*), and the character set pointer is
the bitmap in bank 4 — so the 4000 8-byte glyphs are a bitmap laid out as
the C64's. Each cell has its own colour (text, desk, tags, bars).

### Memory

| Address | Size | Use |
|---------|------|-----|
| `$40000` | 32000 | the bitmap |
| `$48000` | 8000 | the screen (16-bit cells) |
| `$4A000` | 12 KB | the fonts' glyphs (`WRITER.FNT`) |
| `$4D000` | 12 KB | the line table (1024 lines) |
| `$50000` | 15000 | document text |
| `$53B00` | 15000 | document attributes |
| `$57600` | 30 KB | scratch: insertion bounce buffer, file image |
| `$5EC00` | 5 KB | clipboard |

### Files

A document (`S`ave / `L`oad) is a PRG file: `"WP"`, version 1, the length
(2 bytes), the text, then the attributes. The attribute byte: bits 0-1 font,
2 bold, 3 italic, 4 underline; on a paragraph's CR (or page break) bits 5-6
its alignment. Import / Export use plain sequential PETSCII files.

### Fonts

`tools/mkfonts.py` rasterises DejaVu Sans 12, DejaVu Serif 12, Liberation
Mono 13 and DejaVu Sans Bold 19 (1-bit, hinted, whole-pixel advances) and
the X11 8×13 font for the raw view; `make fonts` rebuilds `src/fonts.c`
(metrics, kept in bank 0 for the layout) and `src/writer.fnt` (glyphs).

## Limits

- 15000 characters (about 2500 words) and 1024 laid-out lines a document;
  a 2560-character clipboard; imported text up to 14 KB.
- No undo, no printing (yet).

## MEGA65 details worth knowing

- **Function keys**: the KERNAL turns F-keys into their `KEY` macros unless
  the Editor's lock bit (bit 5 of `$F7`) is set. `KEY OFF` sets it, but first
  waits for a pending macro (`$D1`) to drain, which never happens in a running
  program; the program sets the bit itself.
- **RUN/STOP** is ignored while editing: the KERNAL's STOP vector (`$0328`)
  points at a 5-byte routine at `$0378` (free between the KERNAL's file
  tables and its RAM code) that answers "not pressed" as the KERNAL's own
  does. It has to be below `$2000`: BASIC 65 runs with its ROM mapped over
  `$2000`-`$BFFF`. Q)uit puts both back.
- **Bank 0**: the BASIC program, the C code and its data must end below
  `$C000`, so the glyph bitmaps live in bank 4 (`WRITER.FNT`).
