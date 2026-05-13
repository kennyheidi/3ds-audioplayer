#!/usr/bin/env python3
"""
gen_banner.py <output.png>
Generates a 256x128 Home Menu banner PNG using only stdlib (no Pillow needed).
Replace banner/banner.png with your own 256x128 art at any time.
"""
import sys, zlib, struct

W, H = 256, 128

def chunk(tag, data):
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

def write_png(path, rows):
    raw = b""
    for row in rows:
        raw += b"\x00" + b"".join(bytes(p) for p in row)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))

# Gradient: top = deep purple #1E0A3C, bottom = vivid purple #7C3AFF
BG1 = (0x1E, 0x0A, 0x3C)
BG2 = (0x7C, 0x3A, 0xFF)

def lerp(a, b, t):
    return int(a + (b - a) * t)

rows = []
for r in range(H):
    t = r / (H - 1)
    rgb = (lerp(BG1[0], BG2[0], t),
           lerp(BG1[1], BG2[1], t),
           lerp(BG1[2], BG2[2], t))
    rows.append([rgb] * W)

# Simple text: "3DS AUDIO" centered, white pixels
TEXT = [
    "XXX  XXX  X  X      X  X  X  XXX  X  XXX",
    "X X   X   XX X      X  X  X  X  X  X  X  ",
    "XXX   X   X XX      XXXX  X  X  X  X  X  ",
    "X     X   X  X      X  X  X  X  X  X  X  ",
    "X    XXX  X  X      X  X  XXX  XXX  X  XXX",
]
CW, CH = 6, 7  # cell width, cell height with gap
TW = len(TEXT[0]) * CW
TX = (W - TW) // 2
TY = (H - CH * len(TEXT)) // 2

for ri, line in enumerate(TEXT):
    for ci, ch in enumerate(line):
        if ch == 'X':
            px_x = TX + ci * CW
            px_y = TY + ri * CH
            for dy in range(CH - 1):
                for dx in range(CW - 1):
                    ry = px_y + dy
                    rx = px_x + dx
                    if 0 <= ry < H and 0 <= rx < W:
                        rows[ry][rx] = (255, 255, 255)

out = sys.argv[1] if len(sys.argv) > 1 else "banner.png"
write_png(out, rows)
print(f"Written: {out}")
