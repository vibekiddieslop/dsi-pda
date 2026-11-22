"""Writes icon.bmp: a 32x32, 16-colour BMP of a leather planner for the ROM banner.

Run: python tools/make_icon.py   (no third-party modules needed)
"""
import struct
import sys

PAL = [
    (255, 0, 255),    # 0 transparent (magenta)
    (0x5A, 0x3A, 0x22),  # 1 leather
    (0x80, 0x58, 0x38),  # 2 leather highlight
    (0xF4, 0xEC, 0xD0),  # 3 paper
    (0xB8, 0xC8, 0xE0),  # 4 rule
    (0xD8, 0x80, 0x80),  # 5 margin
    (0x28, 0x28, 0x38),  # 6 ink
    (0xE8, 0xC8, 0x70),  # 7 gold
    (0x38, 0x88, 0x40),  # 8 green tick
]
PAL += [(0, 0, 0)] * (16 - len(PAL))

W = H = 32
px = [[0] * W for _ in range(H)]


def rect(x0, y0, x1, y1, c):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px[y][x] = c


rect(3, 2, 28, 29, 1)          # cover
rect(4, 3, 27, 28, 2)
rect(5, 4, 27, 28, 1)
rect(8, 5, 26, 27, 3)          # page
for y in range(10, 27, 4):     # ruled lines
    rect(9, y, 25, y, 4)
rect(11, 5, 11, 27, 5)         # margin
for y in (7, 12, 17, 22):      # rings
    rect(5, y, 9, y + 1, 7)
# tick mark
for i, (x, y) in enumerate([(14, 15), (15, 16), (16, 17), (17, 16), (18, 15), (19, 14), (20, 13)]):
    px[y][x] = 8
    px[y + 1][x] = 8
rect(14, 21, 23, 21, 6)        # text line
rect(14, 9, 21, 9, 6)

row_bytes = W // 2
pixel_data = b""
for y in range(H - 1, -1, -1):  # BMP rows bottom-up
    row = bytes((px[y][x] << 4) | px[y][x + 1] for x in range(0, W, 2))
    pixel_data += row
palette = b"".join(struct.pack("<BBBB", b, g, r, 0) for (r, g, b) in PAL)
offset = 14 + 40 + len(palette)
header = struct.pack("<2sIHHI", b"BM", offset + len(pixel_data), 0, 0, offset)
dib = struct.pack("<IiiHHIIiiII", 40, W, H, 1, 4, 0, len(pixel_data), 2835, 2835, 16, 16)

out = sys.argv[1] if len(sys.argv) > 1 else "icon.bmp"
with open(out, "wb") as f:
    f.write(header + dib + palette + pixel_data)
print("wrote", out)
