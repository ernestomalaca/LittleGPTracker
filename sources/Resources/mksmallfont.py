#!/usr/bin/env python3
# trk2als: genera hdfont_small.h, letra chica de 8x12 para etiquetas y textos
# libres de los pinceles (DejaVu Sans Mono Bold, ver DEJAVU_LICENSE).
#
# USO: ./mksmallfont.py DejaVuSansMono-Bold.ttf hdfont_small.h
import sys
from PIL import Image, ImageDraw, ImageFont

ttf, out = sys.argv[1], sys.argv[2]
W, H, SIZE, BASELINE = 8, 12, 11, 10
f = ImageFont.truetype(ttf, SIZE)
asc, desc = f.getmetrics()

def glyph(c):
    if not (32 <= c <= 126):
        return [0] * (W * H)
    im = Image.new("L", (24, 24), 0)
    ImageDraw.Draw(im).text(((24 - f.getlength(chr(c))) / 2, 0), chr(c), font=f, fill=255)
    top = asc - BASELINE
    return list(im.crop((12 - W // 2, top, 12 + W // 2, top + H)).get_flattened_data())

with open(out, "w") as o:
    o.write("// Generado por mksmallfont.py - no editar a mano\n")
    o.write("#ifndef _HDFONT_SMALL_H_\n#define _HDFONT_SMALL_H_\n\n")
    o.write("#define SMALLFONT_W %d\n#define SMALLFONT_H %d\n\n" % (W, H))
    o.write("static const unsigned char hdfont_small[128][%d] = {\n" % (W * H))
    for c in range(128):
        o.write("  {" + ",".join(str(v) for v in glyph(c)) + "},\n")
    o.write("};\n\n#endif\n")
print("ok", out)
