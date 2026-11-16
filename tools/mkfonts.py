#!/usr/bin/env python3
"""mkfonts.py — the word processor's fonts: src/fonts.c and src/writer.fnt

Usage:  python3 tools/mkfonts.py src/fonts.c src/writer.fnt [preview.png]

The document fonts' glyphs, and where each starts, are the file writer.fnt,
which the BASIC program loads (BLOAD) to FONT_LIN in bank 4 before it starts
the C half; fonts.c has the fonts' metrics and advance widths, which the
layout reads all the time and so stay in bank 0. The raw-view font (8 x 13,
13 bytes a glyph) is in the file too, after the offsets (font_raw_at).

Each document font is a TrueType font rasterised by FreeType at one pixel
size without anti-aliasing (PIL's 1-bit mode, hinted), one glyph at a time,
so every glyph has a whole-pixel advance. The raw-view / user-interface font
is the X11 misc 8x13 bitmap font, with two glyphs of its own: 127 the
paragraph mark and 128 a solid block.

The format (wp.h, struct font):
    height     the cell height (ascent + descent), rows per glyph
    ascent     the baseline, rows from the top
    widths[]   the advance of each code 32..LAST (pixels, at most 16)
    offs       FONT_LIN + where its offsets are in writer.fnt: each glyph's
               first byte from bits (2 bytes, low first)
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


def emit_widths(out, cname, widths):
    n = LAST - FIRST + 1
    out.write("static const u8 %s_w[%d] = {\n" % (cname, n))
    for i in range(0, n, 16):
        out.write("    " + ", ".join(str(w) for w in widths[i:i + 16]) + ",\n")
    out.write("};\n")


def emit(out, blob, offs_blob, cname, height, ascent, glyphs):
    """a document font: its glyphs to blob, its offsets to offs_blob (both
    in writer.fnt: offs_blob after blob), its metrics to fonts.c"""
    bits, offs, widths = rasterise(glyphs)
    base = len(blob)
    obase = len(offs_blob)
    for o in offs:
        offs_blob.extend((o & 0xFF, o >> 8))
    blob.extend(bits)
    out.write("/* %s: %d rows, baseline %d, %d bytes */\n" % (cname, height, ascent, len(bits)))
    emit_widths(out, cname, widths)
    return lambda glyph_end: out.write(
        "const struct font %s = { %d, %d, %s_w, FONT_LIN + %dUL, FONT_LIN + %dUL };\n\n"
        % (cname, height, ascent, cname, glyph_end + obase, base))


def emit_raw(out, blob, height, glyphs):
    """the raw-view font: its glyphs (fixed 8 wide) at the end of blob"""
    bits, _, _ = rasterise(glyphs)
    assert all(adv == 8 for adv, _ in glyphs) and len(bits) == height * len(glyphs)
    out.write("/* the raw-view font: 8 x %d, in writer.fnt */\n" % height)
    out.write("const u32 font_raw_at = FONT_LIN + %dUL;\n" % len(blob))
    blob.extend(bits)


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
    rh, ra, rg = raw_glyphs()
    blob = bytearray()
    offs_blob = bytearray()
    with open(out_path, "w") as out:
        out.write("/* fonts.c — generated by tools/mkfonts.py; do not edit */\n")
        out.write('#include "wp.h"\n\n')
        finish = [emit(out, blob, offs_blob, cname, h, a, g) for cname, h, a, g in fonts]
        glyph_end = len(blob)
        for f in finish:
            f(glyph_end)
        blob.extend(offs_blob)
        emit_raw(out, blob, rh, rg)
    if len(blob) > FONT_MAX:
        sys.exit("mkfonts: %d bytes of glyphs, more than the %d of FONT_LIN's area" % (len(blob), FONT_MAX))
    with open(fnt_path, "wb") as f:
        f.write(bytes([0x00, 0xA0]))        # a PRG's load address ($A000: FONT_LIN in bank 4)
        f.write(blob)
    print("%s, %s: %d bytes of glyphs and offsets" % (out_path, fnt_path, len(blob)))
    fonts.append(("font_raw", rh, ra, rg))
    if len(sys.argv) > 3:
        preview(fonts, sys.argv[3])


if __name__ == "__main__":
    main()
