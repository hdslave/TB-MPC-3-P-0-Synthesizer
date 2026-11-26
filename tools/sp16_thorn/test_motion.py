"""Exercise the footer at fixed times with Playwright, Pillow and a Chromium browser."""

import argparse
import base64
import io
from pathlib import Path

from PIL import Image, ImageChops, ImageStat
from playwright.sync_api import sync_playwright


def difference(a, b):
    return sum(ImageStat.Stat(ImageChops.difference(a, b)).mean) / 3


def check(browser):
    page = browser.new_page()
    page.set_content('<canvas width="334" height="48"></canvas>')
    page.add_script_tag(path=Path(__file__).with_name('whip-storm.js'))
    page.evaluate("""() => {
        const canvas = document.querySelector('canvas');
        const context = canvas.getContext('2d');
        context.setTransform(2, 0, 0, 2, -403, -507);
        const draw = makeWhipStorm(context);
        const pattern = [
            [1,1,1,1,0,0,1,1,0,1,1,1,0,1,1,0],
            [0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],
            [0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,1]
        ];
        window.capture = ([time, accentAge, edit]) => {
            const rows = pattern.map(row => row.slice());
            if (edit !== null) rows[edit][7] ^= 1;
            context.clearRect(201.5, 253.5, 167, 24);
            draw(time, ...rows, accentAge ?? Infinity);
            return canvas.toDataURL().split(',')[1];
        };
    }""")

    def frame(time, accent_age=None, edit=None):
        data = page.evaluate('args => window.capture(args)', [time, accent_age, edit])
        return Image.open(io.BytesIO(base64.b64decode(data))).convert('RGB')

    for t in (0, .25, 1.5, 2.75, 3.75):
        assert difference(frame(t), frame(t + 8)) == 0, f'Loop mismatch at {t}s'

    seam = [difference(frame(8 + i / 60), frame(8 + (i + 1) / 60)) for i in (-2, -1, 0, 1)]
    assert min(seam) > 0 and max(seam) < 1.4 * min(seam), f'Uneven loop seam: {seam}'

    neutral = frame(.65)
    for row in range(3):
        assert difference(neutral, frame(.65, edit=row)) > .1, f'Row {row} edit had no visible effect'
    # An inlay on the underside alone can pass a timing check while being invisible.
    assert difference(neutral, frame(.65, accent_age=.1)) > 1, 'Accent reflection hidden'
    assert difference(neutral, frame(.65, accent_age=.48)) == 0, 'Accent failed to decay'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--chromium', help='Path to Chromium, or use Playwright’s installed browser')
    args = parser.parse_args()
    with sync_playwright() as playwright:
        with playwright.chromium.launch(executable_path=args.chromium) as browser:
            check(browser)
    print('Loop, articulation edits and accent reflection passed.')
