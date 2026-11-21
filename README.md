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

It needs a MEGA65 with attic RAM (the 8 MB HyperRAM at `$8000000`), where
the document is kept; without it, it stops at the start with "No attic
RAM." xemu emulates it.

## Building and running

Needs the cc45 toolchain (M65Compiler: `basic45`, `cc45`, `disk45`), by
default checked out next to this directory (`CC45_ROOT`), at master
`2b53dc79` or later (`basic45 --map`, a working
`--minify`, struct fields read at their addresses (M65Compiler #657-#660),
without which bank 0 overflows, and `basic45 --stack-page`; build
`BASFLAGS=` for an unminified program).

```bash
make            # build/writer.prg and build/writer.d81
make run        # run it in xemu
make test       # the C half on the host (gcc): editing, layout, files; build/*.png
```

On a MEGA65: copy `build/writer.d81` to the SD card, mount it, then
`DLOAD "WRITER"` and `RUN`. The disk holds `WRITER` (the program),
`WRITER.FNT` (the fonts' glyphs, loaded by the program), `WRITER.MSG` (the
messages, menu and prompts, from `src/messages.txt`: loaded by the program
at each start to `$1800`), `WRITER.HELP` (the
help: a Writer document shown when the program starts and again with F8 / HELP;
without it the program starts with an empty document), `MARKDOWN.OVL`
(Markdown import and export, a module: without it the rest works, and a
`.md` name gets "Markdown needs MARKDOWN.OVL on the disk"),
`RTF.OVL` (RTF import and export, the same for `.rtf`), `HTML.OVL` and
`HTMLIN.OVL` (HTML export and import, the same for `.htm` and `.html`), `LIST.OVL`
(list mode, a module: without it the menu does not offer it) and
`DEFAULT0.OVL` (undo, find and replace, and the styles; a required module,
named so that it is not taken for an optional one: without it those do
nothing, or say "No DEFAULT0.OVL.").

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
| F3 / F5 / F7, or CTRL+B / CTRL+I / CTRL+U | bold / italic / underline |
| F2 | font: Sans, Serif, Mono, Head |
| F4 | paragraph alignment: left, centre, right, justify |
| F6 | page break |
| F9 | start / end a selection (move the cursor to extend it) |
| CTRL + cursor keys | start a selection at the cursor, or extend the one there is |
| F10 / F11 / F12 | copy / cut / paste |
| CTRL+Z | undo the last edit; again: redo it (below) |
| CTRL+F | find (below) |
| SHIFT+CTRL+F / SHIFT+CTRL+B | find the next / the one before |
| CTRL+H | find and replace (below) |
| CTRL+W | save, under the document's name (asking only when it has none) |
| F8 or HELP | the help (`WRITER.HELP`) as the document (after asking about unsaved changes) |
| ESC | menu, in a panel over the bottom three rows: New, Load, Save, Import, Export (plain text; Markdown for a name ending in `.md`, RTF for `.rtf`, HTML for `.htm` or `.html`), Directory; Redraw, List on / off (with `LIST.OVL`), Help, Quit (ESC also ends a selection) |
| CTRL+D | the style drop-down (below) |
| CTRL+L | redraw the whole screen |
| CTRL+A | Compose: the next two characters make an accented one (below) |

Styles and fonts apply to the selection, or to what you type next.

**Paragraph styles** ([#4](https://github.com/CTalkobt/m65-wordproc/issues/4)):
the top bar's left end shows the style of the cursor's paragraph: Normal,
Heading 1, Heading 2, Heading 3, Quote, Code, Bullet or Numbered. CTRL+D
drops the list down under it, each style in its own look; the cursor keys
move through it and RETURN applies the one lit (or N 1 2 3 Q C B # at once;
ESC leaves it).

- A paragraph the selection holds whole (all its text), or with no
  selection the cursor's, becomes that kind. Its characters in the old
  style's font and style take the new one's (Heading 1: Head, bold;
  Heading 2: Head; Heading 3: Serif, bold; Quote: Serif, italic; Code:
  Mono; Normal: Sans), while other fonts are kept, and so is bold, italic
  or underline added on top of the old style.
- The rest of the selection, part of a paragraph, takes the style's font
  and look as a character style (Normal: plain Sans), and its paragraph
  keeps its kind. A selection over part of one paragraph, then whole ones,
  then part of another does each its way.
- A quote is laid out indented with a bar beside it, and a code block
  indented with a dotted line. RETURN at a heading's end starts a Normal
  paragraph; the raw view shows a kind as a green tag (`[H1]`, `[Quote]`,
  `[Code]`, `[List]`, `[Num2]`: a list item's level from the second on)
  at the paragraph's start.
- **Lists**: a bulleted or numbered item is a paragraph, indented by its
  level (four levels). Its bullet (a square, hollow at the second and
  fourth levels) or number is drawn in the indent, not stored as text: a
  number counts the numbered items before it at its level, passing over
  deeper ones and stopping at anything else, so the list renumbers itself
  as you edit it. At an item's start TAB takes it a level in and DEL a
  level out (from the first level: a Normal paragraph); RETURN makes the
  next item, and RETURN on an empty item takes it out a level, so twice
  ends the list. A list changed to the other kind keeps its levels.

**Undo** (CTRL+Z) takes back the last edit: what was typed (with its DELs)
since the cursor last moved, or one command (a style, a cut, a paste, a
replace-all). CTRL+Z again does it again, so it also redoes. It keeps the
text the edit replaced, up to 7500 characters: a longer cut, say, can't
be undone ("Nothing to undo."). A new document, a load or an import
leaves nothing to undo, and so does a save or export whose file is larger
than about 44 KB (it needs the scratch area where undo keeps its copy).

**Find** (CTRL+F) asks for the text (up to 40 characters; letters match
either case, the accented ones only as they are), and selects the next
place it is, from the cursor, going round from the document's end to its
start. SHIFT+CTRL+F finds the next, SHIFT+CTRL+B the one before.
**Replace** (CTRL+H) asks for the text and what to put instead, then for
each place found asks "Replace? Y)es N)o A)ll": Y replaces it (in the
style of its first character) and finds the next, N finds the next, A
replaces it and every other in the document (then says how many), another
key stops. An empty replacement deletes what is found.

**List mode** (ESC T; [#2](https://github.com/CTalkobt/m65-wordproc/issues/2)),
as vi's `:set list`, shows the text's invisible structure in either view,
without moving any of it: a dot for each space, ¶ at each paragraph's end
(raw view shows it always), an arrow after a line the paragraph wraps
from, trailing spaces (before a paragraph's end) as a block (raw view: red),
and `~` in raw view's rows past the end. The keys are the same.

**Accented characters** ([#1](https://github.com/CTalkobt/m65-wordproc/issues/1)):
CTRL+A, then two characters, in either order, types a Latin-1 character:

| Type | For | Type | For |
|------|-----|------|-----|
| `"a` `'e` `` `e `` `^o` `~n` | ä é è ô ñ (any letter that has it, capitals too) | `,c` `oa` | ç å |
| `ss` `ae` `AE` `o/` | ß æ Æ ø | `<<` `>>` `??` `!!` | « » ¿ ¡ |
| `co` `ro` `L-` `Y=` `c/` | © ® £ ¥ ¢ | `oo` `+-` `xx` `:-` `mu` | ° ± × ÷ µ |
| `12` `14` `34` `1^` `2^` `3^` | ½ ¼ ¾ ¹ ² ³ | `so` `PP` `..` | § ¶ · |
| `a_` `o_` `th` `d-` | ª º þ ð | two spaces | a no-break space |

`` ` `` is SHIFT+@ (the keyboard has no key for it). The status bar says
"Compose" while it waits; a pair the table does not have types nothing.
The table is `tools/mkfonts.py`'s (`COMPOSE_MORE`, and every Latin-1
letter with its accent), in `WRITER.FNT`.

## How it is split

| Part | Language | Does |
|------|----------|------|
| `src/wp.bas` | BASIC 65 | the main loop (GET and what each key means; idle time handed to C), the menu, file-name prompts and confirmations, all disk work (`BLOAD` / `BSAVE` documents and fonts, `DOPEN` + `FREAD#` / `FWRITE#` text and Markdown import and export, `DIR`), loading the help document, error trapping |
| `src/doc.c` | C | the document: two parallel arrays in attic RAM (text, an attribute byte per character) with a gap at the last edit, insert and delete, the clipboard, the file image |
| `src/style.s45` | asm | the style table (`sty`, `sty_find`, `sty_init`; a loaded file's table merged, its indexes remapped): `conv.c` has them in C for the host |
| `src/gap.s45`, `src/gapmove.s45` | asm | reading and writing a character round the gap (`txt_at` ...), moving the gap (`gap_to`, `move2`): `doc.c` and `conv.c` have them in C for the host |
| `src/layout.c` | C | line breaking for both views, justification, pagination, raw-view tags, cursor ↔ position mapping; incremental and lazy (below) |
| `src/render.c` | C | drawing: glyphs blitted (synthetic bold, italic shear, underline) into a band buffer and copied to the bitmap by DMA; only what changed is drawn again; status bars |
| `src/m65.c` | C | the VIC-IV screen mode, DMA and colour RAM |
| `src/conv.c` | C | what the converters share (below): output to the scratch area, a walk over the document for export, a builder for import, Unicode as the document's characters |
| `src/md.c` | C | Markdown export and import (below): the module `MARKDOWN.OVL` |
| `src/rtf.c` | C | RTF export and (streamed) import: the module `RTF.OVL` |
| `src/html.c` | C | HTML export (below): the module `HTML.OVL` |
| `src/htmlin.c` | C | HTML (streamed) import: the module `HTMLIN.OVL` |
| `src/list.c` | C | list mode's markers: the module `LIST.OVL` |
| `src/edit.c`, `src/edit_undo.c` | C | undo, find and replace, the styles: the module `DEFAULT0.OVL` |
| `src/modcall.s45` | asm | the jump into `DEFAULT0.OVL` (and, for ln45's call graph, what it calls) |
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
  Markdown (`MARKDOWN.OVL`, from `src/md.c`: the largest module, 6710
  bytes, so it sets `MOD_SIZE`), RTF (`RTF.OVL`, from
  `src/rtf.c`), HTML (`HTML.OVL`, from `src/html.c`, and `HTMLIN.OVL`, from
  `src/htmlin.c`: together more than `MOD_SIZE`); PETSCII graphics, PDF export, find and replace or a word
  count would fit the same way. One can also be called several times in a
  row (a command with `MOD_MORE`: `mod_run` then leaves it and the drawing's
  caches alone, as they share its memory): RTF import is streamed that way,
  1 KB of the file a call, as files from other word processors are often
  larger than the 59 KB scratch area.
- **Resident**, loaded at the start and called while drawing: list mode
  (`LIST.OVL`, from `src/list.c`). It lives in `mod_area`'s last
  `LIST_SIZE` bytes (`LIST_OFS`, past the drawing's buffers), so it too
  costs no bank 0 of its own; a run-once module overwrites it, so
  `mod_run` sets `list_ok` to 0 (the drawing then does not call it) and
  BASIC loads it again after. `render.c` calls it (`list_marks`) for each
  line it draws, after the line's text and before its cursor, with the
  line in `hk_l` / `hk_L`; it XORs its markers into the band. A feature
  that changes what keys do (undo) still belongs in the program.
- **Kept, copied in for each call**: undo, find and replace, and the
  styles (`DEFAULT0.OVL`, from `src/edit.c` and `src/edit_undo.c`; 4.2 KB).
  BASIC `BLOAD`s it into `mod_area` at the start (with `LIST.OVL`), and
  the C half keeps a copy in colour RAM (`edit_keep`). Each call
  (`edit_run`: every edit notes for undo what it will change, so every
  key that edits makes one) copies it into `mod_area`, up to `LIST.OVL`,
  by DMA and runs it: about 6 KB of DMA, well under a millisecond. Its
  first 2805 bytes there are buffers the drawing uses only while it draws
  (the band, the layout's new lines, the menu's text, ...: `MOD_SCRATCH`);
  the rest, the drawing's caches, go to colour RAM meanwhile and come
  back after, so nothing is drawn again for it. It keeps nothing of its
  own (it is copied anew each time): its state is `doc.c`'s. The editing
  it does (deleting and inserting for a replace) uses the program's
  functions that note nothing (`del_raw`, `room`): it notes its own
  changes, and must not call back into itself.

- **Where it runs**: `mod_area` (`src/render.c`, `MOD_SIZE` = 6784 bytes in
  bank 0), which is also where the drawing keeps its buffers and caches
  (the band, the redraw cache, the bars' text, the row patterns, and the
  layout's buffer of new lines and the menu's text: 4.3 KB),
  all remade after a full redraw. So a module costs bank 0 only what it
  needs beyond those, once for all modules.
- **Running it** (run once): BASIC `BLOAD`s it into `mod_area` (`bload
  "mdimport.ovl", b0, p({mod_area})`, under `TRAP`: no file, no module)
  just before `SYS {wp_get}, ...` runs it, with nothing drawn between.
  `mod_run` checks its header (`"WM"`, the build's ID), calls its
  `mod_main` (what to do in `mod_cmd`, the result in `mod_ret`), then
  marks the drawing's caches unknown so the next frame redraws all;
  65535 tells BASIC there was none or it is from another build.
- **Building it** (`tools/mkmod.py`, from the Makefile): the module's C
  files are compiled as any other; basic45 links the program and writes
  its link map (`--map`: symbols, call graph, base-page blocks), and
  mkmod.py reads that map, writes the
  program's symbols the module uses as equates (its functions and
  variables, the run time: the module calls into the program and has no
  copy of what the program has) and the header, links the module at
  `mod_area` (`--ofs`: at an offset in it, checked to be past the drawing's
  buffers, whose size render.c leaves in `mod_bufsize`), and checks that it
  fits (`--size`). The build's ID is a
  checksum of the sources and the tools (`build/buildid.h`), so a module
  is only run by the program it was linked with.
- **What the converters share** (`src/conv.c`, in the program, once; issue
  #3): a format module holds only its syntax and calls the program for
  the rest.
  - *Output*: `o_start`, `o_put`, `o_puts`, `o_end` write to the scratch
    area (and count past its end: `o_end` is then 0, too long).
  - *Export*: `walk_start` / `walk_next` hand over the document an event
    at a time (`EV_PARA` with its alignment, `EV_RUN` characters of one
    style, `EV_PEND` a CR or a page break, `EV_END`). It is pulled rather
    than calling the module back: a call through a pointer reaches every
    function, so ln45 would have to keep every base-page block apart.
  - *Import*: `build_start`, `build_put` (with `b_attr`), `build_uni` (a
    Unicode character as the document's: ASCII, accents left out),
    `build_end` (the final CR, the length). The program lays the document
    out after the module returns (`doc_built`, from `mod_run`): called
    from the module, the layout's base-page blocks would all have to miss
    the module's.
  - `txt_at` / `atr_at`: the document's character and attribute (a far
    `TXT[i]` is some 30 bytes of code with cc45, a call 10). `doc.c` and
    `layout.c` use them too.
  - `conv.c` is compiled with `-fno-zp-locals`: its functions keep no
    base-page variables, so nothing a module calls there meets its blocks.

  The program grew by 530 bytes, the modules shrank by 700 (Markdown 6978
  → 6236 bytes, RTF 4272 → 3576), so `MOD_SIZE` went from 7 KB to 6.25 KB.
  The PETSCII text import and export (`doc.c`) use the same calls.
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

- **The gap**: the document's store (text and attributes, in attic RAM)
  keeps `GAP_SPAN` (2048, in `wp.h`) unused bytes at the last edit. Typing
  fills them, a deletion widens them, and an edit elsewhere moves the gap
  there, moving only the characters between the two places. The rest of
  the document moves only when the gap is used up (every 2 KB typed, or a
  larger paste), where before every key moved all of it (text and
  attributes, each twice through the scratch area). Reading a character
  past the gap adds its length (`gap.s45`). Undo and the clipboard's and
  the file's copies see the document through it; undo closes it first.
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

In xemu (40 MHz), typing takes about 65 ms a key in a document of 1000
characters, 80 ms in one of 13,000 and 93 ms in one of 28,000 (2000
lines): drawing the changed line is most of it, and what grows with the
document is the layout's work on the lines after the edit. xemu does not
slow attic RAM down as the HyperRAM does, so it shows nothing of what the
gap saves; on a real MEGA65 each key used to copy the document's tail four
times through the HyperRAM, and the reads of the text (a 32-bit
`lda [bp],z` each) take longer there too. Not yet measured on hardware.

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
| `$4A000` | 12 KB | `WRITER.FNT`'s first part: Head's glyphs and records, the raw font, the compose table |
| `$50000` | 59 KB | scratch (to the clipboard): the gap's bounce buffer, file image, text and Markdown files; from `$57530` (`DOC_MAX`) what undo puts back while it swaps; its last 15000 bytes (`$5B168`) undo's copy of what the last edit replaced |
| `$5EC00` | 5 KB | clipboard |
| `$8000000` | 64 KB | attic RAM: document text (up to 30000 characters) and the gap |
| `$8010000` | 64 KB | attic RAM: document attributes, the same |
| `$8020000` | 48 KB | attic RAM: the line table (4096 lines of 12 bytes) |
| `$8030000` | 48 KB | attic RAM: the line table's bounce buffer (lines moved up) |
| `$FF83000` | 20 KB | colour RAM past the screen's cells (`$FF81000`-`$FF82F3F`): `WRITER.FNT`'s second part, Sans, Serif and Mono (12 KB); then (`$FF86020`) the find and replace strings, the drawing's caches while `DEFAULT0.OVL` runs, `DEFAULT0.OVL` (`$FF86680`), and the style table (`$FF87800`, up to 1 KB) |

### Files

A document (`S`ave / `L`oad) is a PRG file: `"WP"`, version 3, the length
(2 bytes), the text (Latin-1: ASCII, and `$A0`-`$FF` for the accented
characters; CR ends a paragraph, FF a page), the attributes, then the
style table: its entry size (1), its count (2 bytes), its entries. A
character's attribute byte is an index into the style table (issue #5),
whose entry is its style: bits 0-1 font, 2 bold, 3 italic, 4 underline.
Entries 0-31 are those 32 styles at their own bits; others are added as
styles are used (none yet: room for strikethrough, superscript and
subscript, more fonts, named styles). The table belongs to the session,
so the clipboard and undo keep their meaning across documents: a file's
entries are merged into it when it loads, and its characters' indexes
changed where they differ. Version 2 files (before the table: a
character's attribute was its style) load as they are.
A paragraph's CR (or page break) holds the paragraph's: bits 0-2 its kind
(0 Normal, 1-3 Heading 1-3, 4 Quote, 5 Code, 6 Bullet, 7 Numbered), bits
3-4 a list item's level (0-3), bits 5-6 its alignment. Version 1 files (before the kinds: their
CRs had the typing style in bits 0-4) load as Normal paragraphs. Import / Export use sequential files: plain PETSCII text
(accents left out on export: PETSCII has none), Markdown when the name ends
in `.md`, RTF for `.rtf`, HTML for `.htm` or `.html`.

### Markdown

Export (`src/md.c`, `md_export`, in the module `MARKDOWN.OVL`) writes
UTF-8 with LF line ends; Import reads it back (up to 59 KB) and Markdown
from elsewhere.

| Writer | Markdown |
|--------|----------|
| a paragraph | a line; paragraphs one after another with no empty one between them (and the same alignment) are one Markdown paragraph with hard breaks (`\` at the line's end); others are blocks with a blank line between |
| Heading 1, 2, 3 | `# `, `## `, `### ` (import: `####` and deeper as Heading 3; setext `===` Heading 1, `---` Heading 2) |
| Quote | `> ` (quote paragraphs one after another: one quote with hard breaks; import: a `>` line alone ends a paragraph in it, a line without `>` goes on with it) |
| Code | a fenced code block, a line a paragraph (empty ones too) |
| list items | `- ` or `N. ` (its number), four spaces before it a level; items one after another are one list (import: `-`, `*`, `+`, `N.` or `N)`; an item deeper than the one before is a level in, else at the level whose indent it has; a blank line between items keeps the list going, as a loose list; the marker is not kept as text) |
| bold, italic, underline | `**…**`, `*…*`, `<u>…</u>`, where they differ from the paragraph style's own (a heading's bold is not written) |
| Mono inside a paragraph | `` `…` `` |
| centre, right, justify | `<div align="center">` … `</div>` around the block |
| page break | `<div style="page-break-after: always"></div>` |
| Serif; Head inside a paragraph | (nothing in Markdown: plain text) |

Markup characters in the text are escaped with `\` (only where Markdown
would read them: `*`, `_`, `` ` ``, `[`, `]`, `\`, `~` everywhere; `#`, `>`, `-`,
`+`, `=` and a number's `.` at a paragraph's start; `<` and `&` before a
name), and spaces at a paragraph's start are `&nbsp;`; empty paragraphs are
not written (blocks are separated by blank lines instead).

Import also takes links (`[text](url)` becomes "text (url)", an image its text),
`<br>`, the common entities, and UTF-8: Latin-1's characters (U+00A0-U+00FF)
as they are; curly quotes, dashes, the ellipsis and € become ASCII, others
`?`. It puts an empty paragraph between blocks (not after a heading).
Emphasis is paired within a paragraph; an unpaired `*` or `_` is text.

Tests (`make test`): the help and 300 random documents (each `SEED`)
exported, imported and exported again give the same Markdown; Markdown
from elsewhere imported as expected. The exported random documents were
also checked with markdown-it (CommonMark): their text comes out exactly.

### HTML

Export (`src/html.c`, `html_export`, in the module `HTML.OVL`, 2.6 KB)
writes one UTF-8 page: the document's name as its title, a style sheet
(spaces kept with `white-space: pre-wrap`; quotes and Heading 3 in a serif
font, as Writer draws them), then the paragraphs grouped as the Markdown
export groups them: `<p>` (paragraphs with no empty one between them, of
the same alignment: one, with `<br>`), `<h1>`-`<h3>`, `<blockquote>`,
`<pre><code>` (a line a paragraph), and list items as `<li>` in `<ul>` /
`<ol>` (`start="N"` past 1), each level's list inside the item before it.
Bold, italic and underline (where the kind's own style lacks them) are
`<b>` `<i>` `<u>`; another font in part is `<code>` (Mono) or a `<span>`
(Serif, Sans, Head); alignment is `text-align`, a page break a `<div>`
with `page-break-after: always`.

Import (`src/htmlin.c`, in its own module `HTMLIN.OVL`, 4.6 KB) is
streamed as RTF's, 1 KB of the file a call, so a page larger than the
scratch area can be read. It takes back what the export writes, and from
elsewhere:

| HTML | Writer |
|------|--------|
| `<p>`, `<div>` and the other block elements (`section`, `table`, ...) | paragraphs; an empty one between blocks (not after a heading or a page break, nor between a list's items), as the Markdown import |
| `<br>` | a paragraph's end |
| `<h1>`-`<h3>` (`<h4>`-`<h6>`: Heading 3), `<blockquote>`, `<pre>` | the kinds; each line of a `<pre>` a Code paragraph |
| `<ul>`, `<ol>`, `<li>` | Bullet, Numbered, nested: the level (up to 4) |
| `<b>` `<strong>`, `<i>` `<em>` `<cite>`, `<u>` `<ins>`, `<code>` `<tt>` `<kbd>` `<samp>` | bold, italic, underline, Mono |
| `<span class="s">` / `"r"` / `"h"` (the export's) | Sans, Serif, Head |
| `style="text-align: ..."`, `align="..."`, `<center>` | alignment |
| `style="page-break-after: always"` (or `break-before` / `break-after`) | a page break |
| table rows, cells | a paragraph a row, two spaces between cells |
| `<title>`, `<script>`, `<style>`, `<svg>`, `<template>`; comments, `<!...>` | left out; other tags are dropped, their text kept |

White space with a line end in it is one space (spaces alone are kept, as
the export writes them), and there is none at a paragraph's start or end;
in `<pre>` it is kept. Characters: UTF-8 (as the Markdown import: Latin-1
as it is, some punctuation as ASCII, others `?`; a byte that is not UTF-8
is taken as Latin-1), `&#N;`, `&#xH;`, and named entities: the common ones
and Latin-1's accented letters (`&eacute;`, `&Uuml;`, `&szlig;`, ...).

Tests (`make test`): the help exported, imported and exported again gives
the same HTML; HTML from elsewhere imported as expected. In xemu a
2.8 KB page from elsewhere and the help's HTML, imported and exported
again on the MEGA65, are byte for byte what the host makes.

### RTF

Export (`src/rtf.c`, in the module `RTF.OVL`) is lossless for Writer's
documents: a font table (`\f0` Sans `\fswiss`, `\f1` Serif `\froman`,
`\f2` Mono `\fmodern`, `\f3` Head `\fswiss` at `\fs36`), `\b` `\i` `\ul`
where they change, a stylesheet (`\s0` Normal, `\s1`-`\s3` heading 1-3
with `\outlinelevel`, `\s4` Quote and `\s5` Code with an indent, `\li`,
`\s6` List Bullet and `\s7` List Number), each paragraph as `\pard`, its
style with those properties (readers take them from the paragraph; a list
item also its level, `\ilvl`, an indent by it and a hanging first line,
and its bullet or number as `{\listtext ...\tab}`, the text a reader
without lists shows), its alignment (`\ql` `\qc` `\qr` `\qj`) and
`\par`, a page break as `\par\page`; `\ { }` escaped,
accented characters as `\'hh` (`\ansicpg1252`, which has Latin-1's).

Import reads the file 1 KB at a time (BASIC streams it to the module, so
it may be larger than the scratch area): control words, control symbols,
groups (which keep the character formatting) and text. Fonts map by their
family in `\fonttbl` (`\froman` Serif, `\fmodern` Mono, others Sans), a
size of 14 pt or more is Head; `\b`, `\i`, the underlines, `\plain`,
`\pard`, alignment; a paragraph's kind from `\outlinelevelN` (heading N+1,
from 3 on heading 3) or from its `\sN`, by the style's name in the
`\stylesheet` ("heading N" as its level, "Title" Heading 1, a name with
"quot" Quote, with "code", "preformatted" or "source" Code, with
"bullet" or "number" a list); a list item from `{\listtext ...}` (as Word
writes it, before the paragraph's `\pard`: a digit in it makes a numbered
item, else bulleted; its text left out) and its level from `\ilvlN`; `\par`, `\line` and `\row` end a paragraph, `\page`
ends a page; `\tab` and `\cell` are spaces; `\uN` (skipping `\ucN`
characters after it) and `\'hh` (Windows-1252) are Latin-1's characters, or ASCII
for the punctuation it does not have; `\binN` data is skipped. `\colortbl`, `\info`,
pictures, headers, footers, footnotes and every `{\* ...}` group are left
out, as are unknown control words. Writer's own RTF round-trips exactly
(the host tests check it, and random documents); pandoc's imports with its
headings as headings (`\outlinelevel`) and its code as Mono (it marks a
quote only with an indent, so that comes in as Normal).

### Fonts

`tools/mkfonts.py` rasterises DejaVu Sans 12, DejaVu Serif 12, Liberation
Mono 13 and DejaVu Sans Bold 19 (1-bit, hinted, whole-pixel advances) and
the X11 8×13 font for the raw view, codes 32-126 and Latin-1's
`$A0`-`$FF`; `make fonts` rebuilds `src/fonts.c` (metrics, where each
font's block is) and `src/writer.fnt`. Each font is one block: a 3-byte
record for each code 32-255 (its advance, where its glyph starts; codes
129-159 are `?`), then the glyphs; the raw font is 13 bytes a code. The
blocks, with the compose table, do not fit bank 4's 12 KB together, so
mkfonts.py places each, the largest first, in bank 4 or in colour RAM's
last 20 KB (`HI_LIN`, `$FF83000`: every MEGA65 has 32 KB of colour RAM,
and the screen's cells use only `$FF81000`-`$FF82F3F`). BASIC `BLOAD`s the
file to the scratch area at the start and `font_install` copies the two
parts by DMA. Layout reads each character's width from its record (far
memory: one `LDA [zp],Z`), so the fonts keep no table in bank 0. Mono's accented capitals lose
their top pixel row, and Head's Æ is cut at 16 pixels, the widest a glyph
can be.

## Limits

- 30000 characters (about 5000 words) and 4096 laid-out lines a document;
  a 2560-character clipboard; imported text up to 14 KB, Markdown 59 KB.
- One level of undo (the last edit, up to 7500 characters of what it
  replaced); no printing (yet).

## MEGA65 details worth knowing

- **Function keys**: the KERNAL turns F-keys into their `KEY` macros unless
  the Editor's lock bit (bit 5 of `$F7`) is set. `KEY OFF` sets it, but first
  waits for a pending macro (`$D1`) to drain, which never happens in a running
  program; the program sets the bit itself.
- **CTRL + letters** come from `GET` as codes 1-26, some of them the
  F-keys' too: CTRL+U is F10's `$15`, CTRL+W F12's `$17`, CTRL+Z F14's
  `$1A`, CTRL+I TAB's 9, CTRL+H 8. So the C half (`wp_key`, its `keys`
  table) takes those as CTRL's only while CTRL is held (`$D611`), and
  SHIFT+CTRL+F / B come through as CTRL+F / B with SHIFT held.
- **`SYS addr, INSTR(...)`** (a ROM bug): SYS puts its address in the
  KERNAL's far-call registers `_bank`, `_pchi`, `_pclo` (`$02`-`$04`)
  first, and then evaluates the register arguments. INSTR keeps its
  `positn`, `match` and `regexp` in those same bytes (`b65.src`), so SYS
  then jumps to wherever INSTR left them (the screen garbled, the keys
  ignored). Work the INSTR out into a variable first. Other functions that
  use `$02`-`$04` would do the same as a SYS argument.
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
  in BASIC took most of that, so it ended at `$BE00`. The BASIC program
  is now built with basic45's `--minify` (no comments or indentation,
  lines joined, short names: 5378 bytes rather than 6115), so the C half
  starts at `$3600`. The converters' shared code (`conv.c`, above) and a
  smaller `mod_area` then left the C data ending at `$BB00`: 1024 bytes
  free; the accented characters (#1: the Compose key, the glyph lookup)
  took half of that, so it ends at `$BD00`. Paragraph styles (#4) took
  2.3 KB at first; to make room, BASIC's two `select`s of keys (the main
  loop's and the menu's) became tables (`instr` over a string of key
  codes, then `ON ... GOSUB`), as basic45 spends some 60 bytes on each
  `case`: the BASIC program went from 5614 to 4756 bytes, so the C half
  starts at `$3300`; the fonts' width tables went to far memory (above);
  and the layout's line buffer, the menu's text and a few work buffers
  moved into `mod_area`'s drawing memory (`LIST_OFS` 4288). Lists (#4)
  took about 1 KB more. BASIC's procs no longer cost a `GOTO` each (basic45
  put one over every proc until [M65Compiler
  #605](https://github.com/CTalkobt/M65Compiler/issues/605)): 4523 bytes,
  so the C half starts at `$3200`. And Markdown became two modules, import
  (4944 bytes) and export (1871), so the largest module (it was Markdown's
  6713 bytes with lists) no longer sets `MOD_SIZE`: the drawing's buffers
  and `LIST.OVL` do (4288 + 1728), and it went from 6400 to 6016 bytes. The
  C data then ended at `$BD00`, leaving 768 bytes for the CPU stack. Undo,
  find and replace (#4) went into a module (`DEFAULT0.OVL`, above) with the
  styles' code (1.2 KB of the program): what is left in the program (the
  key table, `edit_run`, the notes in each edit) and the build without
  tail calls (below) took 330 bytes, so the C data ends at `$BE00`, and
  the stack has 512 bytes. That is plenty: cc45 keeps the variables in
  static storage, not on the stack, and filled with a pattern then run
  through typing, find and replace, undo, the style drop-down, a Markdown
  export and list mode in xemu, the stack never went deeper than 32
  bytes. The gap (above) took 470 bytes, even with its hottest parts in
  assembly (`gap.s45`, `gapmove.s45`: cc45's 32-bit address arithmetic
  made the C some three times larger), so the C data ended at `$BF00` and
  the stack had 256 bytes. cc45's reading of struct fields at their
  addresses, once (M65Compiler #657-#659), then made the C 827 bytes
  smaller, and Markdown became one module again (`MARKDOWN.OVL`, import
  and export: 6710 bytes), so `MOD_SIZE` went from 6016 to 6784 bytes:
  the C data ended at `$BE00`, and the stack had 512 bytes. Then the C
  half's base page moved below the BASIC program, to `$1600`
  (`src/lowmem.s45`: BASIC 65 leaves `$1600`-`$1EFF` free, and a session
  left a pattern there untouched), and its CPU stack to the page after it,
  `$1700`-`$17FF` (`basic45 --stack-page=0x17`; BASIC's own page-1 stack,
  `--stack-page=basic`, hung a session now and then in xemu, the ROM's
  Ctrl-S pause flag set), so the C data ends at `$BC39`: 967 bytes free
  below `$C000`. Then the BASIC program's messages, prompts and menu text
  (58 strings) went into a table on the disk, `WRITER.MSG` (made from
  `src/messages.txt` by `tools/mkmsg.py`, with their numbers, `msg.h`,
  `#include`d by both halves), loaded at each start to `$1800`, the RAM
  the graphics kernel uses for BASIC's graphics commands (which the
  Writer does not use): BASIC calls `SYS wp_say, MSG_...` with any
  file name or number poked into `msg` (`@` a field to a comma, `%` the
  rest). The BASIC program went from 4861 to 3945 bytes, so the C half
  starts at `$3000`; the C grew 233 bytes, and the C data ends at
  `$BA1F`: 1504 bytes free. (The drawing's caches, saved in colour RAM
  while `DEFAULT0.OVL` runs, are now saved alone, not the whole of
  `mod_area` past its buffers up to `EDIT_SIZE`, which with the menu's
  text gone from it would not fit there.) HTML export and import, as
  modules (`HTML.OVL`, `HTMLIN.OVL`), then cost 94 bytes of BASIC and 87 of C:
  the BASIC program ends at `$2FC7` (57 bytes below the C half), and the C
  data at `$BA76`: 1417 bytes free. The style table (#5, its first step:
  every character's style read through it) cost 812 bytes of C: `sty`,
  `sty_find` and the loading's merge and remap in assembly
  (`src/style.s45`, 337 bytes: in C they were some 1100), the rest
  `doc_pack` / `doc_unpack` and the callers. `basic45 --link-align=1`
  (M65Compiler #678) then put the C half right after the BASIC program, at
  `$2FC8`, not the next page: the C data ends at `$BD6A`, 661 bytes free.
  (The BASIC program's
  two `poke`s of `$D1` / `$F7` are written in decimal: `--minify` turns
  `poke $d1` into a variable,
  [M65Compiler #603](https://github.com/CTalkobt/M65Compiler/issues/603).)
  The glyph
  bitmaps (the raw-view font's too, fetched by DMA) live in bank 4
  (`WRITER.FNT`), the C files that do not run on every key are compiled
  with `-O1`, the help is a
  document on the disk rather than text in the program, and the C half
  reads a line of the line table (bank 4) whole with one DMA (`get_line`)
  rather than field by field: with cc45 each far `LINES[l].field` costs
  about 60 bytes of code
  ([M65Compiler #585](https://github.com/CTalkobt/M65Compiler/issues/585)).
- **ln45 and tail calls**: ln45 places functions' static variables and
  base-page blocks so that two functions share bytes only when neither
  reaches the other through calls; for that it counts JSRs and JMPs. But
  the call graph in its map, and its recursion, thunk and clobber checks,
  count JSRs only, so a tail call (ca45 makes a JSR followed by RTS a JMP)
  is missing there: a recursion through one links without the "recursive"
  error ([M65Compiler #615](https://github.com/CTalkobt/M65Compiler/issues/615)).
  `tools/mkmod.py` reads the map's graph to find the functions waiting at a
  call into a module (`ed`'s `jmp edit_run`), so the Makefile compiles with
  `-fno-tail-call-optimization` (it costs nothing here: the code came out
  a few bytes smaller). A call ln45 cannot see at all, into a module, is
  worse: nothing keeps the module's callees apart from the functions
  waiting for it. `src/modcall.s45`'s JSRs tell ln45 what `DEFAULT0.OVL`
  calls, and mkmod.py checks every module: no function waiting while it
  runs may share static storage with one it calls.
- **cc45 and array members**: before M65Compiler master `a7191e09`, cc45
  kept only the low byte of an array member's address when an integer was
  added to it (`s.a + n`: the pointer landed in page 0;
  [M65Compiler #594](https://github.com/CTalkobt/M65Compiler/issues/594)).
  render.c's buffers in `mod_area` are such members: build with a cc45
  that has the fix.
