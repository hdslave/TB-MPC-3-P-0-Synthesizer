"""Render the desktop footer from the original SP-16 whip and lightning artwork."""

import argparse
import base64
import io
from pathlib import Path
import struct
import zlib

from PIL import Image
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'tools/sp16_thorn/whip-storm.js'
DEST = ROOT / 'mac-clap/assets/footer-storm.bank'
WIDTH, HEIGHT = 2240, 80
FPS, SECONDS = 60, 8
FRAMES = FPS * SECONDS


def capture(page, frame):
    data = page.evaluate('at => captureStorm(at)', frame/FPS)
    return Image.open(io.BytesIO(base64.b64decode(data))).convert('RGB')


def bake(page):
    # One palette for the whole loop keeps the chrome and violet reflections stable.
    samples = range(0, FRAMES, 15)
    sheet = Image.new('RGB', (WIDTH//2, HEIGHT*len(samples)))
    for n, frame in enumerate(samples):
        sheet.paste(capture(page, frame).resize((WIDTH//2, HEIGHT)), (0, n*HEIGHT))
    palette = sheet.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    rgb = palette.getpalette()
    bgra = b''.join(bytes((rgb[i+2], rgb[i+1], rgb[i], 255)) for i in range(0, 768, 3))
    header = struct.pack('<4s4H', b'SFP3', WIDTH, HEIGHT, FRAMES, FPS)
    offsets = [len(header) + len(bgra) + 4*(FRAMES+1)]
    packets = []
    for i in range(FRAMES):
        frame = capture(page, i).quantize(palette=palette, dither=Image.Dither.NONE)
        packet = zlib.compress(frame.tobytes(), 9)
        packets.append(packet)
        offsets.append(offsets[-1] + len(packet))
    DEST.write_bytes(header + bgra + struct.pack(f'<{len(offsets)}I', *offsets) + b''.join(packets))
    print(f'{DEST.name}: {WIDTH} x {HEIGHT}, {FRAMES} frames at {FPS} fps, {offsets[-1]:,} bytes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--chromium', help='Chromium executable, or use the Playwright browser')
    parser.add_argument('--proof', type=Path, help='Save four poses instead of baking the bank')
    args = parser.parse_args()
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(executable_path=args.chromium)
        page = browser.new_page()
        page.set_content(f'<canvas width="{WIDTH}" height="{HEIGHT}"></canvas>')
        page.add_script_tag(path=SOURCE)
        page.add_script_tag(path=ROOT / 'mac-clap/tools/footer-art/whip-panorama.js')
        page.evaluate("""() => {
            const canvas = document.querySelector('canvas'), ctx = canvas.getContext('2d');
            const composition = makeFooterPanorama(), scale = canvas.height / 24;
            if (composition.width * scale !== canvas.width)
                throw new Error('The storm must be sampled at the same scale on both axes.');
            ctx.scale(scale, scale);
            ctx.imageSmoothingQuality = 'high';
            const draw = makeWhipStorm(ctx, {left: 0, top: 0, composition, detail: 2});
            window.captureStorm = time => {
                draw(time, ...thornStudyPattern);
                return canvas.toDataURL().split(',')[1];
            };
        }""")
        if args.proof:
            sheet = Image.new('RGB', (WIDTH, HEIGHT*4))
            for row in range(4):
                sheet.paste(capture(page, row*FRAMES//4), (0, row*HEIGHT))
            sheet.save(args.proof)
        else:
            bake(page)
        browser.close()
