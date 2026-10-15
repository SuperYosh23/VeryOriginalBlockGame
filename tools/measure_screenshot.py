#!/usr/bin/env python3
"""Measure the game's rendered geometry from a screenshot.

Written because eyeballing a rendered image is unreliable and the whole point
here is to get exact numbers: where the blocks actually landed, how big a cell
is in points, and where the HUD and preview panel boundaries are. Those numbers
either match the layout arithmetic in -layoutSubviews or they expose the bug.

Pure stdlib: the host has no PIL, so this decodes the PNG itself.
"""
import struct
import sys
import zlib


def decode_png(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos = 8
    idat = b""
    w = h = bitdepth = colortype = None
    plte = None
    trns = None
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if tag == b"IHDR":
            w, h, bitdepth, colortype, comp, filt, interlace = struct.unpack(
                ">IIBBBBB", body)
            assert bitdepth == 8, f"unsupported bit depth {bitdepth}"
            assert interlace == 0, "interlaced PNG not supported"
        elif tag == b"PLTE":
            plte = body
        elif tag == b"tRNS":
            trns = body
        elif tag == b"IDAT":
            idat += body
        elif tag == b"IEND":
            break

    raw = zlib.decompress(idat)
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colortype]
    bpp = channels
    stride = w * bpp

    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if f == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line

    # Normalise to RGB triples.
    px = bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            i = y * stride + x * bpp
            if colortype == 3:
                idx = out[i]
                r, g, b = plte[idx * 3], plte[idx * 3 + 1], plte[idx * 3 + 2]
            elif colortype in (0, 4):
                g = b = out[i]
                r = out[i]
            else:
                r, g, b = out[i], out[i + 1], out[i + 2]
            o = (y * w + x) * 3
            px[o] = r
            px[o + 1] = g
            px[o + 2] = b
    return w, h, px


def bbox(w, h, px, pred):
    x0, y0, x1, y1, count = w, h, -1, -1, 0
    for y in range(h):
        row = y * w * 3
        for x in range(w):
            o = row + x * 3
            if pred(px[o], px[o + 1], px[o + 2]):
                count += 1
                if x < x0:
                    x0 = x
                if x > x1:
                    x1 = x
                if y < y0:
                    y0 = y
                if y > y1:
                    y1 = y
    if count == 0:
        return None
    return x0, y0, x1, y1, count


def main(path):
    w, h, px = decode_png(path)
    print(f"image: {w}x{h} px")
    scale = 2 if w >= 700 else 1
    print(f"assuming @{scale}x -> {w / scale:.0f}x{h / scale:.0f} points")

    # Saturated (coloured) pixels = the tetromino blocks.
    def is_block(r, g, b):
        return max(r, g, b) - min(r, g, b) > 45 and max(r, g, b) > 70

    blocks = bbox(w, h, px, is_block)
    print("\n-- coloured blocks")
    if blocks is None:
        print("  none found")
    else:
        x0, y0, x1, y1, n = blocks
        print(f"  bbox px  x:{x0}..{x1}  y:{y0}..{y1}   ({n} px)")
        print(f"  bbox pt  x:{x0/scale:.0f}..{x1/scale:.0f}  y:{y0/scale:.0f}..{y1/scale:.0f}")
        print(f"  size pt  {(x1-x0+1)/scale:.1f} x {(y1-y0+1)/scale:.1f}")

    # Individual block extents, to recover the cell size.
    print("\n-- column histogram of coloured pixels (in points)")
    cols = []
    for x in range(w):
        c = 0
        for y in range(0, h, 2):
            o = (y * w + x) * 3
            if is_block(px[o], px[o + 1], px[o + 2]):
                c += 1
        cols.append(c)
    runs = []
    inrun = False
    for x, c in enumerate(cols):
        if c > 0 and not inrun:
            start = x
            inrun = True
        elif c == 0 and inrun:
            runs.append((start, x - 1))
            inrun = False
    if inrun:
        runs.append((start, w - 1))
    print("  runs (points): " + ", ".join(
        f"{a/scale:.0f}..{b/scale:.0f}(w={(b-a+1)/scale:.1f})" for a, b in runs[:12]))

    rows = []
    for y in range(h):
        c = 0
        for x in range(0, w, 2):
            o = (y * w + x) * 3
            if is_block(px[o], px[o + 1], px[o + 2]):
                c += 1
        rows.append(c)
    rruns = []
    inrun = False
    for y, c in enumerate(rows):
        if c > 0 and not inrun:
            start = y
            inrun = True
        elif c == 0 and inrun:
            rruns.append((start, y - 1))
            inrun = False
    if inrun:
        rruns.append((start, h - 1))
    print("  row runs (points): " + ", ".join(
        f"{a/scale:.0f}..{b/scale:.0f}(h={(b-a+1)/scale:.1f})" for a, b in rruns[:12]))

    # Bright text.
    def is_bright(r, g, b):
        return r > 150 and g > 150 and b > 150

    print("\n-- bright (white) pixels")
    bright = bbox(w, h, px, is_bright)
    if bright:
        x0, y0, x1, y1, n = bright
        print(f"  bbox px  x:{x0}..{x1}  y:{y0}..{y1}   ({n} px)")
        print(f"  bbox pt  x:{x0/scale:.0f}..{x1/scale:.0f}  y:{y0/scale:.0f}..{y1/scale:.0f}")

    # Dim text (labels like NEXT).
    def is_dim(r, g, b):
        return 70 < r <= 150 and 70 < g <= 150 and 70 < b <= 150 and abs(r - g) < 25 and abs(g - b) < 25

    print("\n-- dim grey pixels (labels)")
    dim = bbox(w, h, px, is_dim)
    if dim:
        x0, y0, x1, y1, n = dim
        print(f"  bbox px  x:{x0}..{x1}  y:{y0}..{y1}   ({n} px)")
        print(f"  bbox pt  x:{x0/scale:.0f}..{x1/scale:.0f}  y:{y0/scale:.0f}..{y1/scale:.0f}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "screenshot.PNG")