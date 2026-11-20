import sys
# trk2als: agrega a hdfont.h, hdfont_small.h y hdfont32.h los caracteres 161-255
# (Latin-1: tildes, ñ, ¿, ¡, ü...). Las mayúsculas con tilde se aprietan para
# que quepan en la celda. USO: ./mkacentos.py JetBrainsMono-ExtraBold.ttf write <carpeta Resources>
from PIL import Image, ImageDraw, ImageFont
DJ="/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
JB=sys.argv[1]
def make(font, W, H, base, stretch=1.0):
    def g(c):
        B=64; cw=W*3
        im=Image.new("L",(cw,128),0)
        ImageDraw.Draw(im).text((cw/2,B),chr(c),font=font,fill=255,anchor="ms")
        if stretch!=1.0: im=im.resize((int(cw*stretch),128),Image.LANCZOS)
        cx=im.width//2
        im=im.crop((cx-W//2,0,cx-W//2+W,128))
        top=B-base                     # primera fila de la celda en el lienzo
        bb=im.getbbox()
        if bb and bb[1]<top:           # lo de arriba (la tilde) no cabe: se aprieta
            upper=im.crop((0,bb[1],W,B)).resize((W,base),Image.LANCZOS)
            lower=im.crop((0,B,W,B+H-base))
            cell=Image.new("L",(W,H),0); cell.paste(upper,(0,0)); cell.paste(lower,(0,base))
        else:
            cell=im.crop((0,top,W,top+H))
        return list(cell.get_flattened_data())
    return g
g16=make(ImageFont.truetype(DJ,18),16,16,13,1.15)
g8=make(ImageFont.truetype(DJ,11),8,12,10)
g32=make(ImageFont.truetype(JB,36),32,32,28)
if sys.argv[2]=="prev":
    s="áéíóúñüÁÉÍÓÚÑÜ¿¡°ExCa"
    prev=Image.new("L",(len(s)*34,3*36),0)
    for r,(g,w,h) in enumerate([(g16,16,16),(g8,8,12),(g32,32,32)]):
        for i,ch in enumerate(s):
            im=Image.new("L",(w,h)); im.putdata(g(ord(ch))); prev.paste(im,(i*34,r*36))
    prev.resize((prev.width*2,prev.height*2),Image.NEAREST).save(sys.argv[3]); sys.exit()
# extender los .h: las 128 de siempre se quedan, se agregan 128..255
import re
def extend(path,name,g,n):
    s=open(path).read()
    rows=re.findall(r"  \{[^}]*\},",s)
    assert len(rows)==128,(path,len(rows))
    new=[]
    for c in range(128,256):
        px=g(c) if c>=161 else [0]*n
        new.append("  {"+",".join(map(str,px))+"},")
    s=s.replace("%s[128]"%name,"%s[256]"%name)
    # después de la fila 127 (no de la primera fila igual a ella)
    end=s.index("};",s.index(rows[-1],sum(len(r) for r in rows[:-1])))
    s=s[:end]+"\n".join(new)+"\n"+s[end:]
    open(path,"w").write(s)
R=sys.argv[3]
extend(R+"/hdfont.h","hdfont",g16,256)
extend(R+"/hdfont_small.h","hdfont_small",g8,96)
extend(R+"/hdfont32.h","hdfont32",g32,1024)
print("ok")
