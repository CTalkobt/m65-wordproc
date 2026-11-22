' wp.bas — MEGA65 Writer: the BASIC 65 half
'
' The BASIC program runs the editor: it reads the keys, decides what each
' one means, runs the menu, asks for file names and does the disk work
' (BSAVE, BLOAD, DOPEN / FWRITE# / FREAD#, DIR). The help is a document
' on the disk (WRITER.HELP), loaded at the start and with F8 / HELP. The C
' half (wp.h: api.c and the rest, linked with basic45 --link) keeps the
' document, lays it out and draws the 640 x 400 screen; BASIC calls it with
' SYS {name}, a and reads results with RREG.
'
' Modules (.OVL files: MARKDOWN.OVL, MDIN.OVL, RTF.OVL, HTML.OVL, HTMLIN.OVL, LIST.OVL, DEFAULT0.OVL) are loaded only when they
' are on the disk (below).
'
' Build: make  (basic45 --link src/api.c ... src/wp.bas -o build/writer.prg)

#define CMD_BOLD 1
#define CMD_ITALIC 2
#define CMD_UNDER 3
#define CMD_FONT 4
#define CMD_ALIGN 5
#define CMD_PAGEBREAK 6
#define CMD_VIEW 7
#define CMD_MARK 8
#define CMD_UNMARK 9
#define CMD_COPY 10
#define CMD_CUT 11
#define CMD_PASTE 12
#define CMD_NEW 13
#define CMD_LINEEND 14
#define CMD_DOCEND 15
#define CMD_PGUP 16
#define CMD_PGDN 17
#define CMD_ALL 18
#define CMD_CLEAN 20
#define CMD_LIST 21
#define CMD_STYLE 32
#define CMD_FX 48

#define GET_DIRTY 0
#define GET_PACK 2
#define GET_UNPACK 4
#define GET_SEL 5
#define GET_PREP 6
#define GET_IMPORT 7
#define GET_MDIMPORT 9
#define GET_LISTOK 10
#define GET_RTFBEGIN 12
' (then CHUNK, END: GET_RTFBEGIN + 1, + 2; HTML's likewise)
#define GET_HTMLBEGIN 21
#define GET_FONTS 15
#define GET_KIND 16
' + the file's kind (fk): the document exported, as text, Markdown, RTF, HTML
#define GET_XEXPORT 17

#define FIND_FWD 2
#define FIND_SETF 5
#define FIND_SETR 6

' LIST.OVL's place: mod_area + LIST_OFS (wp.h)
#define LIST_OFS 4700

' the file image's place: bank 5, $0000 (SCRATCH_LIN in wp.h)
#define IMG 0
' PETSCII.FNT's: bank 5, $6000 (PETFNT_OFS in wp.h)
#define PETFNT_OFS $6000

' the messages' numbers (MSG_...) and MSG_TAB, from src/messages.txt
' (tools/mkmsg.py)
#include "msg.h"

start:
  print chr$(14);
  ' the messages (WRITER.MSG) to free RAM below the program, each time:
  ' after the Writer it could be the graphics commands' buffers
  bload "writer.msg", b0, p(MSG_TAB)
  ' the C half's code overlays (WRITER.OV1, before any SYS into it: ln45
  ' --overlay-no-load), to bank 4 where its first two bytes say ($4D000)
  bload "writer.ov1", b4 : bank 128
  ' the fonts' glyphs, once: to the scratch area, then the C half copies
  ' codes 32-128 to bank 4 (FONT_LIN in wp.h) and the Latin-1 ones and the
  ' compose table to colour RAM (HI_LIN)
  bank 0 : fl = peek({fonts_loaded}) : bank 128
  ' (and PETSCII.FNT, a C64 character set, when it is on the disk: the C
  ' half makes the PETSCII fonts of it, else of the character ROM's)
  if fl <> 165 then print p"Loading fonts..." : bload "writer.fnt", b5, p(IMG) : edma 3, 4096, 0, $50000 + PETFNT_OFS : trap nopetfnt : bload "petscii.fnt", b5, p(PETFNT_OFS)
petback:
  trap
  if fl <> 165 then sys {wp_get}, GET_FONTS : rreg r : bank 0 : poke {fonts_loaded}, 165 : bank 128
  ' (the text is in attic RAM: none, no Writer)
  if r then print p"No attic RAM." : end
  ' F-keys give their codes, not their KEY macros: the Editor's lock bit
  ' (bit 5 of locks, $F7) as KEY OFF sets it, without KEY's wait for a
  ' pending macro ($D1) to drain, which never ends in a running program
  ' ($D1 and $F7 in decimal: basic45 --minify mangles poke $xx, M65Compiler #603)
  bank 128 : poke 209, 0 : poke 247, peek(247) or 32
  ' RUN/STOP ignored while editing
  sys {wp_stopvec}, 1
  ' the first time: the help (a Writer document on the disk), loaded
  ' before the graphics screen is on (none: an empty document)
  wl = 0
  if fl <> 165 then wl = 1 : trap helperr : bload "writer.help", b5, p(IMG) : wl = 2
wpinit:
  trap
  sys {wp_init}
  if wl = 2 then sys {wp_get}, GET_UNPACK
  am = 16
  call loadlist

mainloop:
  trap errh
waitkey:
  ' between keys the C half lays out what is left and reads CTRL + cursor
  ' keys from the keyboard (GET does not give them, or not as such)
  sys {wp_idle}
  get k$ : if k$ = "" then waitkey
  ' the key to the C half, which does what it can (the F-keys' and CTRL's
  ' commands among it) and leaves the rest here (ka, ACT_... in api.c): F8 /
  ' HELP the help, F10 copy, CTRL+D the style drop-down, CTRL+L the screen
  ' drawn again, ESC the menu (or the selection's end), CTRL+F find, CTRL+H
  ' replace, CTRL+W save, CTRL+E the font and effect pop-up
  sys {wp_key}, asc(k$) : rreg ka
  on ka gosub loadhelp, copysel, stylemenu, redraw, escape, dofind, doreplace, savedoc, fxmenu
  goto mainloop

proc copysel
  sys {wp_cmd}, CMD_COPY
  sys {wp_say}, MSG_COPIED
endproc

proc redraw
  sys {wp_redraw}
endproc

proc escape
  sys {wp_get}, GET_SEL : rreg se
  if se then sys {wp_cmd}, CMD_UNMARK : else call menu
endproc

proc listmode
  if lv then sys {wp_cmd}, CMD_LIST
endproc

' ---- the menu (ESC): a panel of three rows at the bottom (MSG_MENU, the
' commands grouped; List only when LIST.OVL is loaded) ----
proc menu
  sys {wp_panel}, 2
  getkey m$
  sys {wp_panel}, 0
  on instr(p"nlsiedhrtq", chr$(asc(m$) and 127)) gosub newdoc, loaddoc, savedoc, importtext, exporttext, showdir, loadhelp, redraw, listmode, quit
endproc

' ---- the style drop-down (CTRL+D) under the top bar's style field: the
' cursor keys move it, RETURN applies the style lit (to the paragraphs
' selected whole, or the cursor's; as a character style to the rest of the
' selection), N 1 2 3 Q C B # P at once, ESC leaves it ----
proc stylemenu
  sys {wp_get}, GET_KIND : rreg sv
  dk = 0 : dn = 9 : call dropdown
  if m = 13 then sys {wp_cmd}, CMD_STYLE + sv
endproc

' ---- the font and effect pop-up (CTRL+E), where the drop-down is: RETURN
' sets the font or turns the effect lit on or off (the selection's, or for
' typing) ----
proc fxmenu
  sv = 0 : dk = 64 : dn = 11 : call dropdown
  if m = 13 then sys {wp_cmd}, CMD_FX + sv
endproc

' (wp_drop's list dk, dn items, sv lit; m: RETURN or ESC)
proc dropdown
  do
    sys {wp_drop}, dk + sv
    getkey m$ : m = asc(m$)
    ' (a comparison is -1 when true)
    sv = mod(sv - (m = 17) + (m = 145) + dn, dn)
    i = instr(p"n123qcb#p", m$)
    if i and dk = 0 then sv = i - 1 : m = 13
  loop until m = 13 or m = 27
  sys {wp_drop}, 255
endproc

' s$ (PETSCII, at most 78 characters) poked at sa in bank 0, then a 0
proc setstr(s$, sa)
  sl = len(s$)
  if sl > 78 then sl = 78
  bank 0
  if sl > 0 then for i = 1 to sl : poke sa + i - 1, asc(mid$(s$, i, 1)) : next
  poke sa + sl, 0
  bank 128
endproc

' ---- the bottom bar's message: MSG_... m, its place holders filled from
' m$ (poked into the C half's msg; fields apart by commas) ----
proc sp(m, m$)
  call setstr(m$, {msg})
  sys {wp_say}, m
endproc

' the document's name in the top bar
proc setname
  call setstr(fi$, {fname})
  sys {wp_setname}
endproc

' a line typed into a$ (starting as it is) after the prompt MSG_... q, at
' most am characters (16: a file name, without spaces, quotes, commas or
' colons); ok = 1 when RETURN ended it
proc ask(q)
  ok = 0
.again:
  call sp(q, a$ + "_")
  getkey c$
  c = asc(c$)
  if c = 13 then ok = 1 : goto .done
  if c = 27 then goto .done
  if c = 20 and len(a$) > 0 then a$ = left$(a$, len(a$) - 1)
  if len(a$) < am and ((c > 31 and c < 128 and (am > 16 or (c > 32 and c <> 34 and c <> 44 and c <> 58))) or (c >= 193 and c <= 218)) then a$ = a$ + c$
  goto .again
.done:
  am = 16
  sys {wp_say}, MSG_NONE
endproc

' unsaved changes: yes when they may be lost (asked: true for y)
proc discard
  yes = -1
  sys {wp_get}, GET_DIRTY : rreg dt
  if dt then sys {wp_say}, MSG_DISCARD : getkey c$ : yes = (asc(c$) and 127) = 89 : sys {wp_say}, MSG_NONE
endproc

' ---- the commands ----
proc newdoc
  call discard
  if yes then
    sys {wp_cmd}, CMD_NEW
    fi$ = "" : call setname
  endif
endproc

' (CTRL+W, ka 8: under the document's name, fi$, asking only when it has
' none: "")
proc savedoc
  a$ = fi$
  if ka = 8 and a$ <> "" then ok = 1 : else call ask(MSG_SAVEAS)
  if ok and a$ <> "" then
    call sp(MSG_SAVING, a$)
    sys {wp_get}, GET_PACK : rreg lo, hi
    n = lo + 256 * hi
    scratch (a$)
    bsave (a$), b5, p(IMG) to p(IMG + n)
    sys {wp_video}, 1
    if ds then
      call sp(MSG_NOTSAVED, ds$)
    else
      fi$ = a$ : sys {wp_cmd}, CMD_CLEAN : call setname
      call sp(MSG_SAVED, a$ + "," + mid$(str$(n), 2))
    endif
  endif
endproc

' ---- find (CTRL+F) and replace (CTRL+H): the strings to the C half (as
' msg); f: 1 when a match is selected ----
proc dofind
  f = 0
  a$ = fs$ : am = 40
  call ask(MSG_FIND)
  if ok and a$ <> "" then fs$ = a$ : call setstr(a$, {msg}) : sys {wp_find}, FIND_SETF : sys {wp_find}, FIND_FWD : rreg f
endproc

' each match: Y replaced (then the next), N the next, A all in the
' document; another key stops. The C half asks (the bottom bar) while a
' match is selected, and says what it found; Y N A are FIND_REPL,
' FIND_FWD, FIND_ALL (another key: 0, FIND_STOP)
proc doreplace
  call dofind
  if f then
    a$ = rs$ : am = 40
    call ask(MSG_REPLWITH)
    if ok then
      rs$ = a$ : call setstr(a$, {msg}) : sys {wp_find}, FIND_SETR
      do while f
        getkey m$
        ' (INSTR apart: it uses $02-$04, where SYS has put its address)
        i = instr(p"yna", chr$(asc(m$) and 127))
        sys {wp_find}, i : rreg f
      loop
    endif
  endif
endproc

proc loaddoc
  call discard
  if yes then
    a$ = ""
    call ask(MSG_LOAD)
    if ok and a$ <> "" then
      call sp(MSG_LOADING, a$)
      bload (a$), b5, p(IMG)
      sys {wp_video}, 1
      sys {wp_get}, GET_UNPACK : rreg r
      if r then
        call sp(MSG_NOTWP, a$)
      else
        fi$ = a$ : call setname
        call sp(MSG_LOADED, a$)
      endif
    endif
  endif
endproc

' a sequential PETSCII file as the text (no styles). The file is opened
' once first: DOPEN of a missing one is no BASIC error, only the drive's
' (ds$, its number first), and the import would empty the document (#9)
proc importtext
  call discard
  if yes then
    a$ = ""
    call ask(MSG_IMPORT)
    if ok and a$ <> "" then call ftype : if fk = 1 then mf$ = "mdin.ovl"
    if fk = 2 then mf$ = "rtfin.ovl"
    if fk = 3 then mf$ = "htmlin.ovl"
    if ok and a$ <> "" then dopen#2, (a$) : e$ = ds$ : dclose#2 : if val(e$) then call sp(MSG_NOTIMP, e$) : ok = 0
    if ok and a$ <> "" then
      call sp(MSG_IMPORTING, a$)
      ml = 1 : lo = 0 : hi = 0
      if fk >= 2 then
        call streamin
      else
        sys {wp_get}, GET_PREP
        dopen#2, (a$)
        ' 512 bytes at a time until the end of the file (past it the drive
        ' gives CRs, which the import trims) or the scratch area's end (118
        ' of them: 60416 bytes; the clipboard is next)
        p = 0
        do
          fread#2, $50000 + p * 512, 512
          p = p + 1
        loop until st <> 0 or p >= 118
        dclose#2
        ' (Markdown: by MARKDOWN.OVL, loaded last: drawing overwrites it)
        if fk then call loadmod
        if ml then sys {wp_get}, GET_IMPORT + fk * (GET_MDIMPORT - GET_IMPORT) : rreg lo, hi
      endif
      sys {wp_video}, 1
      if fk then call loadlist
      call modsaid
      if ok then
        fi$ = "" : call setname
        call sp(MSG_IMPORTED, mid$(str$(lo + 256 * hi), 2))
      endif
    endif
  endif
endproc

' RTF and HTML: by RTF.OVL or HTMLIN.OVL, loaded first, the file streamed to
' it 1 KB at a time (it can be larger than the scratch area); nothing is
' drawn until the end. g: the module's first GET_...
proc streamin
  call loadmod
  g = GET_RTFBEGIN : if fk = 3 then g = GET_HTMLBEGIN
  if ml then
    sys {wp_get}, g : rreg lo, hi
    if lo <> 255 or hi <> 255 then
      dopen#2, (a$)
      do
        fread#2, $50000, 1024
        s = st
        sys {wp_get}, g + 1
      loop until s <> 0
      dclose#2
      sys {wp_get}, g + 2 : rreg lo, hi
    endif
  endif
endproc

proc exporttext
  a$ = ""
  call ask(MSG_EXPORT)
  if ok and a$ <> "" then
    call ftype
    ml = 1 : lo = 0 : hi = 0
    if fk then call loadmod
    if ml then sys {wp_get}, GET_XEXPORT + fk : rreg lo, hi
    if fk then call loadlist
    n = lo + 256 * hi
    call modsaid
    if ok and n = 0 then sys {wp_say}, MSG_TOOLONG + fk : ok = 0
    if ok then
      scratch (a$)
      dopen#2, (a$), w
      fwrite#2, $50000, n
      dclose#2
      sys {wp_video}, 1
      call sp(MSG_EXPORTED, mid$(str$(n), 2) + "," + a$)
    endif
  endif
endproc

' ---- modules: .OVL files on the disk, each loaded when it is needed into
' the C half's module region (mod_area, where the drawing keeps its
' buffers) and run at once (SYS {wp_get}, ...): Markdown (MDIMPORT.OVL, MDEXPORT.OVL),
' RTF (RTF.OVL); list mode (LIST.OVL) stays ----

' list mode: LIST.OVL, resident, loaded at the start (and again after a
' module run in mod_area has overwritten it) when it is on the disk; lv: 1
' when loaded and from this build (the menu offers it only then)
proc loadlist
  trap nolist
  bload "list.ovl", b0, p({mod_area} + LIST_OFS)
listback:
  ' (and once, DEFAULT0.OVL: undo, find and replace, which the C half keeps in
  ' colour RAM)
  trap noedit
  if ed = 0 then ed = 1 : bload "default0.ovl", b0, p({mod_area})
listback2:
  trap errh
  sys {wp_get}, GET_LISTOK : rreg lv
endproc

' ml: 1 when the module mf$ loaded
proc loadmod
  ml = 0
  trap nomd
  bload (mf$), b0, p({mod_area})
  ml = 1
mdback:
  trap errh
endproc

' after a module's run: ok 0 when it said why there was none (ml, and
' lo / hi 255: the module from another build of Writer; the messages by
' fk, 1 or 2)
proc modsaid
  ok = 1
  if ml = 0 then sys {wp_say}, MSG_NEEDS + fk - 1 : ok = 0
  if lo = 255 and hi = 255 then sys {wp_say}, MSG_OTHERVER + fk - 1 : ok = 0
endproc

' fk: what a$ is by its end (either case): 0 text, 1 .md (Markdown), 2 .rtf
' (RTF), 3 .htm or .html (HTML); mf$: its module's file
proc ftype
  fk = 0
  b$ = right$(a$, 3)
  if b$ = p".md" or b$ = p".MD" then fk = 1 : mf$ = "markdown.ovl"
  b$ = right$(a$, 4)
  if b$ = p".rtf" or b$ = p".RTF" then fk = 2 : mf$ = "rtf.ovl"
  if b$ = p".htm" or b$ = p".HTM" or b$ = p"html" or b$ = p"HTML" then fk = 3 : mf$ = "html.ovl"
endproc

proc showdir
  sys {wp_video}, 0
  print "{clear}{lower}";
  dir
  print
  print p"Press a key.";
  getkey c$
  sys {wp_video}, 1
endproc

proc quit
  call discard
  if yes then
    sys {wp_video}, 0
    sys {wp_stopvec}, 0 : poke 247, peek(247) and 223
    print "{clear}";
    print p"MEGA65 Writer: bye. RUN starts it again with the document kept."
    end
  endif
  sys {wp_say}, MSG_NONE
endproc

' ---- the help: WRITER.HELP, a Writer document on the disk ----
proc loadhelp
  call discard
  if yes then
    sys {wp_say}, MSG_HELPLOAD
    hl = 0
    trap nohelp
    bload "writer.help", b5, p(IMG)
    hl = 1
helpback:
    trap errh
    sys {wp_video}, 1
    if hl then
      sys {wp_get}, GET_UNPACK
      fi$ = "" : call setname
      sys {wp_say}, MSG_HELP
    else
      sys {wp_say}, MSG_NOHELP
    endif
  endif
endproc

' no PETSCII.FNT
nopetfnt:
  resume petback

' no LIST.OVL
nolist:
  resume listback

' no DEFAULT0.OVL
noedit:
  resume listback2

' no module file (mf$)
nomd:
  ml = 0
  resume mdback

' no help document for F8 / HELP: said so
nohelp:
  hl = 0
  resume helpback

' no help document at the start: an empty one
helperr:
  wl = 0
  resume wpinit

' ---- errors (disk errors among them): shown, then back to the keys ----
errh:
  dclose
  sys {wp_video}, 1
  call sp(MSG_ERROR, err$(er))
  resume mainloop
