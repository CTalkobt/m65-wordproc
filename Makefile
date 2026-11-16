# MEGA65 Writer — a word processor in BASIC 65 and C (the cc45 toolchain)
#
#   make            build/writer.prg and build/writer.d81
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

CSRC    = src/api.c src/doc.c src/layout.c src/render.c src/md.c src/m65.c src/fonts.c
PORTABLE = src/api.c src/doc.c src/layout.c src/render.c src/md.c src/fonts.c
HEADERS = src/wp.h

# The C half is compiled here (basic45 --link takes the objects): -O2 for
# the drawing (the most work on every key), -O1 for the rest, layout
# included, which cc45 makes much smaller (md.c: 8.2 KB at -O2, 6.3 KB at
# -O1): bank 0 is full.
CC45FLAGS = -I$(CC45_ROOT)/lib/include -Isrc
FAST    = render m65 fonts
SMALL   = api doc md layout
OBJS    = $(foreach f,$(FAST) $(SMALL),$(BUILD)/obj/$(f).o45)

$(BUILD)/obj/%.o45: src/%.c $(HEADERS)
	@mkdir -p $(BUILD)/obj
	$(CC45) $(if $(filter $*,$(SMALL)),-O1,-O2) $(CC45FLAGS) -c $< -o $@ 2>&1 | grep -v 'note:\|^ *[0-9]* |\|^ *|' || true

.PHONY: all run shot test fonts clean
.DEFAULT_GOAL := all

all: $(DISK)

$(TARGET): src/wp.bas $(OBJS)
	@mkdir -p $(BUILD)
	$(BASIC45) $(foreach f,$(OBJS),--link $(f)) --report src/wp.bas -o $@

$(DISK): $(TARGET) src/writer.fnt $(BUILD)/help.prg
	rm -f $@
	$(DISK45) create $@ -n "WRITER" -i "65"
	$(DISK45) add $@ $(TARGET) "WRITER"
	$(DISK45) add $@ src/writer.fnt "WRITER.FNT"
	$(DISK45) add $@ $(BUILD)/help.prg "WRITER.HELP"

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
	python3 -c "from PIL import Image; [Image.open('$(BUILD)/%s.ppm' % n).save('$(BUILD)/%s.png' % n) for n in ('page', 'raw', 'long', 'justify', 'fuzz', 'fuzz2')]"

fonts:
	python3 tools/mkfonts.py src/fonts.c src/writer.fnt $(BUILD)/fonts.png

clean:
	rm -rf $(BUILD)
