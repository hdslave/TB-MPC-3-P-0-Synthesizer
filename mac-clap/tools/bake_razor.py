"""Bake Windows plates from the SP-16 Thorn Crown artwork and native animation bank.
Usage: bake_razor.py SP16_CHECKOUT CHROMIUM
Normal builds use the committed bank. Requires Pillow and Playwright.
"""
import base64
import io
from pathlib import Path
import re
import sys

from PIL import Image
from playwright.sync_api import sync_playwright

source, chromium = map(Path, sys.argv[1:])
art = source / "tools/sp16_thorn"
native = source / "assets/sp16-thorn"
native_index = (native / "index.h").read_text()
native_bank = (native / "panel.bank").read_bytes()
assets = Path(__file__).resolve().parents[1] / "assets"
out = assets / "razor"
frames, names = [], {}


def add(image):
    buffer = io.BytesIO()
    image.save(buffer, "PNG", optimize=True)
    frames.append(buffer.getvalue())


def strip(tiles):
    w, h = tiles[0].size
    sheet = Image.new("RGBA", (len(tiles) * w, h))
    for i, tile in enumerate(tiles):
        sheet.paste(tile, (i*w, 0))
    return sheet


def native_frames(name):
    """Read the per-frame BGRA palette and scanline packets in the instrument bank."""
    group = re.search(rf'Images {name} {{(\d+), (\d+), (\d+), {name}Offsets, 0}}', native_index)
    w, h, count = map(int, group.groups())
    offsets = native_index.split(f'{name}Offsets[] = {{')[1].split('};')[0]
    starts = list(map(int, re.findall(r'\d+', offsets)))
    assert len(starts) == count
    for start in starts:
        palette = native_bank[start:start+1024]
        indices = bytearray()
        cursor = start + 1024
        for y in range(h):
            end = (y + 1) * w
            while len(indices) < end:
                code = native_bank[cursor]
                cursor += 1
                run = (code & 127) + 1
                if code & 128:
                    indices.extend(native_bank[cursor:cursor+1] * run)
                    cursor += 1
                else:
                    indices.extend(native_bank[cursor:cursor+run])
                    cursor += run
            assert len(indices) == end
        image = Image.frombytes("P", (w, h), bytes(indices))
        image.putpalette(b"".join(palette[i:i+3][::-1] for i in range(0, 1024, 4)))
        yield image


add(Image.open(art / "media/wordmark.webp").convert("RGBA"))
faces = [frame.convert("RGB") for frame in native_frames("face")]
names["faceCount"] = len(faces)
add(strip(faces).convert("RGB"))
# Share the desktop font masks and knob in their established slots.
index = (assets / "thorn/index.h").read_text().split('offset[] = {')[1].split('};')[0]
offset = list(map(int, re.findall(r'\d+', index)))
old = (assets / "thorn/panel.bank").read_bytes()
frames.extend(old[offset[i]:offset[i+1]] for i in range(2, 7))

with sync_playwright() as browser_api:
    browser = browser_api.chromium.launch(executable_path=str(chromium), args=["--no-sandbox"])
    page = browser.new_page()
    for src in ("lettering.js", "razor-choir.js", "step-counter.js"):
        page.add_script_tag(content=(art / src).read_text())
    font = base64.b64encode((art / "media/barlow-counter.woff2").read_bytes()).decode()
    page.evaluate("""async data => document.fonts.add(await new FontFace('SlinkItalic',
        'url(data:font/woff2;base64,'+data+')').load())""", font)
    page.evaluate("() => { window.makeCounter = makeStepCounter() }")

    def tile(kind, value):
        data = page.evaluate("""([kind,value]) => {
            const cv = document.createElement('canvas');
            const g = cv.getContext('2d');
            if (kind === 'notes') return chromeLetter(value).toDataURL();
            if (kind === 'counter') {
                cv.width=273; cv.height=210;
                g.setTransform(3,0,0,3,-528*3,-20*3);
                makeCounter(g,value,1);
            } else if (kind === 'glass') {
                cv.width=555; cv.height=270;
                g.setTransform(3,0,0,3,-527*3,-6*3); drawStepGlass(g);
            } else if (kind === 'steps') {
                cv.width=104; cv.height=66;
                scoreFigures(g,String(value).padStart(2,'0'),52,33,53,94,true);
            } else if (kind === 'labels') {
                cv.width=212; cv.height=60;
                scoreText(g,value,106,30,51,205,value==='NOTE'?'#171b30':'#f2f0ff');
            } else {
                cv.width=52; cv.height=64;
                if (value==='#') scoreText(g,value,26,32,54,48,kind==='pearl'?'#ffffff':'#171b30');
                else scoreFigures(g,value,26,32,54,48,kind==='pearl');
            }
            return cv.toDataURL();
        }""", [kind, value])
        return Image.open(io.BytesIO(base64.b64decode(data.split(",")[1]))).convert("RGBA")

    for kind, values in (("notes", "ABCDEFG"), ("pearl", "0123456789-#"), ("steel", "0123456789-#"),
                         ("steps", range(1,33)), ("labels", ["STEP","NOTE","GATE","ACC","SLD","PLAY","STOP","REGEN"]),
                         ("glass", [0]), ("counter", range(33))):
        names[kind] = len(frames)
        add(strip([tile(kind, v) for v in values]))
    browser.close()

for group, name in (("crest", "camera"), ("storm", "storm")):
    first = len(frames)
    for image in native_frames(group):
        add(image)
    names[name + "First"] = first
    names[name + "Count"] = len(frames) - first
offsets = [0]
for f in frames:
    offsets.append(offsets[-1] + len(f))
out.mkdir(exist_ok=True)
(out / "panel.bank").write_bytes(b"".join(frames))
(out / "index.h").write_text("// Generated by tools/bake_razor.py.\n#pragma once\n#include <cstdint>\nnamespace razor {\n"
    + "".join(f"constexpr int {k} = {v};\n" for k,v in names.items()) + "inline constexpr uint32_t offset[] = {\n"
    + "".join("    "+", ".join(map(str, offsets[i:i+10]))+",\n" for i in range(0,len(offsets),10)) + "};\n}\n")
