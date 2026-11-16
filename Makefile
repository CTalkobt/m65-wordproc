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
HOSTFLAGS = -std=c99 -Wall -Wextra -O1 -g -Isrc

BUILD  = build
TARGET = $(BUILD)/writer.prg
DISK   = $(BUILD)/writer.d81

CSRC    = src/api.c src/doc.c src/layout.c src/render.c src/md.c src/list.c src/m65.c src/fonts.c
PORTABLE = src/api.c src/doc.c src/layout.c src/render.c src/md.c src/list.c src/fonts.c
HEADERS = src/wp.h

# The C half is compiled here (basic45 --link takes the objects): -O2 for
# the drawing (the most work on every key), -O1 for the rest, layout
# included, which cc45 makes much smaller (md.c: 8.2 KB at -O2, 6.3 KB at
# -O1): bank 0 is full.
CC45FLAGS = -I$(CC45_ROOT)/lib/include -Isrc -I$(BUILD)
FAST    = render m65 fonts
SMALL   = api doc layout
OBJS    = $(foreach f,$(FAST) $(SMALL),$(BUILD)/obj/$(f).o45)
CCQUIET = 2>&1 | grep -v 'note:\|^ *[0-9]* |\|^ *|' || true

$(BUILD)/obj/%.o45: src/%.c $(HEADERS)
	@mkdir -p $(BUILD)/obj
	$(CC45) $(if $(filter $*,$(SMALL)),-O1,-O2) $(CC45FLAGS) -c $< -o $@ $(CCQUIET)

# (basic45 -v logs its ln45 command, and BASIC45_KEEP_TMP keeps that link's
# inputs in build/tmp: tools/mkmod.py links again for the program's map.)
#
# Modules (.OVL files, loaded by BASIC when used and only when on the disk):
# linked by tools/mkmod.py to run at the program's mod_area, against its
# map; the build's ID (of the sources and the tools) ties each to the
# program it was linked with (render.c's mod_run refuses another's).
MODS    = $(BUILD)/markdown.ovl $(BUILD)/list.ovl
BUILD_ID := $(shell cat src/*.c src/*.h src/wp.bas tools/mkmod.py $(CC45) $(CC45_ROOT)/bin/ln45 2>/dev/null | cksum | awk '{print $$1 % 65536}')

$(BUILD)/buildid.h: FORCE
	@mkdir -p $(BUILD)
	@echo '#define BUILD_ID $(BUILD_ID)u' > $@.new
	@cmp -s $@.new $@ && rm $@.new || mv $@.new $@

$(BUILD)/obj/render.o45: $(BUILD)/buildid.h

$(BUILD)/obj/md_mod.o45: src/md.c $(HEADERS)
	@mkdir -p $(BUILD)/obj
	$(CC45) -O1 $(CC45FLAGS) -c $< -o $@ $(CCQUIET)

$(BUILD)/markdown.ovl: $(TARGET) $(BUILD)/obj/md_mod.o45 tools/mkmod.py
	python3 tools/mkmod.py $(BUILD)/link.log $@ $(BUILD_ID) $(BUILD)/obj/md_mod.o45

# LIST.OVL: resident (loaded at the start), at the end of mod_area
# (LIST_OFS); render.c's list_marks calls it while drawing (inlined: from
# draw_line and render_raw)
$(BUILD)/obj/list_mod.o45: src/list.c $(HEADERS)
	@mkdir -p $(BUILD)/obj
	$(CC45) -O1 $(CC45FLAGS) -c $< -o $@ $(CCQUIET)

$(BUILD)/list.ovl: $(TARGET) $(BUILD)/obj/list_mod.o45 tools/mkmod.py
	python3 tools/mkmod.py --at _mod_area --ofs LIST_OFS --size LIST_SIZE --entry _list_main \
	    --from _draw_line__render,_render_raw__render \
	    $(BUILD)/link.log $@ $(BUILD_ID) $(BUILD)/obj/list_mod.o45

.PHONY: all run shot test fonts clean FORCE
.DEFAULT_GOAL := all

all: $(DISK)

$(TARGET): src/wp.bas $(OBJS)
	@mkdir -p $(BUILD)
	@rm -rf $(BUILD)/tmp && mkdir -p $(BUILD)/tmp
	BASIC45_KEEP_TMP=1 TMPDIR=$(abspath $(BUILD)/tmp) $(BASIC45) -v $(foreach f,$(OBJS),--link $(f)) --report src/wp.bas -o $@ 2>$(BUILD)/link.log || (cat $(BUILD)/link.log; false)
	@grep -v '^\[basic45:INFO\]\|note:' $(BUILD)/link.log || true

$(DISK): $(TARGET) src/writer.fnt $(BUILD)/help.prg $(MODS)
	rm -f $@
	$(DISK45) create $@ -n "WRITER" -i "65"
	$(DISK45) add $@ $(TARGET) "WRITER"
	$(DISK45) add $@ src/writer.fnt "WRITER.FNT"
	$(DISK45) add $@ $(BUILD)/help.prg "WRITER.HELP"
	$(DISK45) add $@ $(BUILD)/markdown.ovl "MARKDOWN.OVL"
	$(DISK45) add $@ $(BUILD)/list.ovl "LIST.OVL"

# the document shown at the start, made on the host by the C half itself
$(BUILD)/mkhelp: tools/mkhelp.c tools/helpdoc.c tests/host.c $(PORTABLE) $(HEADERS)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTFLAGS) -o $@ tools/mkhelp.c tools/helpdoc.c tests/host.c $(PORTABLE)

$(BUILD)/help.prg: $(BUILD)/mkhelp
	$(BUILD)/mkhelp $@

run: $(DISK)
	xemu-xmega65 -prgmode 65 -8 $(DISK) -prg $(TARGET)

shot: $(DISK)
	python3 tools/xemu_run.py $(TARGET) $(DISK) $(BUILD)/writer.png wait:8

$(BUILD)/test_wp: tests/test_wp.c tests/host.c tools/helpdoc.c $(PORTABLE) $(HEADERS)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTFLAGS) -fsanitize=address,undefined -o $@ tests/test_wp.c tests/host.c tools/helpdoc.c $(PORTABLE)

test: $(BUILD)/test_wp
	$(BUILD)/test_wp
	python3 -c "from PIL import Image; [Image.open('$(BUILD)/%s.ppm' % n).save('$(BUILD)/%s.png' % n) for n in ('page', 'raw', 'list_page', 'list_raw', 'long', 'justify', 'fuzz', 'fuzz2')]"

fonts:
	python3 tools/mkfonts.py src/fonts.c src/writer.fnt $(BUILD)/fonts.png

clean:
	rm -rf $(BUILD)
