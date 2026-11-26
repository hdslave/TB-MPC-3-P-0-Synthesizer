"""Bake Slink's original three animated prints. Requires NumPy, Pillow and SciPy."""

import argparse
import math
import struct
import zlib
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import distance_transform_edt, gaussian_filter, shift

WIDTH, HEIGHT = 1088, 56
CUTS = (458, 776)
FRAMES, FPS = 120, 24
SCALE = 3
TAU = math.tau
DEST = Path(__file__).resolve().parents[1] / 'assets' / 'footer-print.bank'
PAPER = (8, 8, 18)
RESAMPLE = Image.Resampling.LANCZOS


def grid(width):
    y, x = np.ogrid[:HEIGHT*SCALE, :width*SCALE]
    return (x + .5) / SCALE, (y + .5) / SCALE


def contour(draw, start, curves, ink):
    """A closed pen contour, drawn large enough to retain its finest terminals."""
    points = [np.array(start, dtype=float)]
    t = np.linspace(0, 1, 31)[1:, None]
    for x1, y1, x2, y2, x3, y3 in curves:
        a = points[-1]
        b, c, d = (x1, y1), (x2, y2), (x3, y3)
        points.extend((1-t)**3*a + 3*(1-t)**2*t*b + 3*(1-t)*t**2*c + t**3*d)
    draw.polygon([tuple(p*SCALE) for p in points], fill=ink)


def engraved_metal():
    """Forge the raised faces from the signed distance to the hand-drawn cuts."""
    width = 458
    mask = Image.new('L', (width*SCALE, HEIGHT*SCALE))
    pen = ImageDraw.Draw(mask)
    # Two unequal hooked castings reach toward a pierced four-point jewel.
    contour(pen, (5,35), [
        (55,32,91,26,116,13), (134,4,140,9,155,2),
        (142,12,128,16,128,23), (128,30,151,20,173,8),
        (180,5,188,4,197,2), (182,12,183,22,172,29),
        (160,37,148,33,133,38), (119,42,106,49,96,53),
        (106,41,114,33,103,32), (80,31,51,36,5,35)], 255)
    contour(pen, (103,28), [(114,23,121,18,131,15), (119,24,126,27,135,27),
                            (124,31,110,29,103,28)], 0)
    contour(pen, (145,30), [(160,26,169,20,177,15), (172,25,163,31,145,30)], 0)
    contour(pen, (158,34), [
        (193,28,208,25,217,18), (224,13,228,6,234,0),
        (231,12,228,18,235,21), (245,25,263,22,286,18),
        (265,27,247,28,236,34), (229,39,225,48,220,55),
        (219,39,221,34,212,32), (196,29,181,35,158,34)], 255)
    contour(pen, (215,28), [(222,26,226,23,230,18), (228,25,231,27,236,27),
                            (229,30,226,32,222,36), (222,31,220,29,215,28)], 0)
    contour(pen, (252,45), [
        (270,32,274,18,281,11), (288,6,298,7,305,3),
        (293,15,290,22,298,23), (310,27,335,15,346,7),
        (355,1,363,4,377,1), (362,10,350,17,352,22),
        (355,30,387,22,408,17), (422,15,436,16,453,13),
        (425,22,398,25,375,29), (359,32,345,45,335,53),
        (340,41,344,33,331,33), (316,33,300,36,281,48),
        (289,38,289,34,283,34), (275,33,264,40,252,45)], 255)
    contour(pen, (298,30), [(307,29,321,25,333,19), (329,26,333,29,339,29),
                            (325,32,309,29,298,30)], 0)
    image = np.asarray(mask) > 127
    distance = distance_transform_edt(image) / SCALE
    # A steep polished bevel gives way to flatter facets at the spine of each stroke.
    height = gaussian_filter((np.sqrt(distance+.12)-math.sqrt(.12))*3.5, 3.6)
    ny, nx = np.gradient(height, 1/SCALE)
    length = np.sqrt(1 + nx*nx + ny*ny)
    nx, ny, nz = -nx/length, -ny/length, 1/length
    field = np.stack((2*nz*nx, 2*nz*ny, 2*nz*nz-1), axis=-1)
    return image, field


def chrome_print(casting, phase):
    shape, normals = casting
    x, _ = grid(458)
    rx, ry, rz = np.moveaxis(normals, -1, 0)
    # Narrow white strip lights, a cobalt card and a violet reflection move around the casting.
    angle = .55*np.sin(phase) + .20
    reflected = ry*np.cos(angle) + rx*np.sin(angle) + .11*rz + .16*np.sin(x/52 - phase)
    levels = [-1.5,-.92,-.75,-.65,-.55,-.38,-.26,-.15,-.05,.07,.17,.23,.37,.49,.54,.65,.79,1.5]
    cards = [(53,66,80),(127,149,174),(234,239,236),(117,137,163),(25,30,45),
             (8,12,25),(69,62,108),(141,129,173),(45,53,75),(11,15,24),
             (54,68,88),(152,181,200),(243,249,245),(203,224,235),(62,91,127),
             (25,36,64),(135,146,157),(214,224,218)]
    rgb = np.stack([np.interp(reflected, levels, [c[i] for c in cards]) for i in range(3)], axis=-1)
    # A few millimetres of dark turned edge separate the front from the negative space.
    dy, dx = 1.15 + .65*np.cos(phase), 2*np.sin(phase-.5)
    edge = shift(shape, (round(dy*SCALE),round(dx*SCALE)), order=0, mode='constant')
    flank = np.where((rz>.1)[...,None], (48,58,85), (73,67,102))
    ink = np.where(shape[...,None], rgb, np.where(edge[...,None],flank,PAPER))
    im = Image.fromarray(ink.astype(np.uint8))
    # The three linked pieces cant at different points along their shared axis.
    def extent(at):
        u = at/im.width
        stretch = 1 + .10*np.sin(TAU*u-phase)
        offset = SCALE*1.65*np.sin(TAU*u+phase)*np.sin(np.pi*u)
        return ((1-stretch)*im.height/2+offset, (1+stretch)*im.height/2+offset)
    mesh = []
    for left in range(0,im.width,24*SCALE):
        right = min(im.width,left+24*SCALE)
        top0,bottom0 = extent(left)
        top1,bottom1 = extent(right)
        mesh.append(((left,0,right,im.height),(left,top0,left,bottom0,right,bottom1,right,top1)))
    return im.transform(im.size,Image.Transform.MESH,mesh,Image.Resampling.BILINEAR,fillcolor=PAPER)


def checker_print(phase):
    width = 318
    x, y = grid(width)
    # A two-sided printed strip twists about its long axis. Inverting the projection
    # gives constant-sized squares on the paper and sharp perspective at each fold.
    u = (x-7)/(width-14)
    twist = .33 + .77*TAU*u + .38*np.sin(phase) + .25*np.sin(u*TAU-phase)
    bow = 29 - 8*np.sin(TAU*u+.55) + 2.0*np.sin(phase+3*u)
    half = (13+6*np.sin(np.pi*np.clip(u,0,1))**2)*np.sin(twist)
    v = (y-bow)/np.where(np.abs(half)>.08,half,.08)
    edge = (u>=0) & (u<=1) & (np.abs(v)<1)
    a = 17*u + .48*v*np.cos(twist) + .16*np.sin(phase)
    b = (v+1)*1.5
    checks = ((np.floor(a).astype(int)+np.floor(b).astype(int)) & 1).astype(bool)
    face = (.48+.52*np.abs(np.sin(twist))) * np.where(half>0,1,.78)
    pearl = np.array([222,220,207]) * face[...,None]
    black = np.array([20,18,31]) + np.minimum(np.abs(v),1)[...,None]*[6,6,8]
    color = np.where(checks[...,None], pearl, black)
    hem = np.clip((np.abs(v)-.93)/.07,0,1)[...,None]
    color = color*(1-hem) + hem*np.array([114,120,143])
    rgb = np.where(edge[...,None],color,PAPER)
    # A single lacquer bead travels above and below the fold.
    cx, cy = 162 + 25*np.sin(phase), 26 - 6*np.cos(phase)
    sx, sy = (x-cx)/10.9, (y-cy)/10.9
    rr = sx*sx + sy*sy
    sz = np.sqrt(np.maximum(0,1-rr))
    shadow = np.exp(-(((x-cx-3)/13)**2 + ((y-cy-8)/5.5)**2))
    rgb = rgb * (1 - .47*(shadow*edge)[...,None])
    shade = np.clip(.32 - .23*sx - .56*sy + .47*sz,0,1)
    bead = np.array([52,12,40]) + shade[...,None]*[172,23,22]
    sheen = np.exp(-((sx+.29)**2/.050 + (sy+.52)**2/.015))
    bead += sheen[...,None]*[42,137,142]
    ribbon_depth = v*17*np.cos(twist)
    behind_fold = edge & (ribbon_depth > 10*np.cos(phase)+10.9*sz)
    rgb = np.where(((rr<1) & ~behind_fold)[...,None],bead,rgb)
    return Image.fromarray(np.uint8(np.clip(rgb,0,255)))


def stencil_print(phase):
    width = 312
    x, y = grid(width)
    # A measured field of elliptical cuts, torn open along a single oblique axis.
    xx = (x-155)/127
    yy = (y-28)/16.7
    bend = .25*np.sin(phase+3.3*xx) + .47*np.sin(5.8*xx-.62*np.sin(phase))
    v = yy-bend
    capsule = (np.abs(xx)**3.0 + np.abs(v*.98)**3.0) < 1
    groove = 1.18*xx + .24*v + .20*np.sin(3.2*xx + 1.6*v + phase) + .085*np.sin(7*xx - 2.3*v - phase)
    slits = np.cos(TAU*(5.3*groove - .20*np.sin(phase))) > -.04
    edge = capsule & slits & (np.abs(.80*xx+.18*v-.12)>.024)
    violet = np.array([143,104,241])
    acid = np.array([192,237,38])
    blue = np.array([72,146,233])
    # Cool edges and a small area of concentrated fluorescent yellow-green.
    offset = .12*np.cos(phase)
    emerald = np.clip((xx+.87+offset)*1.8,0,1)
    indigo = np.clip((xx-.32+offset)*2.6,0,1)
    color = blue*(1-emerald[...,None]) + acid*emerald[...,None]
    color = color*(1-indigo[...,None]) + violet*indigo[...,None]
    # Quiet bent registration grid between the solid strokes, echoed by the header glass.
    grid_x = np.abs((x/12 + .13*np.sin(y/9+phase) + .5)%1-.5)
    grid_y = np.abs((y/8 + .25*np.sin(x/42-phase) + .5)%1-.5)
    mesh = ((grid_x<.031) | (grid_y<.044)) & (np.abs(xx)<1.11) & (np.abs(yy)<1.15)
    rgb = np.where(mesh[...,None],(35,40,48),PAPER)
    rgb = np.where(edge[...,None],color,rgb)
    return Image.fromarray(np.uint8(np.clip(rgb,0,255)))


def compose(casting, frame):
    phase = TAU * (frame % FRAMES) / FRAMES
    plate = Image.new('RGB', (WIDTH*SCALE, HEIGHT*SCALE), PAPER)
    plate.paste(chrome_print(casting, phase), (0,0))
    plate.paste(checker_print(phase), (458*SCALE,0))
    plate.paste(stencil_print(phase), (776*SCALE,0))
    # An interrupted baseline ties all three prints together.
    pen = ImageDraw.Draw(plate)
    def stroke(x1,y1,x2,y2,ink,weight=.7):
        pen.line((x1*SCALE,y1*SCALE,x2*SCALE,y2*SCALE), fill=ink, width=max(1,round(weight*SCALE)))
    for start, end in ((1,58),(402,446),(471,503),(733,764),(788,815),(1039,1086)):
        stroke(start,52,end,52,(63,61,84))
    return plate.resize((WIDTH,HEIGHT),RESAMPLE)


def bake(images):
    # A single palette across all poses avoids flicker. Each frame is independently
    # compressed, so opening an editor and seeking a frame only needs one small buffer.
    sample = Image.new('RGB', (WIDTH, HEIGHT*12))
    for n, frame in enumerate(images[::FRAMES//12]):
        sample.paste(frame, (0,n*HEIGHT))
    palette = sample.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    colors = np.array(palette.getpalette(), dtype=np.uint8).reshape((-1,3))
    bgra = np.full((256,4),255,dtype=np.uint8)
    bgra[:len(colors),:3] = colors[:,::-1]
    packets = []
    header = struct.pack('<4s6H',b'SFP1',WIDTH,HEIGHT,FRAMES,FPS,*CUTS)
    offsets = [len(header) + bgra.nbytes + 4*(FRAMES+1)]
    for frame in images:
        pixels = frame.quantize(palette=palette,dither=Image.Dither.NONE).tobytes()
        packets.append(zlib.compress(pixels,9))
        offsets.append(offsets[-1] + len(packets[-1]))
    DEST.write_bytes(header + bgra.tobytes()
                     + struct.pack(f'<{len(offsets)}I',*offsets) + b''.join(packets))
    print(f'{DEST.name}: {FRAMES} frames, {offsets[-1]:,} bytes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--proof', type=Path, help='Render a four-pose contact sheet instead of baking the bank')
    args = parser.parse_args()
    casting = engraved_metal()
    if args.proof:
        sheet = Image.new('RGB', (WIDTH,HEIGHT*4))
        for row in range(4):
            sheet.paste(compose(casting,row*FRAMES//4), (0,row*HEIGHT))
        sheet.save(args.proof)
    else:
        bake([compose(casting,i) for i in range(FRAMES)])
