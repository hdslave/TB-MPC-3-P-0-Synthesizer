"""Pack the Thorn Crown SP-16 interface study as a standalone, offline page."""

import argparse
import base64
import json
from pathlib import Path

SOURCE = Path(__file__).resolve().parent
ROOT = SOURCE.parents[1]


def embed(name, mime):
    encoded = base64.b64encode((SOURCE / 'media' / name).read_bytes()).decode('ascii')
    return f'data:{mime};base64,{encoded}'


def build_fragment():
    assets = {
        'logo': embed('wordmark.webp', 'image/webp'),
        'face': embed('smiley.webp', 'image/webp'),
        'texture': embed('crest.webp', 'image/webp'),
        'depth': embed('depth.png', 'image/png'),
        'poster': embed('crest-poster.webp', 'image/webp'),
        'film': embed('crest.webm', 'video/webm'),
        'fonts': {
            'SlinkSans': embed('barlow-labels.woff2', 'font/woff2'),
            'SlinkItalic': embed('barlow-counter.woff2', 'font/woff2'),
        },
    }
    scripts = [(SOURCE / name).read_text() for name in
               ('camera.js', '../../mac-clap/tools/footer-art/whip-panorama.js', 'whip-storm.js', 'step-counter.js', 'lettering.js', 'razor-choir.js', 'panel.js')]
    scripts[-1] = scripts[-1].replace('THORN_ASSETS', json.dumps(assets, separators=(',', ':')))
    notice = (ROOT / 'licenses/OFL-barlow.txt').read_text()
    return ((SOURCE / 'frame.in').read_text() +
            '\n<script type="text/plain" id="slink-font-licenses">\n' + notice + '\n</script>\n' +
            '<script>\n(()=>{\n' + '\n'.join(scripts) + '\n})();\n</script>\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path, nargs='?', default=ROOT / 'build/slink-thorn-crown.html')
    args = parser.parse_args()
    page = ('<!doctype html>\n<html lang="en"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width,initial-scale=1">'
            '<title>Slink · Thorn Crown</title><style>body{margin:0;background:#080812}'
            'main{max-width:1100px;margin:auto}</style></head><body><main>\n' +
            build_fragment() + '</main></body></html>\n')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(page, encoding='utf-8')
    print(args.output)


if __name__ == '__main__':
    main()
