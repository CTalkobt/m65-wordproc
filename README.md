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
`WRITER.FNT` (the fonts' glyphs, loaded by the program), `WRITER.HELP` (the
help: a Writer document shown when the program starts and again with F8 / HELP;
without it the program starts with an empty document), `MARKDOWN.OVL`
(Markdown import and export, a module: without it the rest works, and a
`.md` name gets "Markdown needs MARKDOWN.OVL on the disk"), `RTF.OVL` (RTF
import and export, the same for `.rtf`) and `LIST.OVL`
(list mode, a module: without it the menu does not offer it).

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
| CTRL + cursor keys | start a selection at the cursor, or extend the one there is |
| F10 / F11 / F12 | copy / cut / paste |
| F8 or HELP | the help (`WRITER.HELP`) as the document (after asking about unsaved changes) |
| ESC | menu, in a panel over the bottom three rows: New, Load, Save, Import, Export (plain text; Markdown for a name ending in `.md`, RTF for `.rtf`), Directory; Redraw, List on / off (with `LIST.OVL`), Help, Quit (ESC also ends a selection) |
| CTRL+L | redraw the whole screen |

Styles and fonts apply to the selection, or to what you type next.

**List mode** (ESC T; [#2](https://github.com/CTalkobt/m65-wordproc/issues/2)),
as vi's `:set list`, shows the text's invisible structure in either view,
without moving any of it: a dot for each space, ¶ at each paragraph's end
(raw view shows it always), an arrow after a line the paragraph wraps
from, trailing spaces (before a paragraph's end) as a block (raw view: red),
and `~` in raw view's rows past the end. The keys are the same.

## How it is split

| Part | Language | Does |
|------|----------|------|
| `src/wp.bas` | BASIC 65 | the main loop (GET and what each key means; idle time handed to C), the menu, file-name prompts and confirmations, all disk work (`BLOAD` / `BSAVE` documents and fonts, `DOPEN` + `FREAD#` / `FWRITE#` text and Markdown import and export, `DIR`), loading the help document, error trapping |
| `src/doc.c` | C | the document: two parallel arrays in bank 5 (text, an attribute byte per character), insert and delete by DMA, styles, alignment, clipboard, the file image |
| `src/layout.c` | C | line breaking for both views, justification, pagination, raw-view tags, cursor ↔ position mapping; incremental and lazy (below) |
| `src/render.c` | C | drawing: glyphs blitted (synthetic bold, italic shear, underline) into a band buffer and copied to the bitmap by DMA; only what changed is drawn again; status bars |
| `src/m65.c` | C | the VIC-IV screen mode, DMA and colour RAM |
| `src/md.c` | C | Markdown export and import (below): the module `MARKDOWN.OVL` |
| `src/rtf.c` | C | RTF export and (streamed) import: the module `RTF.OVL` |
| `src/list.c` | C | list mode's markers: the module `LIST.OVL` |
| `src/api.c` | C | the functions BASIC calls with `SYS {name}, a` (and reads with `RREG`) |
| `tools/helpdoc.c`, `tools/mkhelp.c` | C (host) | the help document, written as a Writer file by the C half itself |
| `tools/mkmod.py` | Python | links a module against the program (below) |
| `tests/` | C (host) | the C half on the PC: editing, files, layout (incremental = from scratch), drawing (cached = full redraw), fuzzed |

The Makefile compiles the C files with cc45 (`-O2` for the drawing, which
does the most work on every key; `-O1` for the rest, layout included,
which cc45 makes much smaller: md.c is 8.2 KB at `-O2`, 6.3 KB at `-O1`)
and `basic45 --link` links them right after the BASIC program in one `.prg`. BASIC passes strings (messages, the file
name) by `POKE`ing them into C buffers (`{msg}`, `{fname}`).

### Modules

Optional features are modules: `.OVL` files on the disk, so a user who
has no use for one leaves it off the disk and the program does not need
the room for it. Two kinds:

- **Run once** (as one job over the document), loaded just before it runs:
  Markdown (`MARKDOWN.OVL`, from `src/md.c`), RTF (`RTF.OVL`, from
  `src/rtf.c`); PETSCII graphics, PDF export, find and replace or a word
  count would fit the same way. One can also be called several times in a
  row (a command with `MOD_MORE`: `mod_run` then leaves it and the drawing's
  caches alone, as they share its memory): RTF import is streamed that way,
  1 KB of the file a call, as files from other word processors are often
  larger than the 30 KB scratch area.
- **Resident**, loaded at the start and called while drawing: list mode
  (`LIST.OVL`, from `src/list.c`). It lives in `mod_area`'s last
  `LIST_SIZE` bytes (`LIST_OFS`, past the drawing's buffers), so it too
  costs no bank 0 of its own; a run-once module overwrites it, so
  `mod_run` sets `list_ok` to 0 (the drawing then does not call it) and
  BASIC loads it again after. `render.c` calls it (`list_marks`) for each
  line it draws, after the line's text and before its cursor, with the
  line in `hk_l` / `hk_L`; it XORs its markers into the band. A feature
  that changes what keys do (undo) still belongs in the program.

- **Where it runs**: `mod_area` (`src/render.c`, `MOD_SIZE` = 7 KB in
  bank 0), which is also where the drawing keeps its buffers and caches
  (the band, the redraw cache, the bars' text, the row patterns: 3.4 KB),
  all remade after a full redraw. So a module costs bank 0 only what it
  needs beyond those, once for all modules.
- **Running it** (run once): BASIC `BLOAD`s it into `mod_area` (`bload
  "markdown.ovl", b0, p({mod_area})`, under `TRAP`: no file, no module)
  just before `SYS {wp_get}, ...` runs it, with nothing drawn between.
  `mod_run` checks its header (`"WM"`, the build's ID), calls its
  `mod_main` (what to do in `mod_cmd`, the result in `mod_ret`), then
  marks the drawing's caches unknown so the next frame redraws all;
  65535 tells BASIC there was none or it is from another build.
- **Building it** (`tools/mkmod.py`, from the Makefile): the module's C
  files are compiled as any other; basic45 links the program (with `-v`,
  and `BASIC45_KEEP_TMP` keeping its link's inputs in `build/tmp`), and
  mkmod.py runs that `ln45` again for the program's map, writes the
  program's symbols the module uses as equates (its functions and
  variables, the run time: the module calls into the program and has no
  copy of what the program has) and the header, links the module at
  `mod_area` (`--ofs`: at an offset in it, checked to be past the drawing's
  buffers, whose size render.c leaves in `mod_bufsize`), and checks that it
  fits (`--size`). The build's ID is a
  checksum of the sources and the tools (`build/buildid.h`), so a module
  is only run by the program it was linked with.
- **The base page**: cc45 keeps the most used variables of functions that
  make calls in the program's base page (blocks placed by ln45 up to $F1).
  The module's header says it uses all of the register pool, so its own
  blocks start at $80; where they can meet the program's:
  - the blocks of the program functions that lead to the call (`--from`:
    `mod_run`; for list mode `draw_line` and `render_raw`, where cc45
    inlined `list_marks`) and their callers: the header saves those bytes
    before it calls the module and puts them back after (Markdown: 9 bytes,
    list mode: 71), as those functions' variables are not used until it
    returns;
  - the blocks of the program functions the module calls, and everything
    they call, which are in use together with the module's: mkmod.py
    checks that none shares a byte with the module's (should one ever,
    compile that module with `-fno-zp-locals`, at some 20 % more code).

### Speed

- **Layout** is incremental: an edit is laid out from the line before it
  (from its paragraph's start when a paragraph mark is added or removed)
  until a new line matches an old one moved by the edit, and the rest of
  the line table is kept. When the edit changes how many lines follow (so
  every later line moves), only the lines down to the screen's end are laid
  out; the rest is laid out while no key is waiting (BASIC polls with `GET`
  and calls `wp_idle`), and the page count reads `?` until it is done.
- **Drawing** is cached: each band of screen rows (a line, blank page, desk,
  a page number) has a hash of what it shows; a band the same as in the
  last frame, in the same place, is not drawn again. The bars are redrawn
  only when their text changes. The hash is two 16-bit running sums
  (Fletcher-style: two adds a byte, no table): one byte changed, or two
  neighbours swapped, always changes it; a 16-bit hash (two 8-bit sums, or
  a CRC-16) let a line in some ten thousand changed ones show its old text
  in the host tests' random editing. Should one ever, CTRL+L redraws all.

About 10 ms a key in a short document and 80 ms in one of 10,000
characters (40 MHz, measured in xemu), and some 5-8 % more since the
layout is compiled with `-O1` and the raw font is fetched by DMA (to make
room for Markdown).

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
| `$4A000` | 12 KB | the fonts' glyphs and the document fonts' glyph offsets (`WRITER.FNT`, 11 KB) |
| `$4D000` | 12 KB | the line table (1024 lines) |
| `$50000` | 15000 | document text |
| `$53B00` | 15000 | document attributes |
| `$57600` | 30 KB | scratch: insertion bounce buffer, file image, text and Markdown files |
| `$5EC00` | 5 KB | clipboard |

### Files

A document (`S`ave / `L`oad) is a PRG file: `"WP"`, version 1, the length
(2 bytes), the text, then the attributes. The attribute byte: bits 0-1 font,
2 bold, 3 italic, 4 underline; on a paragraph's CR (or page break) bits 5-6
its alignment. Import / Export use sequential files: plain PETSCII text, or
Markdown when the name ends in `.md`.

### Markdown

Export (`src/md.c`, `md_export`, in the module `MARKDOWN.OVL`) writes
UTF-8-compatible ASCII with LF line ends; Import reads it back (up to
30 KB) and Markdown from elsewhere.

| Writer | Markdown |
|--------|----------|
| a paragraph | a line; paragraphs one after another with no empty one between them (and the same alignment) are one Markdown paragraph with hard breaks (`\` at the line's end); others are blocks with a blank line between |
| a paragraph all in Head | `# ` heading (import: any level, and setext `===` / `---` underlines) |
| a paragraph all in Mono, unstyled | a fenced code block; one line a paragraph |
| bold, italic, underline | `**…**`, `*…*`, `<u>…</u>` |
| Mono inside a paragraph | `` `…` `` |
| centre, right, justify | `<div align="center">` … `</div>` around the block |
| page break | `<div style="page-break-after: always"></div>` |
| Serif; Head inside a paragraph | (nothing in Markdown: plain text) |

Markup characters in the text are escaped with `\` (only where Markdown
would read them: `*`, `_`, `` ` ``, `[`, `]`, `\`, `~` everywhere; `#`, `>`, `-`,
`+`, `=` and a number's `.` at a paragraph's start; `<` and `&` before a
name), and spaces at a paragraph's start are `&nbsp;`; empty paragraphs are
not written (blocks are separated by blank lines instead).

Import also takes lists and quotes (their markers kept as text, an item a
paragraph), links (`[text](url)` becomes "text (url)", an image its text),
`<br>`, the common entities, and UTF-8: accented letters lose their accents
(ä → a, ß → ss), curly quotes, dashes and the ellipsis become ASCII, others
`?`. It puts an empty paragraph between blocks (not after a heading).
Emphasis is paired within a paragraph; an unpaired `*` or `_` is text.

Tests (`make test`): the help and 300 random documents (each `SEED`)
exported, imported and exported again give the same Markdown; Markdown
from elsewhere imported as expected. The exported random documents were
also checked with markdown-it (CommonMark): their text comes out exactly.

### RTF

Export (`src/rtf.c`, in the module `RTF.OVL`) is lossless for Writer's
documents: a font table (`\f0` Sans `\fswiss`, `\f1` Serif `\froman`,
`\f2` Mono `\fmodern`, `\f3` Head `\fswiss` at `\fs36`), `\b` `\i` `\ul`
where they change, each paragraph as `\pard`, its alignment (`\ql` `\qc`
`\qr` `\qj`) and `\par`, a page break as `\par\page`; `\ { }` escaped.

Import reads the file 1 KB at a time (BASIC streams it to the module, so
it may be larger than the scratch area): control words, control symbols,
groups (which keep the character formatting) and text. Fonts map by their
family in `\fonttbl` (`\froman` Serif, `\fmodern` Mono, others Sans), a
size of 14 pt or more is Head; `\b`, `\i`, the underlines, `\plain`,
`\pard`, alignment; `\par`, `\line` and `\row` end a paragraph, `\page`
ends a page; `\tab` and `\cell` are spaces; `\uN` (skipping `\ucN`
characters after it) and `\'hh` (Windows-1252) become ASCII where there is
one; `\binN` data is skipped. `\colortbl`, `\stylesheet`, `\info`,
pictures, headers, footers, footnotes and every `{\* ...}` group are left
out, as are unknown control words. Writer's own RTF round-trips exactly
(the host tests check it, and random documents); pandoc's imports with its
headings as Head and its code as Mono.

### Fonts

`tools/mkfonts.py` rasterises DejaVu Sans 12, DejaVu Serif 12, Liberation
Mono 13 and DejaVu Sans Bold 19 (1-bit, hinted, whole-pixel advances) and
the X11 8×13 font for the raw view; `make fonts` rebuilds `src/fonts.c`
(metrics and advance widths, kept in bank 0 for the layout; the raw font's
glyphs, drawn the most) and `src/writer.fnt` (the document fonts' glyphs,
then where each glyph starts).

## Limits

- 15000 characters (about 2500 words) and 1024 laid-out lines a document;
  a 2560-character clipboard; imported text up to 14 KB, Markdown 30 KB.
- No undo, no printing (yet).

## MEGA65 details worth knowing

- **Function keys**: the KERNAL turns F-keys into their `KEY` macros unless
  the Editor's lock bit (bit 5 of `$F7`) is set. `KEY OFF` sets it, but first
  waits for a pending macro (`$D1`) to drain, which never happens in a running
  program; the program sets the bit itself.
- **CTRL + cursor keys**: the keyboard's PETSCII table has no code for CTRL
  with cursor right or down, so the KERNAL never queues them; CTRL with left
  or up (the MEGA65's own keys, which the hardware sends as SHIFT + right /
  down) comes through as plain left / up. So BASIC polls with `GET`, the C
  half checks CTRL (`$D611`) when a cursor key arrives, and, between keys,
  reads CTRL + right / down from the keyboard matrix (`$D614` / `$D613`,
  with `$D60F` and the SHIFT bits to leave out left and up), repeating it by
  the frame counter (`$D7FA`).
- **RUN/STOP** is ignored while editing: the KERNAL's STOP vector (`$0328`)
  points at a 5-byte routine at `$0378` (free between the KERNAL's file
  tables and its RAM code) that answers "not pressed" as the KERNAL's own
  does. It has to be below `$2000`: BASIC 65 runs with its ROM mapped over
  `$2000`-`$BFFF`. Q)uit puts both back.
- **Bank 0**: the BASIC program, the C code and its data must end below
  `$C000` (the CPU stack is the page below it). With Markdown in the
  program it was full; as a module (above) the C data ended at `$B600`;
  the menu's panel, the hooks for list mode and RTF's import and export
  in BASIC took most of that, so it ends at `$BE00`, leaving 256 bytes
  (basic45's `--minify` would save several hundred, once it works:
  [M65Compiler #598](https://github.com/CTalkobt/M65Compiler/issues/598)).
  The glyph
  bitmaps (the raw-view font's too, fetched by DMA) live in bank 4
  (`WRITER.FNT`), the C files that do not run on every key are compiled
  with `-O1`, the help is a
  document on the disk rather than text in the program, and the C half
  reads a line of the line table (bank 4) whole with one DMA (`get_line`)
  rather than field by field: with cc45 each far `LINES[l].field` costs
  about 60 bytes of code
  ([M65Compiler #585](https://github.com/CTalkobt/M65Compiler/issues/585)).
- **cc45 and array members**: before M65Compiler master `a7191e09`, cc45
  kept only the low byte of an array member's address when an integer was
  added to it (`s.a + n`: the pointer landed in page 0;
  [M65Compiler #594](https://github.com/CTalkobt/M65Compiler/issues/594)).
  render.c's buffers in `mod_area` are such members: build with a cc45
  that has the fix.
