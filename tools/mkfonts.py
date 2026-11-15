#!/usr/bin/env python3
"""mkfonts.py — the word processor's fonts: src/fonts.c and src/writer.fnt

Usage:  python3 tools/mkfonts.py src/fonts.c src/writer.fnt [preview.png]

The glyphs' bitmaps are the file writer.fnt, which the BASIC program loads
(BLOAD) to FONT_LIN in bank 4 before it starts the C half; fonts.c has the
fonts' metrics, their advance widths and each glyph's place in the file,
which the layout reads all the time and so stay in bank 0.

Each document font is a TrueType font rasterised by FreeType at one pixel
size without anti-aliasing (PIL's 1-bit mode, hinted), one glyph at a time,
so every glyph has a whole-pixel advance. The raw-view / user-interface font
is the X11 misc 8x13 bitmap font, with two glyphs of its own: 127 the
paragraph mark and 128 a solid block.

The format (wp.h, struct font):
    height     the cell height (ascent + descent), rows per glyph
    ascent     the baseline, rows from the top
    widths[]   the advance of each code 32..LAST (pixels, at most 16)
    offs[]     each glyph's first byte in bits[]
    bits       FONT_LIN + where its glyphs start in writer.fnt: glyph rows,
               top first, 1 byte a row (width <= 8) or 2 (<= 16), the
               leftmost pixel in bit 7 of the first byte
"""
import gzip
import io
import sys

from PIL import Image, ImageDraw, ImageFont
from PIL.PcfFontFile import PcfFontFile

FIRST = 32
LAST = 128          # 127 = paragraph mark, 128 = block (raw font); others: blank

DOCFONTS = [
    # name,  C name,  file, size
    ("Sans", "font_sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 12),
    ("Serif", "font_serif", "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf", 12),
    ("Mono", "font_mono", "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf", 13),
    ("Head", "font_head", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 19),
]
RAWFONT = "/usr/share/fonts/X11/misc/8x13.pcf.gz"


def ttf_glyphs(path, size):
    f = ImageFont.truetype(path, size)
    ascent, descent = f.getmetrics()
    height = ascent + descent
    glyphs = []
    for c in range(FIRST, LAST + 1):
        ch = chr(c) if c < 127 else " "
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


def raw_glyphs():
    pcf = PcfFontFile(io.BytesIO(gzip.open(RAWFONT).read()))
    height, ascent = 13, 11
    glyphs = []
    for c in range(FIRST, LAST + 1):
        rows = [[0] * 8 for _ in range(height)]
        if c < 127:
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


def emit(out, blob, cname, title, height, ascent, glyphs):
    base = len(blob)
    bits = []
    offs = []
    widths = []
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
    n = LAST - FIRST + 1
    out.write("/* %s: %d rows, baseline %d, %d bytes */\n" % (title, height, ascent, len(bits)))
    out.write("static const u8 %s_w[%d] = {\n" % (cname, n))
    for i in range(0, n, 16):
        out.write("    " + ", ".join(str(w) for w in widths[i:i + 16]) + ",\n")
    out.write("};\n")
    out.write("static const u16 %s_o[%d] = {\n" % (cname, n))
    for i in range(0, n, 12):
        out.write("    " + ", ".join(str(o) for o in offs[i:i + 12]) + ",\n")
    out.write("};\n")
    blob.extend(bits)
    if cname == "font_raw":
        # the raw view's and the bars' font is drawn most: its glyphs in bank 0 too
        out.write("const u8 font_raw_rows[%d] = {\n" % len(bits))
        for i in range(0, len(bits), 16):
            out.write("    " + ", ".join("0x%02X" % b for b in bits[i:i + 16]) + ",\n")
        out.write("};\n")
    out.write("const struct font %s = { %d, %d, %s_w, %s_o, FONT_LIN + %dUL };\n\n"
              % (cname, height, ascent, cname, cname, base))
    return len(bits)


def preview(fonts, path):
    img = Image.new("1", (640, 32 * len(fonts)), 1)
    y = 2
    text = "The quick brown fox jumps over the lazy dog. 0123456789 (MEGA65)!?"
    for _, height, ascent, glyphs in fonts:
        x = 2
        for ch in text:
            adv, rows = glyphs[ord(ch) - FIRST]
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
        h, a, g = ttf_glyphs(path, size)
        fonts.append((cname, h, a, g))
    h, a, g = raw_glyphs()
    fonts.append(("font_raw", h, a, g))
    blob = bytearray()
    with open(out_path, "w") as out:
        out.write("/* fonts.c — generated by tools/mkfonts.py; do not edit */\n")
        out.write('#include "wp.h"\n\n')
        for cname, h, a, g in fonts:
            emit(out, blob, cname, cname, h, a, g)
    if len(blob) > FONT_MAX:
        sys.exit("mkfonts: %d bytes of glyphs, more than the %d of FONT_LIN's area" % (len(blob), FONT_MAX))
    with open(fnt_path, "wb") as f:
        f.write(bytes([0x00, 0xA0]))        # a PRG's load address ($A000: FONT_LIN in bank 4)
        f.write(blob)
    print("%s, %s: %d bytes of glyphs" % (out_path, fnt_path, len(blob)))
    if len(sys.argv) > 3:
        preview(fonts, sys.argv[3])


if __name__ == "__main__":
    main()
