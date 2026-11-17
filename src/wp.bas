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
' Modules (.OVL files: MDIMPORT.OVL, MDEXPORT.OVL, RTF.OVL, LIST.OVL) are loaded only when they
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

#define GET_DIRTY 0
#define GET_PACK 2
#define GET_EXPORT 3
#define GET_UNPACK 4
#define GET_SEL 5
#define GET_PREP 6
#define GET_IMPORT 7
#define GET_MDEXPORT 8
#define GET_MDIMPORT 9
#define GET_LISTOK 10
#define GET_RTFEXPORT 11
#define GET_RTFBEGIN 12
#define GET_RTFCHUNK 13
#define GET_RTFEND 14
#define GET_FONTS 15
#define GET_KIND 16

' LIST.OVL's place: mod_area + LIST_OFS (wp.h)
#define LIST_OFS 4288

' the file image's place: bank 5, $7600 (SCRATCH_LIN in wp.h)
#define IMG 30208

start:
  print chr$(14);
  ' the fonts' glyphs, once: to the scratch area, then the C half copies
  ' codes 32-128 to bank 4 (FONT_LIN in wp.h) and the Latin-1 ones and the
  ' compose table to colour RAM (HI_LIN)
  bank 0 : fl = peek({fonts_loaded}) : bank 128
  if fl <> 165 then print p"Loading fonts..." : bload "writer.fnt", b5, p(IMG) : sys {wp_get}, GET_FONTS : bank 0 : poke {fonts_loaded}, 165 : bank 128
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
  fi$ = p"untitled"
  gosub loadlist

mainloop:
  trap errh
waitkey:
  ' between keys the C half lays out what is left and reads CTRL + cursor
  ' keys from the keyboard (GET does not give them, or not as such)
  sys {wp_idle}
  get k$ : if k$ = "" then waitkey
  k = asc(k$)
  ' a command key: its command (wp_cmd), by a table (as select's cases
  ' cost some 60 bytes each): F1 view, F3 / F5 / F7 bold / italic /
  ' underline, F2 font, F4 align, F6 page break, F9 mark, F11 cut, F12
  ' paste, F13 / F14 page up / down (CMD_... in the second string)
  i = instr("{$85}{$86}{$87}{$88}{$89}{$8a}{$8b}{$10}{$16}{$17}{$19}{$1a}", k$)
  if i then sys {wp_cmd}, asc(mid$("{$07}{$01}{$02}{$03}{$04}{$05}{$06}{$08}{$0b}{$0c}{$10}{$11}", i, 1)) : goto mainloop
  ' F8 / HELP the help, F10 copy, CTRL+D the style drop-down, CTRL+L the
  ' screen drawn again, ESC the menu (or the selection's end)
  i = instr("{$8c}{$84}{$15}{$04}{$0c}{$1b}", k$)
  if i then on i gosub loadhelp, loadhelp, copysel, stylemenu, redraw, escape : goto mainloop
  sys {wp_key}, k
  goto mainloop

' (A subroutine without parameters is a label and RETURN, reached with
' GOSUB, rather than a proc: basic45 puts a GOTO over each proc, even
' where nothing can fall into it, M65Compiler #605. Each ends in RETURN,
' so none falls into the next.)
copysel:
  sys {wp_cmd}, CMD_COPY
  call say(p"Copied.")
  return

redraw:
  sys {wp_redraw}
  return

escape:
  sys {wp_get}, GET_SEL : rreg se
  if se then sys {wp_cmd}, CMD_UNMARK : else gosub menu
  return

listmode:
  if lv then sys {wp_cmd}, CMD_LIST
  return

' ---- the menu (ESC): a panel of three rows at the bottom, the commands
' grouped (List only when LIST.OVL is loaded) ----
menu:
  b$ = ""
  if lv then
    bank 0 : lq = peek({list_on}) : bank 128
    b$ = p"  T)List on"
    if lq then b$ = p"  T)List off"
  endif
  call setstr(p"Menu:", {mod_area})
  call setstr(p"File:  N)ew  L)oad  S)ave  I)mport  E)xport  D)ir", {mod_area} + 81)
  call setstr(p"View:  R)edraw" + b$ + p"    H)elp  Q)uit    ESC: back", {msg})
  sys {wp_panel}, 2
  getkey m$
  sys {wp_panel}, 0
  on instr(p"nlsiedhrtq", chr$(asc(m$) and 127)) gosub newdoc, loaddoc, savedoc, importtext, exporttext, showdir, loadhelp, redraw, listmode, quit
  return

' ---- the style drop-down (CTRL+D) under the top bar's style field: the
' cursor keys move it, RETURN applies the style lit (to the paragraphs
' selected whole, or the cursor's; as a character style to the rest of the
' selection), N 1 2 3 Q C B # at once, ESC leaves it ----
stylemenu:
  sys {wp_get}, GET_KIND : rreg sv
  do
    sys {wp_drop}, sv
    getkey m$ : m = asc(m$)
    ' (a comparison is -1 when true)
    sv = mod(sv - (m = 17) + (m = 145) + 8, 8)
    i = instr(p"n123qcb#", m$)
    if i then sv = i - 1 : m = 13
  loop until m = 13 or m = 27
  sys {wp_drop}, 255
  if m = 13 then sys {wp_cmd}, CMD_STYLE + sv
  return

' s$ (PETSCII, at most 78 characters) poked at sa in bank 0, then a 0
proc setstr(s$, sa)
  sl = len(s$)
  if sl > 78 then sl = 78
  bank 0
  if sl > 0 then for i = 1 to sl : poke sa + i - 1, asc(mid$(s$, i, 1)) : next
  poke sa + sl, 0
  bank 128
endproc

' ---- the bottom bar's message: m$ poked into the C half's msg ----
proc say(m$)
  call setstr(m$, {msg})
  sys {wp_setmsg}
endproc

' the document's name in the top bar
setname:
  call setstr(fi$, {fname})
  sys {wp_setname}
  return

' a line typed into a$ (starting as it is); ok = 1 when RETURN ended it
proc ask(q$)
  ok = 0
.again:
  call say(q$ + a$ + "_")
  getkey c$
  c = asc(c$)
  if c = 13 then ok = 1 : goto .done
  if c = 27 then goto .done
  if c = 20 and len(a$) > 0 then a$ = left$(a$, len(a$) - 1)
  if len(a$) < 16 and ((c > 32 and c < 128 and c <> 34 and c <> 44 and c <> 58) or (c >= 193 and c <= 218)) then a$ = a$ + c$
  goto .again
.done:
  call say("")
endproc

' y or n; yes is true for y
proc confirm(q$)
  call say(q$ + p" (Y/N)")
  getkey c$
  yes = (asc(c$) and 127) = 89
  call say("")
endproc

' unsaved changes: yes when they may be lost
discard:
  yes = -1
  sys {wp_get}, GET_DIRTY : rreg dt
  if dt then call confirm(p"Discard the unsaved changes?")
  return

' ---- the commands ----
newdoc:
  gosub discard
  if yes then
    sys {wp_cmd}, CMD_NEW
    fi$ = p"untitled" : gosub setname
  endif
  return

savedoc:
  a$ = fi$
  if a$ = p"untitled" then a$ = ""
  call ask(p"Save as: ")
  if ok and a$ <> "" then
    call say(p"Saving " + a$ + "...")
    sys {wp_get}, GET_PACK : rreg lo, hi
    n = lo + 256 * hi
    scratch (a$)
    bsave (a$), b5, p(IMG) to p(IMG + n)
    sys {wp_video}, 1
    if ds then
      call say(p"Not saved: " + ds$)
    else
      fi$ = a$ : sys {wp_cmd}, CMD_CLEAN : gosub setname
      call say(p"Saved " + a$ + " (" + mid$(str$(n), 2) + p" bytes).")
    endif
  endif
  return

loaddoc:
  gosub discard
  if yes then
    a$ = ""
    call ask(p"Load: ")
    if ok and a$ <> "" then
      call say(p"Loading " + a$ + "...")
      bload (a$), b5, p(IMG)
      sys {wp_video}, 1
      sys {wp_get}, GET_UNPACK : rreg r
      if r then
        call say(a$ + p" is not a Writer document (try I)mport).")
      else
        fi$ = a$ : gosub setname
        call say(p"Loaded " + a$ + ".")
      endif
    endif
  endif
  return

' a sequential PETSCII file as the text (no styles)
importtext:
  gosub discard
  if yes then
    a$ = ""
    call ask(p"Import (name.md: Markdown, name.rtf: RTF): ")
    if ok and a$ <> "" then
      call say(p"Importing " + a$ + "...")
      gosub ftype
      ml = 1 : lo = 0 : hi = 0
      if fk = 2 then
        gosub rtfin
      else
        sys {wp_get}, GET_PREP
        dopen#2, (a$)
        ' 1 KB at a time until the end of the file (past it the drive gives
        ' CRs, which the import trims) or the scratch area's end (30 KB)
        p = 0
        do
          fread#2, $57600 + p * 1024, 1024
          p = p + 1
        loop until st <> 0 or p >= 30
        dclose#2
        ' (Markdown: by MDIMPORT.OVL, loaded last: drawing overwrites it)
        if fk then gosub loadmod
        if ml then sys {wp_get}, GET_IMPORT + fk * (GET_MDIMPORT - GET_IMPORT) : rreg lo, hi
      endif
      sys {wp_video}, 1
      if fk then gosub loadlist
      gosub modsaid
      if ok then
        fi$ = p"untitled" : gosub setname
        call say(p"Imported " + mid$(str$(lo + 256 * hi), 2) + p" characters.")
      endif
    endif
  endif
  return

' RTF: by RTF.OVL, loaded first, the file streamed to it 1 KB at a time (it
' can be larger than the scratch area); nothing is drawn until the end
rtfin:
  gosub loadmod
  if ml then
    sys {wp_get}, GET_RTFBEGIN : rreg lo, hi
    if lo <> 255 or hi <> 255 then
      dopen#2, (a$)
      do
        fread#2, $57600, 1024
        s = st
        sys {wp_get}, GET_RTFCHUNK
      loop until s <> 0
      dclose#2
      sys {wp_get}, GET_RTFEND : rreg lo, hi
    endif
  endif
  return

exporttext:
  a$ = ""
  call ask(p"Export as (name.md: Markdown, name.rtf: RTF): ")
  if ok and a$ <> "" then
    gosub ftype
    ' (Markdown's export is a module of its own)
    if fk = 1 then mf$ = "mdexport.ovl" : mu$ = p"MDEXPORT.OVL"
    ml = 1 : lo = 0 : hi = 0
    if fk then
      gosub loadmod
      if ml then sys {wp_get}, GET_MDEXPORT + (fk - 1) * (GET_RTFEXPORT - GET_MDEXPORT) : rreg lo, hi
      gosub loadlist
    else
      sys {wp_get}, GET_EXPORT : rreg lo, hi
    endif
    n = lo + 256 * hi
    gosub modsaid
    if ok and n = 0 then call say(p"Too long for " + mn$ + p" (more than 30 KB).") : ok = 0
    if ok then
      scratch (a$)
      dopen#2, (a$), w
      fwrite#2, $57600, n
      dclose#2
      sys {wp_video}, 1
      call say(p"Exported " + mid$(str$(n), 2) + p" bytes to " + a$ + ".")
    endif
  endif
  return

' ---- modules: .OVL files on the disk, each loaded when it is needed into
' the C half's module region (mod_area, where the drawing keeps its
' buffers) and run at once (SYS {wp_get}, ...): Markdown (MDIMPORT.OVL, MDEXPORT.OVL),
' RTF (RTF.OVL); list mode (LIST.OVL) stays ----

' list mode: LIST.OVL, resident, loaded at the start (and again after a
' module run in mod_area has overwritten it) when it is on the disk; lv: 1
' when loaded and from this build (the menu offers it only then)
loadlist:
  trap nolist
  bload "list.ovl", b0, p({mod_area} + LIST_OFS)
listback:
  trap errh
  sys {wp_get}, GET_LISTOK : rreg lv
  return

' ml: 1 when the module mf$ loaded
loadmod:
  ml = 0
  trap nomd
  bload (mf$), b0, p({mod_area})
  ml = 1
mdback:
  trap errh
  return

' after a module's run: ok 0 when it said why there was none (ml, and
' lo / hi 255: the module from another build of Writer)
modsaid:
  ok = 1
  if ml = 0 then call say(mn$ + p" needs " + mu$ + p" on the disk.") : ok = 0
  if lo = 255 and hi = 255 then call say(mu$ + p" is from another version of Writer.") : ok = 0
  return

' fk: what a$ is by its end (either case): 0 text, 1 .md (Markdown), 2 .rtf
' (RTF); mf$: its module's file, mu$ its name shown, mn$ the format's
ftype:
  fk = 0
  b$ = right$(a$, 3)
  if b$ = p".md" or b$ = p".MD" then fk = 1 : mf$ = "mdimport.ovl" : mu$ = p"MDIMPORT.OVL" : mn$ = p"Markdown"
  b$ = right$(a$, 4)
  if b$ = p".rtf" or b$ = p".RTF" then fk = 2 : mf$ = "rtf.ovl" : mu$ = p"RTF.OVL" : mn$ = p"RTF"
  return

showdir:
  sys {wp_video}, 0
  print "{clear}{lower}";
  dir
  print
  print p"Press a key.";
  getkey c$
  sys {wp_video}, 1
  return

quit:
  gosub discard
  if yes then
    sys {wp_video}, 0
    sys {wp_stopvec}, 0 : poke 247, peek(247) and 223
    print "{clear}";
    print p"MEGA65 Writer: bye. RUN starts it again with the document kept."
    end
  endif
  call say("")
  return

' ---- the help: WRITER.HELP, a Writer document on the disk ----
loadhelp:
  gosub discard
  if yes then
    call say(p"Loading the help...")
    hl = 0
    trap nohelp
    bload "writer.help", b5, p(IMG)
    hl = 1
helpback:
    trap errh
    sys {wp_video}, 1
    if hl then
      sys {wp_get}, GET_UNPACK
      fi$ = p"untitled" : gosub setname
      call say(p"The help. ESC N starts a new document.")
    else
      call say(p"No help: WRITER.HELP is not on the disk.")
    endif
  endif
  return

' no LIST.OVL
nolist:
  resume listback

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
  call say(p"Error: " + err$(er))
  resume mainloop
