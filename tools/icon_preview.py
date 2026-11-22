#!/usr/bin/env python3
"""Renders the ASCII icons in source/ui/icons.c to a PNG contact sheet.

  python3 tools/icon_preview.py out.png

Also reports rows that are not exactly 16 characters. Needs only the
standard library.
"""
import re
import struct
import sys
import zlib
from pathlib import Path

COLORS = {
    '#': 0x282838, 'w': 0xFFFFFF, 'r': 0xC03030, 'b': 0x3060A0, 'B': 0x5B97E0,
    'g': 0x388840, 'G': 0x6CBF5A, 'y': 0xE8C870, 'Y': 0xF2C744, 'o': 0xF29A3E,
    'k': 0x5A3A22, 'K': 0x805838, 's': 0x707078, 'e': 0xD8D4C8, 't': 0x3DBFAE,
    'm': 0xD88080,
}
BG = 0xF4ECD0
SCALE = 6
PER_ROW = 8
CELL = 16 * SCALE + 12


def write_png(path, width, height, pixels):
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            v = pixels[y * width + x]
            raw += bytes(((v >> 16) & 255, (v >> 8) & 255, v & 255))

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    Path(path).write_bytes(png)


def main():
    root = Path(__file__).resolve().parent.parent
    text = (root / "source/ui/icons.c").read_text()
    icons = []
    for m in re.finditer(r"\[(ICON_\w+)\]\s*=\s*\{(.*?)\},\n", text, re.S):
        rows = re.findall(r'"([^"]*)"', m.group(2))
        icons.append((m.group(1), rows))
        if len(rows) != 16:
            print(f"{m.group(1)}: {len(rows)} rows")
        for i, row in enumerate(rows):
            if len(row) != 16:
                print(f"{m.group(1)} row {i}: {len(row)} chars")
            for ch in row:
                if ch != '.' and ch not in COLORS:
                    print(f"{m.group(1)} row {i}: unknown color '{ch}'")
    cols = PER_ROW
    lines = (len(icons) + cols - 1) // cols
    width, height = cols * CELL, lines * CELL
    pixels = [BG] * (width * height)
    for n, (name, rows) in enumerate(icons):
        ox = (n % cols) * CELL + 6
        oy = (n // cols) * CELL + 6
        for r, row in enumerate(rows[:16]):
            for c, ch in enumerate(row[:16]):
                if ch not in COLORS:
                    continue
                for dy in range(SCALE):
                    base = (oy + r * SCALE + dy) * width + ox + c * SCALE
                    for dx in range(SCALE):
                        pixels[base + dx] = COLORS[ch]
    out = sys.argv[1] if len(sys.argv) > 1 else "icons.png"
    write_png(out, width, height, pixels)
    print(f"{len(icons)} icons -> {out}")
    for n, (name, _) in enumerate(icons):
        print(f"  {n // cols},{n % cols} {name}")


if __name__ == "__main__":
    main()
