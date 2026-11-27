"""Register the original chrome artwork to the 33 SP-16 poses and bake desktop plates.

Run with Pillow, NumPy and SciPy. Registration runs offline. A single smooth
inverse warp carries the painted features and silhouette together at each pose.
"""
from pathlib import Path
import io
import struct

import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter, gaussian_filter1d, map_coordinates

ROOT = Path(__file__).resolve().parents[2]
GROUND = (8, 8, 18)
COUNT = 33
DENSITY = 4


def opaque(image):
    bed = Image.new('RGBA', image.size, GROUND + (255,))
    return Image.alpha_composite(bed, image.convert('RGBA')).convert('RGB')


def features(rgba):
    light = np.asarray(opaque(rgba).convert('L'), dtype=float) / 255
    alpha = np.asarray(rgba.getchannel('A'), dtype=float) / 255
    return light, alpha


def curvature(grid, tile):
    y, x = (grid + .5) * 2 / tile - 1
    return np.stack((np.ones_like(x), x, y, x*x, x*y, y*y))


def register(reference, target, grid, basis, coeff):
    # Fit the whole face as one gently curved surface. Six coefficients per
    # axis let the eyes travel with the body while the four horns follow its rim.
    weights = np.array([1, 1, 1.5, 1.5, 2.5, 1.5] * 2)
    for blur in (2.2, 1.1):
        layers = []
        for src, dst in zip(reference, target):
            src = gaussian_filter(src, blur)
            layers.append((src, gaussian_filter(dst, blur), *np.gradient(src)))
        for _ in range(6):
            coords = grid + np.einsum('ab,bij->aij', coeff, basis)
            jacobian, residual = [], []
            for src, dst, dy, dx in layers:
                def sample(field):
                    return map_coordinates(field, coords, order=1, mode='nearest')
                residual.append((dst - sample(src)).ravel())
                jacobian.append(np.concatenate((basis * sample(dy), basis * sample(dx))).reshape(12,-1).T)
            jacobian = np.vstack(jacobian)
            error = np.concatenate(residual)
            # Damp changes where a moving reflection gives little geometric information.
            lhs = jacobian.T @ jacobian + np.diag(weights*.55)
            rhs = jacobian.T @ error - weights*.05*coeff.ravel()
            delta = np.linalg.solve(lhs, rhs).reshape(2,6)
            coeff = coeff + np.clip(delta, -1.25, 1.25)
    return coeff


def bake():
    master = Image.open(ROOT/'mac-clap/tools/smiley/chrome-master.png').convert('RGBA')
    atlas = Image.open(ROOT/'tools/sp16_thorn/media/smiley.webp').convert('RGBA')
    tile = atlas.width // 8
    grid = np.indices((tile, tile), dtype=float)
    ref = features(master.resize((tile, tile), Image.Resampling.LANCZOS))
    guides = []
    for i in range(COUNT):
        x, y = (i % 8) * tile, (i // 8) * tile
        guides.append(features(atlas.crop((x, y, x+tile, y+tile))))
    # Start at the pose nearest the front-facing master, and follow the two turns.
    front = min(range(COUNT), key=lambda i: np.abs(guides[i][0] - ref[0]).mean())
    basis = curvature(grid, tile)
    transforms = np.empty((COUNT, 2, 6))
    transforms[front] = register(ref, guides[front], grid, basis, np.zeros((2, 6)))
    for direction in (-1, 1):
        for i in range(front+direction, COUNT if direction > 0 else -1, direction):
            transforms[i] = register(ref, guides[i], grid, basis, transforms[i-direction])
    transforms = gaussian_filter1d(transforms, .85, axis=0, mode='nearest')

    # Match the original face's placement inside the 61 x 58 hardware region.
    # Sample the master directly at 4x for every pose, including the horn tips.
    size = (61*DENSITY, 58*DENSITY)
    pixels = np.indices(size[::-1], dtype=float)
    pixels[0] = ((pixels[0]+.5)/DENSITY+.85)*tile/59-.5
    pixels[1] = ((pixels[1]+.5)/DENSITY-.5)*tile/59-.5
    rgb = np.asarray(opaque(master))
    packets = []
    final_basis = curvature(pixels, tile)
    for index, coeff in enumerate(transforms):
        offset = np.einsum('ab,bij->aij', coeff, final_basis)
        coords = (pixels+offset+.5)*(master.width/tile)-.5
        plate = np.stack([map_coordinates(rgb[...,c], coords, order=3,
                         mode='constant', cval=GROUND[c]) for c in range(3)], axis=-1)
        # The moving chrome reflections belong to the original turn. Carry its
        # broad lighting changes onto the painted metal without importing pixel edges.
        guide_coords = grid + np.einsum('ab,bij->aij', coeff, basis)
        neutral = map_coordinates(ref[0], guide_coords, order=1, mode='nearest')
        exposure = (gaussian_filter(guides[index][0], 3) + .12) / (gaussian_filter(neutral, 3) + .12)
        exposure = np.clip(map_coordinates(exposure, pixels, order=1, mode='nearest'), .65, 1.55)
        v = plate.astype(float)/255
        gain = exposure[...,None]**2
        lit = np.rint(255 * gain * v / (1 + (gain-1)*v)).astype(np.uint8)
        plate = np.where(np.any(plate != GROUND, axis=-1, keepdims=True), lit, plate)
        encoded = io.BytesIO()
        Image.fromarray(plate).save(encoded, format='PNG', optimize=True)
        packets.append(encoded.getvalue())
    offsets = [4*(COUNT+1)]
    for packet in packets:
        offsets.append(offsets[-1] + len(packet))
    path = ROOT/'mac-clap/assets/desktop-smiley.bank'
    path.write_bytes(struct.pack(f'<{len(offsets)}I', *offsets) + b''.join(packets))
    print(f'{COUNT} smiley poses, {size[0]} x {size[1]}, {path.stat().st_size} bytes')


if __name__ == '__main__':
    bake()
