#!/usr/bin/env python3
"""mkfonts.py — the word processor's fonts: src/fonts.c and src/writer.fnt

Usage:  python3 tools/mkfonts.py src/fonts.c src/writer.fnt [preview.png]

The fonts' glyphs are the file writer.fnt, which the BASIC program loads
(BLOAD) to the scratch area at the start; the C half (font_install) copies
its first part to FONT_LIN in bank 4 and its second to HI_LIN in colour RAM
(issue #1: with the Latin-1 codes $A0-$FF the glyphs no longer fit in bank
4). fonts.c has where each font's block is, the fonts' metrics and the
advance widths of codes 32-128, which the layout reads all the time and so
stay in bank 0.

Each font is one block, in whichever part has room: for the document
fonts, a record for each code 32-255 (3 bytes: its advance, and its
glyph's first byte from the glyphs, low byte first; codes 129-159 are "?")
and then the glyphs; for the raw-view font (8 x 13), 13 bytes for each
code 32-255. The compose table (compose_at) is a block too: what the
Compose key makes of two characters, three bytes an entry (the two
ASCII characters, each pair in both orders; the Latin-1 code), a zero at
its end.

Each document font is a TrueType font rasterised by FreeType at one pixel
size without anti-aliasing (PIL's 1-bit mode, hinted), one glyph at a time,
so every glyph has a whole-pixel advance. The raw-view / user-interface font
is the X11 misc 8x13 bitmap font, with two glyphs of its own: 127 the
paragraph mark and 128 a solid block.

The format (wp.h, struct font):
    height     the cell height (ascent + descent), rows per glyph
    ascent     the baseline, rows from the top
    widths[]   the advance of each code 32..LAST (pixels, at most 16)
    offs       where its records are (FONT_LIN + ... or HI_LIN + ...)
    bits       where its glyphs start (after the records): glyph rows,
               top first, 1 byte a row (width <= 8) or 2 (<= 16), the
               leftmost pixel in bit 7 of the first byte
"""
import gzip
import io
import sys

from PIL import Image, ImageDraw, ImageFont
from PIL.PcfFontFile import PcfFontFile

import unicodedata

FIRST = 32
LAST = 128          # 127 = paragraph mark, 128 = block (raw font); others: blank
HI_FIRST = 0xA0     # Latin-1, in the second part
HI_LAST = 0xFF
LOW = list(range(FIRST, LAST + 1))
HIGH = list(range(HI_FIRST, HI_LAST + 1))

DOCFONTS = [
    # name,  C name,  file, size
    ("Sans", "font_sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 12),
    ("Serif", "font_serif", "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf", 12),
    ("Mono", "font_mono", "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf", 13),
    ("Head", "font_head", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 19),
]
RAWFONT = "/usr/share/fonts/X11/misc/8x13.pcf.gz"


def ttf_glyphs(path, size, codes):
    f = ImageFont.truetype(path, size)
    ascent, descent = f.getmetrics()
    height = ascent + descent
    glyphs = []
    for c in codes:
        ch = chr(c) if c < 127 or c >= HI_FIRST else " "
        adv = int(round(f.getlength(ch)))
        adv = max(1, min(16, adv))
        img = Image.new("1", (adv + 8, height), 0)
        d = ImageDraw.Draw(img)
        d.fontmode = "1"
        d.text((0, 0), ch, font=f, fill=1)
        rows = []
        for y in range(height):
            rows.append([img.getpixel((x, y)) for x in range(adv)])
        glyphs.append((adv, rows))
    return height, ascent, glyphs


def raw_glyphs(codes):
    pcf = PcfFontFile(io.BytesIO(gzip.open(RAWFONT).read()))
    height, ascent = 13, 11
    glyphs = []
    for c in codes:
        rows = [[0] * 8 for _ in range(height)]
        if c < 127 or c >= HI_FIRST:
            g = pcf.glyph[c]
            if g:
                (dx, dy), (x0, y0, x1, y1), _, im = g
                # the box: x0..x1 from the origin, y0 (negative: above) to y1 from the baseline
                for y in range(im.size[1]):
                    for x in range(im.size[0]):
                        if im.getpixel((x, y)):
                            row = ascent + y0 + y
                            col = x0 + x
                            if 0 <= row < height and 0 <= col < 8:
                                rows[row][col] = 1
        elif c == 127:      # pilcrow
            pat = ["..####..", ".###.#..", ".###.#..", ".###.#..", "..##.#..",
                   "....#.#.", "....#.#.", "....#.#.", "....#.#."]
            for i, p in enumerate(pat):
                rows[1 + i] = [1 if ch == "#" else 0 for ch in p]
        else:               # solid block
            rows = [[1] * 8 for _ in range(height)]
        glyphs.append((8, rows))
    return height, ascent, glyphs


FONT_MAX = 12288     # FONT_LIN's area (wp.h)
HI_MAX = 20480       # HI_LIN's (colour RAM $FF83000-$FF87FFF)

# The compose table's pairs that are not a letter and its accent
COMPOSE_MORE = {
    "ss": 0xDF, "ae": 0xE6, "AE": 0xC6, "o/": 0xF8, "O/": 0xD8, "d-": 0xF0, "D-": 0xD0,
    "th": 0xFE, "TH": 0xDE, "!!": 0xA1, "??": 0xBF, "<<": 0xAB, ">>": 0xBB, "c/": 0xA2,
    "L-": 0xA3, "Y=": 0xA5, "so": 0xA7, "co": 0xA9, "ro": 0xAE, "oo": 0xB0, "+-": 0xB1,
    "mu": 0xB5, "PP": 0xB6, "..": 0xB7, "12": 0xBD, "14": 0xBC, "34": 0xBE, "1^": 0xB9,
    "2^": 0xB2, "3^": 0xB3, "xx": 0xD7, ":-": 0xF7, "a_": 0xAA, "o_": 0xBA, "  ": 0xA0,
    "-,": 0xAC, "||": 0xA6, "ox": 0xA4, "''": 0xB4, ",,": 0xB8, '""': 0xA8, "^-": 0xAF,
}
# a combining accent: the character typed for it
ACCENTS = {0x300: "`", 0x301: "'", 0x302: "^", 0x303: "~", 0x308: '"', 0x30A: "o", 0x327: ","}


def compose_table():
    """three bytes an entry: two characters (either order), the Latin-1 code"""
    pairs = dict(COMPOSE_MORE)
    for c in HIGH:
        d = unicodedata.decomposition(chr(c)).split()
        if len(d) == 2 and int(d[1], 16) in ACCENTS:
            pairs[ACCENTS[int(d[1], 16)] + chr(int(d[0], 16))] = c
    seen = {}
    for k, v in pairs.items():
        key = "".join(sorted(k))
        if key in seen and seen[key] != v:
            sys.exit("mkfonts: compose %r and %r are the same pair" % (k, seen[key]))
        seen[key] = v
    t = bytearray()
    for k, v in sorted(pairs.items(), key=lambda kv: kv[1]):
        t.extend((ord(k[0]), ord(k[1]), v))
        if k[0] != k[1]:
            t.extend((ord(k[1]), ord(k[0]), v))
    t.append(0)
    return t, len(pairs)


def rasterise(glyphs):
    """the glyphs' rows as bytes; each glyph's first byte; the widths"""
    bits, offs, widths = [], [], []
    for adv, rows in glyphs:
        offs.append(len(bits))
        widths.append(adv)
        for r in rows:
            v = 0
            for x, p in enumerate(r):
                if p:
                    v |= 0x8000 >> x
            bits.append(v >> 8)
            if adv > 8:
                bits.append(v & 0xFF)
    return bits, offs, widths


ALL = range(FIRST, 256)          # the codes a block has (129-159: "?")


def font_block(glyphs):
    """a document font's block: a record for each code of ALL, the glyphs
    (glyphs: LOW then HIGH)"""
    bits, offs, widths = rasterise(glyphs)
    rec = dict(zip(LOW + HIGH, zip(widths, offs)))
    block = bytearray()
    for c in ALL:
        w, o = rec.get(c, rec[ord("?")])
        block.extend((w, o & 0xFF, o >> 8))
    return block + bytes(bits)


def raw_block(height, glyphs):
    bits, _, _ = rasterise(glyphs)
    g = dict(zip(LOW + HIGH, (bits[i * height:(i + 1) * height] for i in range(len(glyphs)))))
    block = bytearray()
    for c in ALL:
        block.extend(g.get(c, g[ord("?")]))
    return block


def preview(fonts, path):
    img = Image.new("1", (640, 32 * len(fonts)), 1)
    y = 2
    text = "The quick brown fox jumps over the lazy dog. 0123456789 (MEGA65)!? \u00c4\u00d6\u00dc\u00e4\u00f6\u00fc\u00df \u00c0\u00c9\u00ee\u00f1\u00e7\u00e5 \u00ab\u00bb\u00bf\u00a9"
    for _, height, ascent, glyphs in fonts:
        x = 2
        for ch in text:
            adv, rows = glyphs[ord(ch) - FIRST if ord(ch) <= LAST else ord(ch) - HI_FIRST + LAST + 1 - FIRST]
            for r, row in enumerate(rows):
                for c, p in enumerate(row):
                    if p and x + c < 640:
                        img.putpixel((x + c, y + r), 0)
            x += adv
        y += height + 6
    img.resize((img.size[0] * 2, img.size[1] * 2)).save(path)


def main():
    out_path, fnt_path = sys.argv[1], sys.argv[2]
    fonts = []
    for title, cname, path, size in DOCFONTS:
        h, a, g = ttf_glyphs(path, size, LOW)
        _, _, gh = ttf_glyphs(path, size, HIGH)
        fonts.append((cname, h, a, g + gh))
    rh, ra, rg = raw_glyphs(LOW + HIGH)
    table, npairs = compose_table()
    blocks = [(cname, font_block(g)) for cname, _, _, g in fonts]
    blocks += [("font_raw", raw_block(rh, rg)), ("compose", table)]
    # each block in a part, the largest first, where it fits
    parts = [["FONT_LIN", FONT_MAX, bytearray()], ["HI_LIN", HI_MAX, bytearray()]]
    at = {}
    for name, block in sorted(blocks, key=lambda b: -len(b[1])):
        for part in parts:
            if len(part[2]) + len(block) <= part[1]:
                at[name] = "%s + %dUL" % (part[0], len(part[2]))
                part[2].extend(block)
                break
        else:
            sys.exit("mkfonts: no room for %s (%d bytes)" % (name, len(block)))
    with open(out_path, "w") as out:
        out.write("/* fonts.c — generated by tools/mkfonts.py; do not edit */\n")
        out.write('#include "wp.h"\n\n')
        for cname, h, a, g in fonts:
            base = at[cname]
            out.write("/* %s: %d rows, baseline %d */\n" % (cname, h, a))
            out.write("const struct font %s = { %d, %d, %s, %s + %dUL };\n"
                      % (cname, h, a, base, base.replace("UL", ""), 3 * len(ALL)))
        out.write("\n/* the raw-view font: 8 x %d */\n" % rh)
        out.write("const u32 font_raw_at = %s;\n" % at["font_raw"])
        out.write("/* the compose table: %d pairs */\n" % npairs)
        out.write("const u32 compose_at = %s;\n" % at["compose"])
        out.write("/* writer.fnt's two parts (font_install) */\n")
        out.write("const u16 font_lo_size = %d, font_hi_size = %d;\n" % (len(parts[0][2]), len(parts[1][2])))
    with open(fnt_path, "wb") as f:
        f.write(bytes([0x00, 0x00]))        # a PRG's load address ($0000: SCRATCH_LIN in bank 5)
        f.write(parts[0][2])
        f.write(parts[1][2])
    print("%s, %s: %d + %d bytes" % (out_path, fnt_path, len(parts[0][2]), len(parts[1][2])))
    fonts.append(("font_raw", rh, ra, rg))
    if len(sys.argv) > 3:
        preview(fonts, sys.argv[3])


if __name__ == "__main__":
    main()
