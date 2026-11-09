#!/usr/bin/env python3
# trk2als: genera hdfont.h, la fuente nítida de 16x16 para pantallas de 640x480.
# Caracteres 32-126: DejaVu Sans Mono Bold (licencia libre de Bitstream Vera /
# DejaVu, ver DEJAVU_LICENSE), un poco ensanchada para llenar la celda cuadrada.
# Caracteres 0-31 y 127 (iconos del Piggy): la fuente original de 8x8 al doble.
#
# USO: ./mkhdfont.py original.bmp DejaVuSansMono-Bold.ttf hdfont.h
import sys
from PIL import Image, ImageDraw, ImageFont

orig, ttf, out = sys.argv[1], sys.argv[2], sys.argv[3]
SIZE, STRETCH, BASELINE = 18, 1.15, 13

f = ImageFont.truetype(ttf, SIZE)
asc, desc = f.getmetrics()
bmp = Image.open(orig).convert("RGB").load()

def ttf_glyph(c):
    W = 40                                     # lienzo; el caracter va centrado por su ancho
    im = Image.new("L", (W, 32), 0)
    ImageDraw.Draw(im).text(((W - f.getlength(chr(c))) / 2, 0), chr(c), font=f, fill=255)
    im = im.resize((int(W * STRETCH), 32), Image.LANCZOS)
    cx, top = im.width // 2, asc - BASELINE   # línea base en la fila 13 de 16
    return list(im.crop((cx - 8, top, cx + 8, top + 16)).get_flattened_data())

def orig_glyph(c):
    cx, cy = (c % 16) * 8, (c // 16) * 8
    px = []
    for y in range(16):
        for x in range(16):
            # en la fuente original el negro es la tinta
            px.append(255 if bmp[cx + x // 2, cy + y // 2][0] == 0 else 0)
    return px

with open(out, "w") as o:
    o.write("// Generado por mkhdfont.py - no editar a mano\n")
    o.write("#ifndef _HDFONT_H_\n#define _HDFONT_H_\n\n")
    o.write("#define HDFONT_SIZE 16\n\n")
    o.write("static const unsigned char hdfont[128][256] = {\n")
    for c in range(128):
        g = ttf_glyph(c) if 32 <= c <= 126 else orig_glyph(c)
        o.write("  {" + ",".join(str(v) for v in g) + "},\n")
    o.write("};\n\n#endif\n")
print("ok", out)
