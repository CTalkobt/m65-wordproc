# MEGA65 Writer — a word processor in BASIC 65 and C (the cc45 toolchain)
#
#   make            build/writer.prg, its modules (build/*.ovl) and
#                   build/writer.d81
#   make run        build and start it in xemu
#   make shot       build and save a screenshot (build/writer.png, headless xemu;
#                   tools/xemu_run.py can also type keys first)
#   make test       the C half built and tested on the host (gcc), its
#                   screens written as build/*.png
#   make fonts      src/fonts.c and src/writer.fnt again from the TrueType /
#                   X11 fonts (and build/fonts.png, a sample of each)
#   make clean
#
# The BASIC program (src/wp.bas) is the editor's main loop, menu, dialogs
# and disk work; basic45 --link compiles the C files with cc45 and links
# them right after the BASIC program, in the same .prg.
#
# CC45_ROOT is the toolchain (bin/, lib/include): a checkout of M65Compiler
# next to this project by default.

CC45_ROOT ?= ../m65compiler
BASIC45   = $(CC45_ROOT)/bin/basic45
CC45      = $(CC45_ROOT)/bin/cc45
DISK45    = $(CC45_ROOT)/bin/disk45

HOSTCC    ?= cc
HOSTFLAGS = -std=c99 -Wall -Wextra -O1 -g -Isrc -I$(BUILD)

BUILD  = build
TARGET = $(BUILD)/writer.prg
MAP    = $(BUILD)/writer.map
DISK   = $(BUILD)/writer.d81

CSRC    = src/api.c src/conv.c src/doc.c src/layout.c src/render.c src/md.c src/mdin.c src/rtf.c src/html.c src/htmlin.c src/list.c src/edit.c src/edit_undo.c src/m65.c src/fonts.c
PORTABLE = src/api.c src/conv.c src/doc.c src/layout.c src/render.c src/md.c src/mdin.c src/rtf.c src/html.c src/htmlin.c src/list.c src/edit.c src/edit_undo.c src/fonts.c
HEADERS = src/wp.h src/petuni.h

# The C half is compiled here (basic45 --link takes the objects): -O2 for
# the drawing (the most work on every key), -O1 for the rest, layout
# included, which cc45 makes much smaller (md.c: 8.2 KB at -O2, 6.3 KB at
# -O1): bank 0 is full.
# -fno-tail-call-optimization: the call graph in ln45's map (and its
# recursion, thunk and clobber checks) is made from JSRs only, so a tail
# call (JSR + RTS made a JMP) is missing from it (M65Compiler #615).
# tools/mkmod.py reads that graph to find what is waiting at a call into a
# module (DEFAULT0.OVL: ed's tail call to edit_run). ln45's sharing of static
# storage does count JMPs.
CC45FLAGS = -fno-tail-call-optimization -I$(CC45_ROOT)/lib/include -Isrc -I$(BUILD)
# --minify: the BASIC program without comments and indentation, its lines
# joined and its long names shortened (737 bytes less, so the C half starts
# a page lower; needs M65Compiler #601, master since 6762f672)
# --link-align=1: the C half right after the BASIC program, not at the next
# page (as much as 255 bytes of bank 0 otherwise lost; M65Compiler #678,
# main since c6006fdd)
BASFLAGS ?= --minify --link-align=1
# the C half's CPU stack: a page of its own, $1700-$17FF (it never goes
# deeper than 32 bytes), next to its base page (src/lowmem.s45's $1600),
# not 512 bytes at the end of bank 0 (ln45 --stack-page, M65Compiler
# 2b53dc79). Not BASIC's page-1 stack (--stack-page=basic): with it a
# session hung now and then in xemu, the ROM's Ctrl-S pause flag ($1119)
# set. (0x17, not $17: the shell would take $1 as an argument)
STACKFLAGS = --stack-page=0x17
FAST    = render m65 fonts
SMALL   = api conv doc layout
# (conv.c is called by the modules, whose base-page blocks start at $80:
# its functions keep their variables out of the base page)
NOZP    = conv
OBJS    = $(foreach f,$(FAST) $(SMALL),$(BUILD)/obj/$(f).o45) $(BUILD)/obj/modcall.o45 $(BUILD)/obj/gap.o45 $(BUILD)/obj/gapmove.o45 $(BUILD)/obj/style.o45 $(BUILD)/obj/lowmem.o45 $(BUILD)/obj/vicunlock.o45
CCQUIET = 2>&1 | grep -v 'note:\|^ *[0-9]* |\|^ *|' || true

# The messages (src/messages.txt): WRITER.MSG, BLOADed by the BASIC program
# to $1800 (below it, free RAM), their numbers (msg.h: the BASIC program and
# the C half), and the table for the host build
MSGS = $(BUILD)/writer.msg $(BUILD)/msg.h $(BUILD)/msgtab.c
$(MSGS) &: src/messages.txt tools/mkmsg.py
	@mkdir -p $(BUILD)
	python3 tools/mkmsg.py src/messages.txt $(MSGS)

$(BUILD)/obj/api.o45 $(BUILD)/obj/render.o45: $(BUILD)/msg.h

$(BUILD)/obj/%.o45: src/%.c $(HEADERS)
	@mkdir -p $(BUILD)/obj
	$(CC45) $(if $(filter $*,$(SMALL)),-O1,-O2) $(if $(filter $*,$(NOZP)),-fno-zp-locals) $(CC45FLAGS) -c $< -o $@ $(CCQUIET)

$(BUILD)/obj/%.o45: src/%.s45
	@mkdir -p $(BUILD)/obj
	$(CC45_ROOT)/bin/ca45 -c $< -o $@

# Modules (.OVL files, loaded by BASIC when used and only when on the disk):
# linked by tools/mkmod.py to run at the program's mod_area, against its
# map (basic45 --map: M65Compiler #595, in master since 6a7dd256); the build's ID (of the sources and the tools) ties each to the
# program it was linked with (render.c's mod_run refuses another's).
MODS    = $(BUILD)/markdown.ovl $(BUILD)/mdin.ovl $(BUILD)/rtf.ovl $(BUILD)/html.ovl $(BUILD)/htmlin.ovl $(BUILD)/list.ovl $(BUILD)/default0.ovl
BUILD_ID := $(shell cat src/*.c src/*.h src/wp.bas tools/mkmod.py $(CC45) $(CC45_ROOT)/bin/ln45 2>/dev/null | cksum | awk '{print $$1 % 65536}')

$(BUILD)/buildid.h: FORCE
	@mkdir -p $(BUILD)
	@echo '#define BUILD_ID $(BUILD_ID)u' > $@.new
	@cmp -s $@.new $@ && rm $@.new || mv $@.new $@

$(BUILD)/obj/render.o45: $(BUILD)/buildid.h

# MARKDOWN.OVL: Markdown export (md.c), a run-once module
$(BUILD)/obj/md_mod.o45: src/md.c $(HEADERS)
	@mkdir -p $(BUILD)/obj
	$(CC45) -O1 $(CC45FLAGS) -c $< -o $@ $(CCQUIET)

$(BUILD)/markdown.ovl: $(TARGET) $(BUILD)/obj/md_mod.o45 tools/mkmod.py
	python3 tools/mkmod.py --entry _md_main $(MAP) $(TARGET) $@ $(BUILD_ID) $(BUILD)/obj/md_mod.o45

# MDIN.OVL: Markdown import (mdin.c), apart from the export (together they
# set MOD_SIZE)
$(BUILD)/obj/mdin_mod.o45: src/mdin.c $(HEADERS)
	@mkdir -p $(BUILD)/obj
	$(CC45) -O1 $(CC45FLAGS) -c $< -o $@ $(CCQUIET)

$(BUILD)/mdin.ovl: $(TARGET) $(BUILD)/obj/mdin_mod.o45 tools/mkmod.py
	python3 tools/mkmod.py --entry _mdin_main $(MAP) $(TARGET) $@ $(BUILD_ID) $(BUILD)/obj/mdin_mod.o45

$(BUILD)/obj/rtf_mod.o45: src/rtf.c $(HEADERS)
	@mkdir -p $(BUILD)/obj
	$(CC45) -O1 $(CC45FLAGS) -c $< -o $@ $(CCQUIET)

$(BUILD)/rtf.ovl: $(TARGET) $(BUILD)/obj/rtf_mod.o45 tools/mkmod.py
	python3 tools/mkmod.py --entry _rtf_main $(MAP) $(TARGET) $@ $(BUILD_ID) $(BUILD)/obj/rtf_mod.o45

# HTML.OVL: HTML export (html.c), a run-once module
$(BUILD)/obj/html_mod.o45: src/html.c $(HEADERS)
	@mkdir -p $(BUILD)/obj
	$(CC45) -O1 $(CC45FLAGS) -c $< -o $@ $(CCQUIET)

$(BUILD)/html.ovl: $(TARGET) $(BUILD)/obj/html_mod.o45 tools/mkmod.py
	python3 tools/mkmod.py --entry _html_main $(MAP) $(TARGET) $@ $(BUILD_ID) $(BUILD)/obj/html_mod.o45

# HTMLIN.OVL: HTML import (htmlin.c), streamed as RTF.OVL's (its own module:
# with the export it is more than MOD_SIZE)
$(BUILD)/obj/htmlin_mod.o45: src/htmlin.c $(HEADERS)
	@mkdir -p $(BUILD)/obj
	$(CC45) -O1 $(CC45FLAGS) -c $< -o $@ $(CCQUIET)

$(BUILD)/htmlin.ovl: $(TARGET) $(BUILD)/obj/htmlin_mod.o45 tools/mkmod.py
	python3 tools/mkmod.py --entry _htmlin_main $(MAP) $(TARGET) $@ $(BUILD_ID) $(BUILD)/obj/htmlin_mod.o45

# LIST.OVL: resident (loaded at the start), at the end of mod_area
# (LIST_OFS); render.c's list_marks calls it while drawing (inlined: from
# draw_line and render_raw)
$(BUILD)/obj/list_mod.o45: src/list.c $(HEADERS)
	@mkdir -p $(BUILD)/obj
	$(CC45) -O1 $(CC45FLAGS) -c $< -o $@ $(CCQUIET)

$(BUILD)/list.ovl: $(TARGET) $(BUILD)/obj/list_mod.o45 tools/mkmod.py
	python3 tools/mkmod.py --at _mod_area --ofs LIST_OFS --size LIST_SIZE --entry _list_main \
	    --from _draw_line__render,_render_raw__render \
	    $(MAP) $(TARGET) $@ $(BUILD_ID) $(BUILD)/obj/list_mod.o45

# DEFAULT0.OVL: undo, find and replace; BLOADed at the start to mod_area, kept
# in colour RAM, and copied into mod_area's first EDIT_SIZE bytes (the
# drawing's buffers) for each call: doc.c's edit_run
$(BUILD)/obj/edit_mod.o45: src/edit.c $(HEADERS)
	@mkdir -p $(BUILD)/obj
	$(CC45) -O1 $(CC45FLAGS) -c $< -o $@ $(CCQUIET)

$(BUILD)/obj/edit_undo_mod.o45: src/edit_undo.c $(HEADERS)
	@mkdir -p $(BUILD)/obj
	$(CC45) -O1 -fno-zp-locals $(CC45FLAGS) -c $< -o $@ $(CCQUIET)

$(BUILD)/default0.ovl: $(TARGET) $(BUILD)/obj/edit_mod.o45 $(BUILD)/obj/edit_undo_mod.o45 tools/mkmod.py
	python3 tools/mkmod.py --size EDIT_SIZE --entry _edit_main --from _edit_run \
	    $(MAP) $(TARGET) $@ $(BUILD_ID) $(BUILD)/obj/edit_mod.o45 $(BUILD)/obj/edit_undo_mod.o45

.PHONY: all run shot test fonts clean FORCE
.DEFAULT_GOAL := all

all: $(DISK)

$(TARGET): src/wp.bas $(OBJS) $(BUILD)/msg.h
	@mkdir -p $(BUILD)
	$(BASIC45) -I $(BUILD) $(foreach f,$(OBJS),--link $(f)) --map $(MAP) $(BASFLAGS) $(STACKFLAGS) --report src/wp.bas -o $@

$(DISK): $(TARGET) src/writer.fnt $(BUILD)/writer.msg $(BUILD)/help.prg $(MODS)
	rm -f $@
	$(DISK45) create $@ -n "WRITER" -i "65"
	$(DISK45) add $@ $(TARGET) "WRITER"
	$(DISK45) add $@ src/writer.fnt "WRITER.FNT"
	$(DISK45) add $@ $(BUILD)/writer.msg "WRITER.MSG"
	$(DISK45) add $@ $(BUILD)/help.prg "WRITER.HELP"
	$(DISK45) add $@ $(BUILD)/markdown.ovl "MARKDOWN.OVL"
	$(DISK45) add $@ $(BUILD)/mdin.ovl "MDIN.OVL"
	$(DISK45) add $@ $(BUILD)/rtf.ovl "RTF.OVL"
	$(DISK45) add $@ $(BUILD)/html.ovl "HTML.OVL"
	$(DISK45) add $@ $(BUILD)/htmlin.ovl "HTMLIN.OVL"
	$(DISK45) add $@ $(BUILD)/list.ovl "LIST.OVL"
	$(DISK45) add $@ $(BUILD)/default0.ovl "DEFAULT0.OVL"

# the document shown at the start, made on the host by the C half itself
$(BUILD)/mkhelp: tools/mkhelp.c tools/helpdoc.c tests/host.c $(PORTABLE) $(HEADERS) $(MSGS)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTFLAGS) -o $@ tools/mkhelp.c tools/helpdoc.c tests/host.c $(PORTABLE) $(BUILD)/msgtab.c

$(BUILD)/help.prg: $(BUILD)/mkhelp
	$(BUILD)/mkhelp $@

run: $(DISK)
	xemu-xmega65 -prgmode 65 -8 $(DISK) -prg $(TARGET)

shot: $(DISK)
	python3 tools/xemu_run.py $(TARGET) $(DISK) $(BUILD)/writer.png wait:8

$(BUILD)/test_wp: tests/test_wp.c tests/host.c tools/helpdoc.c $(PORTABLE) $(HEADERS) $(MSGS)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTFLAGS) -fsanitize=address,undefined -o $@ tests/test_wp.c tests/host.c tools/helpdoc.c $(PORTABLE) $(BUILD)/msgtab.c

# (test_wp_s: the same with the style table's entries 0-31 in another
# order, so that a style read without the table (sty) shows)
$(BUILD)/test_wp_s: tests/test_wp.c tests/host.c tools/helpdoc.c $(PORTABLE) $(HEADERS) $(MSGS)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTFLAGS) -DSTY_SCRAMBLE -fsanitize=address,undefined -o $@ tests/test_wp.c tests/host.c tools/helpdoc.c $(PORTABLE) $(BUILD)/msgtab.c

test: $(BUILD)/test_wp $(BUILD)/test_wp_s
	$(BUILD)/test_wp_s
	$(BUILD)/test_wp
	python3 -c "from PIL import Image; [Image.open('$(BUILD)/%s.ppm' % n).save('$(BUILD)/%s.png' % n) for n in ('page', 'raw', 'list_page', 'list_raw', 'latin_page', 'latin_raw', 'style_drop', 'kinds_page', 'kinds_raw', 'lists_page', 'long', 'justify', 'fuzz', 'fuzz2')]"

fonts:
	python3 tools/mkfonts.py src/fonts.c src/writer.fnt $(BUILD)/fonts.png

clean:
	rm -rf $(BUILD)
