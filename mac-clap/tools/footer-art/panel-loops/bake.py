"""Rebuild the three reusable panel loops at their original size and timing."""

import hashlib
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import gaussian_filter

HERE = Path(__file__).resolve().parent
HEIGHT, SCALE = 56, 3
FRAMES, FPS = 120, 24
SILVER_WIDTH, CHECKER_WIDTH, STENCIL_WIDTH = 458, 318, 312
ART = HERE.parent
RESAMPLE = Image.Resampling.LANCZOS
PAPER = (8, 8, 18)
TAU = math.tau


def grid(width):
    y, x = np.ogrid[:HEIGHT*SCALE, :width*SCALE]
    return (x + .5) / SCALE, (y + .5) / SCALE


def engraved_metal():
    """Prepare the cutout, the local bevel directions and the reflection field once."""
    with Image.open(ART / 'silver-engraving.png') as master:
        bounds = master.getchannel('A').point(lambda a: 255 if a > 96 else 0).getbbox()
        relief = master.crop(bounds).resize((416*SCALE, 50*SCALE), RESAMPLE)
    plate = Image.new('RGBA', (SILVER_WIDTH*SCALE, HEIGHT*SCALE))
    plate.paste(relief, (21*SCALE, 3*SCALE))
    rgba = np.asarray(plate)
    alpha = np.clip((rgba[..., 3].astype(float)-52) / 184, 0, 1)
    metal = rgba[..., :3].astype(float)
    lightness = metal @ np.array([.23, .69, .08]) / 255
    # The blacks in the aperture and the engraved cuts also turn the reflection.
    dy, dx = np.gradient(gaussian_filter(lightness*alpha, .85*SCALE), 1/SCALE)
    across = np.tanh(dx*6)
    along = np.tanh(dy*6)
    reflection = .82*lightness + .20*along - .11*across
    return metal, np.uint8(255*alpha), reflection, along, across


def draw_wire(pen, phase, near):
    # The fine black-nickel bail threads the opening. Its near and far arcs
    # share their terminals, but only the near wire crosses the polished face.
    theta = np.linspace(0, np.pi, 151) + (0 if near else np.pi)
    side, depth = np.cos(theta), np.sin(theta)
    cant = .43 + .27*np.sin(phase-.45)
    x = 241 + 77*side + 4.0*depth*np.sin(phase)
    y = 27 + 8.7*side + 20.0*cant*depth
    gleam = (.5+.5*np.cos(theta-1.0+.72*np.sin(phase)))**5
    nickel = np.array([44, 46, 67]) + gleam[:,None]*[116, 118, 123]
    # One crimp of turned silver fixes the wire at the mouth of the eye.
    collar = np.exp(-((theta-1.32)/.064)**4)
    nickel += (collar*(.4+.6*np.cos((theta-1.32)*65)**2))[:,None]*[87, 85, 90]
    for j in range(len(theta)-1):
        pen.line((x[j]*SCALE, y[j]*SCALE, x[j+1]*SCALE, y[j+1]*SCALE),
                 fill=tuple(nickel[j].astype(int)), width=round(2+int(near)+4*collar[j]))


def chrome_print(casting, phase):
    metal, alpha, reflection, along, across = casting
    width = SILVER_WIDTH
    x, _ = grid(width)
    # Relight each rolled face in its own direction. The darker counterstroke
    # picks up the blue card while the two white cards cross the silver bevels.
    turn = np.sin(phase) * (.16+.10*along) + .10*np.cos(phase)*across
    turn += .054*np.sin(phase + 3.5*x/width)
    bright = np.exp(-((reflection+turn-.66)/.108)**2)
    base = np.exp(-((reflection-.66)/.108)**2)
    blue = np.exp(-((reflection-.26-.65*turn)/.10)**2)
    color = metal + (bright-base)[...,None]*[74, 71, 65]
    color += (blue-.25)[...,None]*[1, 5, 19]
    face = Image.fromarray(np.dstack((np.uint8(np.clip(color,0,255)), alpha)))
    # A shallow roll along the long axis opens and closes the negative space.
    # Keep the two needle tips seated while the thick part of the casting turns.
    def ends(at):
        u = at/face.width
        weight = np.sin(np.pi*u)
        height = 1 + weight*.13*np.sin(phase+3.4*u)
        lift = SCALE*2.0*weight*np.sin(phase-2.8*u)
        skew = SCALE*3.2*weight*np.cos(phase-.6)
        middle = face.height*.5
        return (at-skew, middle*(1-height)+lift, at+skew, middle*(1+height)+lift)

    mesh = []
    for left in range(0, face.width, 16*SCALE):
        right = min(face.width, left+16*SCALE)
        tlx,tly,blx,bly = ends(left)
        trx,try_,brx,bry = ends(right)
        mesh.append(((left,0,right,face.height),(tlx,tly,blx,bly,brx,bry,trx,try_)))
    face = face.transform(face.size,Image.Transform.MESH,mesh,Image.Resampling.BILINEAR)
    plate = Image.new('RGB', face.size, PAPER)
    draw_wire(ImageDraw.Draw(plate), phase, False)
    plate.paste(face, mask=face.getchannel('A'))
    draw_wire(ImageDraw.Draw(plate), phase, True)
    return plate


def paper_plane(rgb, seam_a, seam_b, columns, disc, phase):
    """Project one printed quadrilateral; the two UV axes follow its scored edges."""
    back_a, front_a = seam_a
    back_b, front_b = seam_b
    corners = (back_a, back_b, front_b, front_a)
    # A projective map keeps the runs of squares parallel to the receding paper.
    equations, target = [], []
    for (x,y), (u,v) in zip(corners, ((0,0),(1,0),(1,1),(0,1))):
        equations.extend(((x,y,1,0,0,0,-u*x,-u*y),(0,0,0,x,y,1,-v*x,-v*y)))
        target.extend((u,v))
    a,b,c,d,e,f,g,h = np.linalg.solve(equations, target)
    # Work only inside this leaf's bounds, including its antialiased edge.
    xs, ys = np.array(corners).T
    left, right = int(xs.min()*SCALE), int(xs.max()*SCALE)+1
    top, bottom = int(ys.min()*SCALE), int(ys.max()*SCALE)+1
    y,x = np.ogrid[top:bottom,left:right]
    x, y = (x+.5)/SCALE, (y+.5)/SCALE
    denominator = g*x+h*y+1
    u = (a*x+b*y+c)/denominator
    v = (d*x+e*y+f)/denominator
    inside = (u>=0) & (u<=1) & (v>=0) & (v<=1)
    u, v = np.clip(u,0,1), np.clip(v,0,1)
    if columns > 0:
        # Ivory printed face. Fold shadows belong to the paper, as does the
        # cast shadow of the solar disc hanging between the two near leaves.
        check = (np.floor(u*columns).astype(int) + np.floor(3*v).astype(int)) % 2
        light = .91 + .085*(1-v) - .19*np.exp(-u*9)
        light += .045*np.sin(phase+.4)*v
        sun_x, sun_y = disc
        shadow = np.exp(-(((x-sun_x-15)/20)**2+((y-sun_y-13)/7.8)**2))
        light -= .38*shadow
        ivory = np.array([235, 226, 210])*light[...,None]
        ink = np.array([20, 17, 27]) + (1-v)[...,None]*[16, 11, 14]
        color = np.where(check[...,None], ivory, ink)
        # A sliver of the paper's edge shows along the lower cut.
        rim = np.clip((v-.965)/.035,0,1)
        color = color*(1-rim[...,None]) + rim[...,None]*[136, 112, 121]
    else:
        # Intaglio on the reverse. Lines follow the scored return, with a fine
        # second edge in the same garnet ink as the disc.
        lines = -columns
        key = (.5+.5*np.cos(TAU*(lines*v+.28*np.sin(np.pi*u)-.36*u)))**4
        roll = .40+.60*(1-u)**.65
        ink = [109, 111, 134] if lines == 5 else [155, 97, 115]
        color = np.array([28, 21, 37]) + (key*roll)[...,None]*ink
        color += (1-v)[...,None]*[33, 9, 15]
        lip = np.clip((.045-u)/.045,0,1)
        color = color*(1-lip[...,None])+lip[...,None]*[205, 175, 173]
    region = rgb[top:bottom,left:right]
    np.copyto(region, color, where=inside[...,None])


def print_disc(rgb, center, phase):
    # A lacquered solar disc with a crimson belly, wine-coloured cap and a
    # fine set of curved engraved lines in its dark hemisphere.
    x,y = grid(CHECKER_WIDTH)
    sx, sy = (x-center[0])/19.3, (y-center[1])/19.3
    rr = sx*sx + sy*sy
    sz = np.sqrt(np.maximum(0,1-rr))
    tilt = .36 + .19*np.sin(phase-.5)
    angle = .89*sy + tilt*sx + (.30+.10*np.cos(phase))*sz
    levels = [-1.45,-.79,-.55,-.18,.12,.36,.66,1.50]
    inks = [(32,29,61),(92,68,108),(163,40,86),(235,48,64),(219,29,39),
            (137,19,41),(53,13,32),(16,12,24)]
    surface = np.stack([np.interp(angle,levels,[i[c] for i in inks]) for c in range(3)],axis=-1)
    rim = (1-sz)**6 * np.clip(.42-.32*sy-.45*sx,0,1)
    surface += rim[...,None]*[172,133,131]
    latitude = np.cos(TAU*(6*angle+.19*sx))
    etched = np.clip((latitude-.83)*5,0,1)*np.clip((angle-.33)*3,0,1)
    surface += etched[...,None]*[33,13,23]
    np.copyto(rgb, surface, where=(rr<1)[...,None])


def checker_print(phase):
    # A continuous concertina, viewed from above and to the left. Back and
    # front corners share the same hinge; each leaf keeps its own depth order.
    u, v = np.sin(phase), np.cos(phase)
    def seam(x,y, dx,dy, sway,lift):
        back = (x+sway*u,y+lift*v)
        front = (back[0]+dx+1.7*v,back[1]+dy+1.2*u)
        return back,front

    folds = (seam(10,33,10,6,1,1), seam(99,9,26,12,6,2),
             seam(76,29,25,18,3,-2), seam(193,8,32,16,-7,2),
             seam(160,34,34,16,4,-1.8), seam(299,13,10,5,2,-2))
    center = (167+7*np.cos(phase-.45),24.5+2.2*np.sin(phase))
    rgb = np.empty((HEIGHT*SCALE,(CHECKER_WIDTH)*SCALE,3),dtype=float)
    rgb[:] = PAPER
    # Positive counts are checker columns. Negative counts are engraved lines
    # on the two reverse faces, one in graphite and one in garnet.
    for i, columns in enumerate((7,-5,7,-7,8)):
        if i == 3:
            print_disc(rgb, center, phase)
        paper_plane(rgb,folds[i],folds[i+1],columns,center,phase)
    return Image.fromarray(np.uint8(np.clip(rgb,0,255)))


def stencil_print(phase):
    x, y = grid(STENCIL_WIDTH)
    # The moving grooves run continuously through the curved stencil.
    xx = (x-155)/127
    yy = (y-28)/16.7
    bend = .25*np.sin(phase+3.3*xx) + .47*np.sin(5.8*xx-.62*np.sin(phase))
    v = yy-bend
    capsule = (np.abs(xx)**3.0 + np.abs(v*.98)**3.0) < 1
    groove = 1.18*xx + .24*v + .20*np.sin(3.2*xx + 1.6*v + phase) + .085*np.sin(7*xx - 2.3*v - phase)
    slits = np.cos(TAU*(5.3*groove - .20*np.sin(phase))) > -.04
    edge = capsule & slits
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


def save_loop(name, width, render, rules, palette_path, expected_digest):
    palette = Image.new('P', (1, 1))
    with Image.open(palette_path) as inks:
        palette.putpalette(inks.tobytes())

    frames = []
    pixels = hashlib.sha256()
    for i in range(FRAMES):
        image = render(TAU * i / FRAMES)
        pen = ImageDraw.Draw(image)
        for start, end in rules:
            pen.line((start*SCALE, 52*SCALE, end*SCALE, 52*SCALE), fill=(63, 61, 84), width=2)
        frame = image.resize((width, HEIGHT), Image.Resampling.LANCZOS)
        frame = frame.quantize(palette=palette, dither=Image.Dither.NONE)
        pixels.update(frame.convert('RGB').tobytes())
        frames.append(frame)
    if pixels.hexdigest() != expected_digest:
        raise ValueError(f'{name} differs from the original desktop loop')

    # The exporter accepts millisecond delays. Distribute rounding across the five-second cycle.
    times = [round(i*1000/FPS) for i in range(FRAMES+1)]
    durations = [b-a for a, b in zip(times, times[1:])]
    frames[0].save(HERE / name, format='PNG', save_all=True, append_images=frames[1:],
                   duration=durations, loop=0, disposal=0, blend=0)


if __name__ == '__main__':
    casting = engraved_metal()
    save_loop('silver-engraving.apng', SILVER_WIDTH, lambda phase: chrome_print(casting, phase), ((1,58), (402,446)),
              HERE / 'silver-paper-palette.png',
              'cd1901a48e66d068191ea9f6c48c900ba66de8bf10eedcf73ec1e8c50128cea6')
    save_loop('checkerboard.apng', CHECKER_WIDTH, checker_print, ((13,45), (275,306)),
              HERE / 'silver-paper-palette.png',
              '8412e539edbf94aacf7dea0520284676b5db45971721f3b66852adef6a500b88')
    save_loop('fluorescent-stencil.apng', STENCIL_WIDTH, stencil_print, ((12,39), (263,310)),
              HERE.parent / 'stencil-palette.png',
              '00b44ca537e547806f04531aff4fb05d01b1e95d1114ba672104eb3935c101f5')
