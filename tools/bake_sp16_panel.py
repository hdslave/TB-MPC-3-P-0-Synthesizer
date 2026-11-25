"""Bake the panel's indexed artwork and antialiased type with Pillow."""
from pathlib import Path
from math import cos, pi, sin
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'src/sp16_graphics.h'
LANCZOS = Image.Resampling.LANCZOS


def nameplate():
    plate = Image.new('RGB', (780, 67), (8, 14, 21))
    # The wordmark is optically condensed to fit the instrument's nameplate.
    logo = Image.open(ROOT/'assets/art/slink.png').convert('RGB')
    logo = logo.resize((323, 50), LANCZOS)
    alpha = logo.convert('L').point(lambda n: min(255, n*8))
    plate.paste(logo, (13, 9), alpha)
    draw = ImageDraw.Draw(plate)
    draw.line((12, 66, 767, 66), fill=(51, 58, 65))
    # Bake the two titanium orbits at four times screen resolution.
    rings = Image.new('RGBA',(420,268))
    ring_draw = ImageDraw.Draw(rings)
    for rx,ry,angle,color in [(48,15,-.38,(75,101,133)),(28,26,.5,(45,66,87))]:
        points = []
        for i in range(161):
            t = 2*pi*i/160
            u,v = rx*cos(t),ry*sin(t)
            points.append((4*(52+u*cos(angle)-v*sin(angle)),4*(33+u*sin(angle)+v*cos(angle))))
        ring_draw.line(points,fill=color+(255,),width=3)
    rings = rings.resize((105,67),LANCZOS)
    plate.paste(rings,(402,0),rings)
    return plate


def face():
    # The original Press Plate face. Restore its enclosed black features while
    # preserving the open spaces between the hanging drops.
    master = Image.open(ROOT/'assets/explore/press-plate/master.png').convert('RGB')
    box = tuple(round(v*master.width/660) for v in (412,0,609,220))
    source = master.crop(box)
    matte = Image.new('L',source.size)
    colors = zip(*(band.tobytes() for band in source.split()))
    matte.putdata([255 if (g>95 and r>65 and b<g*.7 and r<g*1.45)
                   or (r>125 and b>90 and g<r*.54) else 0 for r,g,b in colors])
    matte = matte.filter(ImageFilter.MaxFilter(7)).filter(ImageFilter.MinFilter(7))
    outside = matte.copy()
    ImageDraw.floodfill(outside,(0,0),128)
    matte = outside.point(lambda v: 0 if v==128 else 255)
    matte = matte.filter(ImageFilter.MaxFilter(9))
    # Map the original eyes and grin to acid enamel. A filled silhouette of
    # the yellow face keeps the dark perimeter and the gaps between drips clean.
    size = (165,183)
    source = source.resize(size,LANCZOS)
    matte = matte.resize(size,LANCZOS)
    green = source.getchannel('G')
    body = green.point(lambda g: 255 if g > 120 else 0)
    body = body.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.MinFilter(3))
    ImageDraw.floodfill(body,(0,0),128)
    body = body.point(lambda n: 0 if n == 128 else 255)
    rim = body.filter(ImageFilter.GaussianBlur(3))
    metal = Image.new('RGBA',size)
    # Broad reflection bands survive the final reduction to 55 pixels.
    stops = [(0,62),(.16,231),(.25,113),(.37,71),(.49,39),(.59,54),
             (.69,100),(.78,185),(.87,76),(1,36)]
    pixels = []
    for y in range(size[1]):
        for x in range(size[0]):
            pos = (x,y)
            u, v = x/(size[0]-1), y/(size[1]-1)
            t = max(0,min(1, v*.83 + u*.12 + (u-.45)**2*.42))
            for (t0,a),(t1,b) in zip(stops,stops[1:]):
                if t <= t1:
                    shade = a + (b-a)*(t-t0)/(t1-t0)
                    break
            if body.getpixel(pos):
                shade += (255-rim.getpixel(pos))*.80
                ink = max(0,min(1,(130-green.getpixel(pos))/96))
            else:
                shade = 43 + 115*(1-u)
                ink = 0
            rgb = (shade*.80, shade*.91, shade*1.08+6)
            enamel = (194,255,48)
            pixels.append(tuple(round(min(255,c*(1-ink)+e*ink)) for c,e in zip(rgb,enamel))
                          + (matte.getpixel(pos),))
    metal.putdata(pixels)
    return metal.resize((55,61),LANCZOS)


def sprites(header):
    icon = face()
    frames = []
    for strength in (0, .55, 1, .72, .38, .15):
        tile = header.crop((421,0,489,67))
        # The face bobs on the accent. Its drips travel farther than its eyes.
        stamp = icon.resize((55+round(3*strength),61-round(3*strength)),LANCZOS)
        for y in range(stamp.height):
            tail = max(0,(y/stamp.height-.52)/.48)
            dx = round(strength*tail*5*sin(y*.42))
            dy = y + int(tail*strength*5)
            line = stamp.crop((0,y,stamp.width,y+1))
            tile.paste(line,(5+dx,1+dy),line)
            if tail and strength:
                tile.paste(line,(5+dx,2+dy),line)
        frames.append(tile)
    return frames


def array(out, kind, name, values, columns=36):
    out.write(f'constexpr {kind} {name}[] = {{\n')
    for i in range(0,len(values),columns):
        out.write('    '+','.join(map(str,values[i:i+columns]))+',\n')
    out.write('};\n')


def bake():
    header = nameplate()
    frames = sprites(header)
    atlas = Image.new('RGB',(780,67+67))
    atlas.paste(header)
    for i,tile in enumerate(frames):
        atlas.paste(tile,(68*i,67))
    indexed = atlas.quantize(colors=256,dither=Image.Dither.NONE)
    with OUT.open('w') as out:
        out.write('// Generated by tools/bake_sp16_panel.py\n#pragma once\nnamespace tbmpc::rave {\n')
        pal = indexed.getpalette()
        array(out,'unsigned','palette',['0xff%02x%02x%02x'%tuple(pal[i:i+3]) for i in range(0,len(pal),3)],8)
        array(out,'unsigned char','header',indexed.crop((0,0,780,67)).tobytes())
        data = b''.join(indexed.crop((68*i,67,68*(i+1),134)).tobytes() for i in range(len(frames)))
        array(out,'unsigned char','faces',data)
        out.write('struct Font { const unsigned char* ink; const unsigned char* widths; int height, stride; };\n')
        for name,size,h in [('small',12,15),('normal',16,20),('large',20,25)]:
            font = ImageFont.truetype(str(ROOT/'assets/fonts/Barlow-SemiBold.ttf'),size)
            glyphs, advances = [], []
            for code in range(32,127):
                mask = Image.new('L',(24,h))
                ImageDraw.Draw(mask).text((0,size),chr(code),fill=255,font=font,anchor='ls')
                advances.append(round(font.getlength(chr(code))))
                row = list(mask.tobytes())
                glyphs.extend((row[i]&0xf0) | (row[i+1]>>4) for i in range(0,len(row),2))
            array(out,'unsigned char',name+'Ink',glyphs)
            array(out,'unsigned char',name+'Advance',advances)
            out.write(f'constexpr Font {name} {{{name}Ink,{name}Advance,{h},12}};\n')
        out.write('}\n')


if __name__ == '__main__':
    bake()
