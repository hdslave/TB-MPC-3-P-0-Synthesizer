"""Original engraved foil and rose-engine linework for the silver MPC panel."""
from math import cos, sin, tau
from PIL import Image, ImageDraw

# Reflections move from violet through coral and sea-glass back to blue.
SPECTRUM = ((110, 87, 249), (228, 102, 223), (255, 173, 136),
            (152, 239, 205), (72, 198, 243), (110, 87, 249))


def spectrum(phase):
    p = (phase % 1) * (len(SPECTRUM) - 1)
    i = int(p)
    return tuple(round(a + (b-a)*(p-i)) for a, b in zip(SPECTRUM[i], SPECTRUM[i+1]))


def inlay(atlas):
    """Print the cobalt trim and black readout, retaining the mascot's original palette."""
    base = atlas.convert('RGB')
    positions = []
    engraving = bytearray()
    for y in range(base.height):
        hero = y < 64
        local_y = y if hero else y-64
        if local_y < 16:  # These source rows sit behind the silver title.
            continue
        # End the print before the ticket die-cut. The face and its fringing retain their inks.
        edge = 246 if hero else 271
        for x in range(edge):
            r, g, b = base.getpixel((x, y))
            cobalt = b > 140 and b > max(r, g) + 74
            if not cobalt and max(r, g, b) > 10:
                continue
            fade = min(1, (edge-x)/18)
            # Marbled strata split, fold and meet as they cross the long window.
            t = .035*x + .125*local_y + 1.95*sin(x/42 + local_y/17) + .94*cos(x/26-local_y/12)
            line = (.5+.5*cos(4.1*t))**5
            light = spectrum(.47 + x/233 + .17*sin(t))
            if cobalt:
                amount = fade*(.42+.49*line)
            else:
                amount = fade*(.043+.20*line)*(.52+.48*sin(x/43+.2)**2)
            engraving.extend(round(v+(c-v)*amount) for v, c in zip((r,g,b), light))
            positions.append(y*base.width+x)

    # Store original pixels losslessly. Quantize only the engraved part of the plate.
    old_indices = sorted(set(atlas.tobytes()))
    mapping = {old: new for new, old in enumerate(old_indices)}
    palette = atlas.getpalette()
    inks = [v for old in old_indices for v in palette[3*old:3*old+3]]
    strip = Image.frombytes('RGB', (len(positions), 1), bytes(engraving))
    strip = strip.quantize(colors=256-len(old_indices), dither=Image.Dither.NONE)
    indices = bytearray(mapping[i] for i in atlas.tobytes())
    for pos, ink in zip(positions, strip.tobytes()):
        indices[pos] = len(old_indices) + ink
    result = Image.frombytes('P', atlas.size, bytes(indices))
    result.putpalette(inks + strip.getpalette())
    return result


def scope_glass(width, height):
    """An offset six-petal spiral with widely spaced rings around a quiet centre."""
    aa = 3
    image = Image.new('RGB', (width*aa, height*aa))
    bg = ImageDraw.Draw(image)
    for y in range(height*aa):
        t = y/(height*aa)
        bg.line((0,y,image.width,y),fill=(round(16+10*t),round(17+2*t),round(29+8*t)))
    cx, cy = width*.74, height*.51
    # Project nested petals obliquely, giving the outer rings a different axis from the core.
    for i in range(22):
        size = 6.8 * 1.178**i
        phase = .48*i
        points = []
        for j in range(361):
            a = tau*j/360
            radius = size*(1+.22*cos(6*a-phase)+.026*cos(12*a-2*phase))
            x = cx + radius*(1.21*cos(a)+.23*sin(a)) + .085*size
            y = cy + radius*(.66*sin(a)-.16*cos(a))
            points.append((round(x*aa), round(y*aa)))
        for k in range(12):
            mid = tau*(k+.5)/12
            tint = spectrum(.76+.065*i+.27*cos(mid+phase*.16))
            strength = (.31+.16*cos(mid+phase*.13)) * (.62+.38*min(1,size/20))
            ink = tuple(round(base+(v-base)*strength) for base,v in zip((22,19,36),tint))
            bg.line(points[k*30:k*30+31], fill=ink, width=aa if i%4 else aa+1)
    return image.resize((width,height),Image.Resampling.LANCZOS)
