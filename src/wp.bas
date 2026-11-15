' wp.bas — MEGA65 Writer: the BASIC 65 half
'
' The BASIC program runs the editor: it reads the keys, decides what each
' one means, runs the menu, asks for file names and does the disk work
' (BSAVE, BLOAD, DOPEN / FWRITE# / FREAD#, DIR) and the help screen. The C
' half (wp.h: api.c and the rest, linked with basic45 --link) keeps the
' document, lays it out and draws the 640 x 400 screen; BASIC calls it with
' SYS {name}, a and reads results with RREG.
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
#define CMD_DEMO 19
#define CMD_CLEAN 20

#define GET_DIRTY 0
#define GET_PACK 2
#define GET_EXPORT 3
#define GET_UNPACK 4
#define GET_SEL 5
#define GET_PREP 6
#define GET_IMPORT 7

' the file image's place: bank 5, $7600 (SCRATCH_LIN in wp.h)
#define IMG 30208

start:
  print chr$(14);
  ' the fonts' glyphs to bank 4, $A000 (FONT_LIN in wp.h): once
  bank 0 : fl = peek({fonts_loaded}) : bank 128
  if fl <> 165 then print p"Loading fonts..." : bload "writer.fnt", b4, p(40960) : bank 0 : poke {fonts_loaded}, 165 : bank 128
  ' F-keys give their codes, not their KEY macros: the Editor's lock bit
  ' (bit 5 of locks, $F7) as KEY OFF sets it, without KEY's wait for a
  ' pending macro ($D1) to drain, which never ends in a running program
  bank 128 : poke $d1, 0 : poke $f7, peek($f7) or 32
  ' RUN/STOP ignored while editing
  sys {wp_stopvec}, 1
  ' the first time: the welcome document (a Writer document on the disk),
  ' loaded before the graphics screen is on (none: an empty document)
  wl = 0
  if fl <> 165 then wl = 1 : trap welcomeerr : bload "welcome", b5, p(IMG) : wl = 2
wpinit:
  trap
  sys {wp_init}
  if wl = 2 then sys {wp_get}, GET_UNPACK
  fi$ = p"untitled"

mainloop:
  trap errh
waitkey:
  ' between keys the C half lays out what is left and reads CTRL + cursor
  ' keys from the keyboard (GET does not give them, or not as such)
  sys {wp_idle}
  get k$ : if k$ = "" then waitkey
  k = asc(k$)
  select k
  case 133
    sys {wp_cmd}, CMD_VIEW
  case 134
    sys {wp_cmd}, CMD_BOLD
  case 135
    sys {wp_cmd}, CMD_ITALIC
  case 136
    sys {wp_cmd}, CMD_UNDER
  case 137
    sys {wp_cmd}, CMD_FONT
  case 138
    sys {wp_cmd}, CMD_ALIGN
  case 139
    sys {wp_cmd}, CMD_PAGEBREAK
  case 140, 132
    call helpscreen
  case 16
    sys {wp_cmd}, CMD_MARK
  case 21
    sys {wp_cmd}, CMD_COPY
    call say(p"Copied.")
  case 22
    sys {wp_cmd}, CMD_CUT
  case 23
    sys {wp_cmd}, CMD_PASTE
  case 25
    sys {wp_cmd}, CMD_PGUP
  case 26
    sys {wp_cmd}, CMD_PGDN
  case 27
    sys {wp_get}, GET_SEL : rreg se
    if se then sys {wp_cmd}, CMD_UNMARK : else call menu
  case else
    sys {wp_key}, k
  endselect
  goto mainloop

' ---- the menu (ESC) ----
proc menu
  call say(p"N)ew  L)oad  S)ave  I)mport  E)xport  D)irectory  H)elp  Q)uit   ESC: back")
  getkey m$
  m = asc(m$) and 127
  select m
  case 78
    call newdoc
  case 76
    call loaddoc
  case 83
    call savedoc
  case 73
    call importtext
  case 69
    call exporttext
  case 68
    call showdir
  case 72
    call helpscreen
  case 81
    call quit
  case else
    call say("")
  endselect
endproc

' ---- the bottom bar's message: m$ poked into the C half's msg ----
proc say(m$)
  ml = len(m$)
  if ml > 78 then ml = 78
  bank 0
  if ml > 0 then for i = 1 to ml : poke {msg} + i - 1, asc(mid$(m$, i, 1)) : next
  poke {msg} + ml, 0
  bank 128
  sys {wp_setmsg}
endproc

' the document's name in the top bar
proc setname
  bank 0
  for i = 1 to len(fi$) : poke {fname} + i - 1, asc(mid$(fi$, i, 1)) : next
  poke {fname} + len(fi$), 0
  bank 128
  sys {wp_setname}
endproc

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
proc discard
  yes = -1
  sys {wp_get}, GET_DIRTY : rreg dt
  if dt then call confirm(p"Discard the unsaved changes?")
endproc

' ---- the commands ----
proc newdoc
  call discard
  if yes then
    sys {wp_cmd}, CMD_NEW
    fi$ = p"untitled" : call setname
  endif
endproc

proc savedoc
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
      fi$ = a$ : sys {wp_cmd}, CMD_CLEAN : call setname
      call say(p"Saved " + a$ + " (" + mid$(str$(n), 2) + p" bytes).")
    endif
  endif
endproc

proc loaddoc
  call discard
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
        fi$ = a$ : call setname
        call say(p"Loaded " + a$ + ".")
      endif
    endif
  endif
endproc

' a sequential PETSCII file as the text (no styles)
proc importtext
  call discard
  if yes then
    a$ = ""
    call ask(p"Import text file: ")
    if ok and a$ <> "" then
      call say(p"Importing " + a$ + "...")
      sys {wp_get}, GET_PREP
      dopen#2, (a$)
      ' 1 KB at a time until the end of the file (past it the drive gives CRs,
      ' which wp_get GET_IMPORT trims)
      p = 0
      do
        fread#2, $57600 + p * 1024, 1024
        p = p + 1
      loop until st <> 0 or p >= 14
      dclose#2
      sys {wp_video}, 1
      sys {wp_get}, GET_IMPORT : rreg lo, hi
      fi$ = p"untitled" : call setname
      call say(p"Imported " + mid$(str$(lo + 256 * hi), 2) + p" characters.")
    endif
  endif
endproc

' the text as a sequential PETSCII file
proc exporttext
  a$ = ""
  call ask(p"Export text as: ")
  if ok and a$ <> "" then
    sys {wp_get}, GET_EXPORT : rreg lo, hi
    n = lo + 256 * hi
    scratch (a$)
    dopen#2, (a$), w
    fwrite#2, $57600, n
    dclose#2
    sys {wp_video}, 1
    call say(p"Exported " + mid$(str$(n), 2) + p" characters to " + a$ + ".")
  endif
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
    sys {wp_stopvec}, 0 : poke $f7, peek($f7) and 223
    print "{clear}";
    print p"MEGA65 Writer: bye. RUN starts it again with the document kept."
    end
  endif
  call say("")
endproc

' ---- help: on BASIC's text screen ----
proc helpscreen
  sys {wp_video}, 0
  print "{clear}{lower}{white}";
  print p"{rev} MEGA65 Writer {off}  a word processor in BASIC 65 and C"
  print
  print p"{yellow}Editing{white}"
  print p"  Type to insert.  RETURN new paragraph.  DEL deletes left, INST right."
  print p"  Cursor keys move; HOME line start/end; CLR document start/end."
  print p"  F13 / F14 page up / down.  TAB inserts four spaces."
  print
  print p"{yellow}View{white}"
  print p"  F1   page layout (fonts, styles, pages as printed) / raw (codes shown)"
  print
  print p"{yellow}Formatting{white}  (the selection, or what you type next)"
  print p"  F3 bold   F5 italic   F7 underline   F2 font: Sans, Serif, Mono, Head"
  print p"  F4 paragraph alignment: left, centre, right, justify"
  print p"  F6 page break"
  print
  print p"{yellow}Selection and clipboard{white}"
  print p"  F9 starts a selection at the cursor (again or ESC ends it); move to"
  print p"  extend it.  CTRL + cursor keys start or extend one, too."
  print p"  F10 copy   F11 cut   F12 paste   typing replaces it"
  print
  print p"{yellow}Menu (ESC){white}"
  print p"  N new   L load   S save (Writer documents keep fonts and styles)"
  print p"  I import / E export plain text (sequential PETSCII files)"
  print p"  D directory   H this help   Q quit"
  print
  print p"{lblue}Press a key to go back to the document.{white}";
  getkey c$
  sys {wp_video}, 1
endproc

' no welcome document: start with an empty one
welcomeerr:
  wl = 0
  resume wpinit

' ---- errors (disk errors among them): shown, then back to the keys ----
errh:
  dclose
  sys {wp_video}, 1
  call say(p"Error: " + err$(er))
  resume mainloop
