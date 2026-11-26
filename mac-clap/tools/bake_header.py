"""Render the desktop faceplate from the original camera mesh, glass and fonts."""
import argparse
import base64
import io
from pathlib import Path
import struct
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / 'mac-clap/assets'
sys.path.insert(0, str(ROOT / 'tools/sp16_thorn'))
from build import build_fragment


def bake_fonts():
    masks = bytearray()
    for factor in (1, 2, 4):
        for size, height in ((12, 15), (16, 20), (20, 25)):
            font = ImageFont.truetype(str(ROOT/'assets/fonts/Barlow-SemiBold.ttf'), size*factor)
            for code in range(32, 127):
                glyph = Image.new('L', (24*factor, height*factor))
                ImageDraw.Draw(glyph).text((0, size*factor), chr(code), anchor='ls', font=font, fill=255)
                masks.extend(glyph.tobytes())
    (DEST/'desktop-type.alpha').write_bytes(masks)


def bake_header(chromium):
    packets = []
    with tempfile.TemporaryDirectory() as folder, sync_playwright() as pw:
        html = Path(folder)/'panel.html'
        html.write_text('<meta charset="utf-8">'+build_fragment())
        browser = pw.chromium.launch(executable_path=chromium,
                                     args=['--use-angle=swiftshader','--enable-unsafe-swiftshader'])
        page = browser.new_page(reduced_motion='reduce')
        page.add_init_script('window.captureThornPlates = p => window.plates = p')
        page.goto(html.as_uri())
        page.wait_for_selector('#thorn-crown-metal[data-ready=true]')
        page.evaluate("""() => {
            const d=4, {bed,bedDraw,createCrest,buttonOn}=plates;
            bed.width=780*d; bed.height=285*d; bedDraw(false,true,-1,d);
            const base=document.createElement('canvas'); base.width=bed.width; base.height=100*d;
            base.getContext('2d').drawImage(bed,0,0);
            const counter=plates.makeStepCounter('62px SlinkSans',d);
            const camera=document.createElement('canvas');camera.width=332*d;camera.height=90*d;
            const pose=createCrest(camera);
            if(!pose) throw Error('Could not render the crest mesh');
            const tile=document.createElement('canvas');
            window.plateAt = index => {
                const rect=index<600?[182,0,340,100]:index<633?[522,0,193,100]:
                           index<637?[622,55,81,29]:index===637?[0,0,182,100]:[720,63,53,26];
                const [x,y,w,h]=rect;tile.width=w*d;tile.height=h*d;
                const g=tile.getContext('2d');g.drawImage(base,-x*d,-y*d);
                g.setTransform(d,0,0,d,-x*d,-y*d);
                if(index<600){
                    pose(index/30);
                    g.beginPath();g.moveTo(186,6);g.lineTo(511,6);g.lineTo(518,13);
                    g.lineTo(518,96);g.lineTo(193,96);g.lineTo(186,89);g.closePath();g.clip();
                    g.drawImage(camera,186,6,332,90);
                }else if(index<633) counter(g,index-600,1);
                else if(index<637) buttonOn(g,['PLAY','STOP','SYNC','PLAY MIDI'][index-633]);
                return tile.toDataURL().split(',')[1];
            };
        }""")
        for i in range(639):
            data = base64.b64decode(page.evaluate('i=>plateAt(i)', i))
            if i<600:
                image = Image.open(io.BytesIO(data)).convert('RGB')
                buf = io.BytesIO()
                image.save(buf, format='JPEG', quality=86, subsampling=0, optimize=True)
                data = buf.getvalue()
            packets.append(data)
        browser.close()
    offsets = [4*(len(packets)+1)]
    for packet in packets:
        offsets.append(offsets[-1]+len(packet))
    (DEST/'desktop-header.bank').write_bytes(struct.pack(f'<{len(offsets)}I', *offsets)+b''.join(packets))
    print('Desktop header', len(packets), 'plates,', offsets[-1], 'bytes')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--chromium')
    args=parser.parse_args()
    bake_fonts()
    bake_header(args.chromium)
