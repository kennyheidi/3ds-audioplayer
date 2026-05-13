import struct, zlib

def make_png(w, h, color):
    def chunk(name, data):
        c = zlib.crc32(name + data) & 0xffffffff
        return struct.pack('>I', len(data)) + name + data + struct.pack('>I', c)
    r, g, b = color
    raw = b''
    for _ in range(h):
        raw += bytes([0]) + bytes([r, g, b] * w)
    compressed = zlib.compress(raw)
    png  = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', compressed)
    png += chunk(b'IEND', b'')
    return png

with open('icon.png', 'wb') as f:
    f.write(make_png(48, 48, (0x7C, 0x3A, 0xFF)))

print("icon.png created successfully")
