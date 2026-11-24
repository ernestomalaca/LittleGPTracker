#!/usr/bin/env python3
# trk2als: genera una fuente para Chancho (chancho6/fuentes/<nombre>.fnt), que se
# elige en Proyecto > FUENTE. El archivo trae las dos letras que se dibujan con
# la fuente: la de 16x16 (256 caracteres Latin-1) y la chica de 8x12.
# Los caracteres 0-31 y 127 (iconos del Piggy) salen de la fuente original de 8x8.
#
# Formato: "CHF1" + 256*256 bytes (16x16) + 256*96 bytes (8x12), cada byte 0..255.
#
# USO: ./mkfnt.py original.bmp fuente.ttf salida.fnt [opciones]
#   --size16 N --stretch F --base16 N   la de 16x16 (por omisión 18, 1.15, 13)
#   --size8 N --base8 N --font8 otra.ttf  la chica (por omisión 11, 10)
#   --pixel                             sin suavizado (fuentes de píxeles)
#   --var NOMBRE                        estilo de una fuente variable (p. ej. Bold)
#   --prev archivo.png                  además, una muestra
import sys, argparse
from PIL import Image, ImageDraw, ImageFont

ap = argparse.ArgumentParser()
ap.add_argument("orig"); ap.add_argument("ttf"); ap.add_argument("out")
ap.add_argument("--size16", type=float, default=18); ap.add_argument("--stretch", type=float, default=1.15)
ap.add_argument("--base16", type=int, default=13)
ap.add_argument("--size8", type=float, default=11); ap.add_argument("--base8", type=int, default=10)
ap.add_argument("--stretch8", type=float, default=1.0)
ap.add_argument("--font8"); ap.add_argument("--pixel", action="store_true")
ap.add_argument("--var"); ap.add_argument("--prev")
a = ap.parse_args()

def load(path, size):
    f = ImageFont.truetype(path, size)
    if a.var:
        try: f.set_variation_by_name(a.var)
        except Exception: pass
    return f

def make(font, W, H, base, stretch):
    def g(c):
        B = 64; cw = W * 3
        im = Image.new("L", (cw, 128), 0)
        d = ImageDraw.Draw(im)
        if a.pixel: d.fontmode = "1"
        d.text((cw / 2, B), chr(c), font=font, fill=255, anchor="ms")
        if stretch != 1.0:
            im = im.resize((int(round(cw * stretch)), 128), Image.NEAREST if a.pixel else Image.LANCZOS)
        cx = im.width // 2
        im = im.crop((cx - W // 2, 0, cx - W // 2 + W, 128))
        top = B - base
        bb = im.getbbox()
        if bb and bb[1] < top:   # lo de arriba (una tilde) no cabe: se aprieta
            upper = im.crop((0, bb[1], W, B)).resize((W, base), Image.NEAREST if a.pixel else Image.LANCZOS)
            lower = im.crop((0, B, W, B + H - base))
            cell = Image.new("L", (W, H), 0); cell.paste(upper, (0, 0)); cell.paste(lower, (0, base))
        else:
            cell = im.crop((0, top, W, top + H))
        px = list(cell.get_flattened_data())
        if c == ord("_") and sum(px) < 255 * 3:   # el guion bajo cae fuera: se dibuja
            for x in range(1, W - 1): px[(H - 2) * W + x] = 230
        return px
    return g

bmp = Image.open(a.orig).convert("RGB").load()
def icon16(c):
    cx, cy = (c % 16) * 8, (c // 16) * 8
    return [255 if bmp[cx + x // 2, cy + y // 2][0] == 0 else 0 for y in range(16) for x in range(16)]

g16 = make(load(a.ttf, a.size16), 16, 16, a.base16, a.stretch)
g8 = make(load(a.font8 or a.ttf, a.size8), 8, 12, a.base8, a.stretch8)
def printable(c): return 32 <= c <= 126 or 161 <= c <= 255
big = [icon16(c) if c < 32 or c == 127 else g16(c) if printable(c) else [0] * 256 for c in range(256)]
small = [g8(c) if printable(c) else [0] * 96 for c in range(256)]
with open(a.out, "wb") as o:
    o.write(b"CHF1")
    for px in big: o.write(bytes(px))
    for px in small: o.write(bytes(px))
if a.prev:
    s = "A0B 01 1l Il O0 Melodia_ch ¿Compás? ñ"
    pv = Image.new("L", (len(s) * 16, 16 + 4 + 12), 0)
    for i, ch in enumerate(s):
        im = Image.new("L", (16, 16)); im.putdata(big[ord(ch)]); pv.paste(im, (i * 16, 0))
        im = Image.new("L", (8, 12)); im.putdata(small[ord(ch)]); pv.paste(im, (i * 8, 20))
    pv.resize((pv.width * 2, pv.height * 2), Image.NEAREST).save(a.prev)
print("ok", a.out)
