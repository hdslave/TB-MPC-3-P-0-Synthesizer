"""Publish RGB and RGB565 proofs from the native C++ renderer."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parents[2]
frames = root/'build/ui-preview/frames'
out = root/'assets/previews'
out.mkdir(exist_ok=True)
chrome = Image.open(root/'tools/ui_preview/edit-keys.png').convert('RGB')
labels = {
    'pattern-16': 'Sixteen steps', 'pattern-32': 'Thirty-two steps', 'pending': 'Pending seed',
    'random': 'Random seed', 'notes': 'MIDI notes', 'voice-303': '303 response',
    'voice-twin': 'Twin Peak  /  spread 3', 'voice-18db': '18 dB  /  full resonance',
    'tone-drive': 'Drive edit', 'tb3po-scale': 'Longest scale name', 'clock-swing': 'Swing  /  66%',
    'shift': 'Shift', 'pickup': 'K2 pickup', 'track-coral': 'Coral  /  FF7A5C',
    'track-mint': 'Mint  /  46D9B0', 'track-violet': 'Violet  /  9D8CFF', 'track-yellow': 'Yellow  /  E8D94A',
}


def panel(im):
    channels = []
    for source, bits in zip(im.convert('RGB').split(), (5,6,5)):
        quant = [v >> (8-bits) for v in range(256)]
        channels.append(source.point([(v << (8-bits)) | (v >> (2*bits-8)) for v in quant]))
    return Image.merge('RGB',channels)


def scale(im, n):
    return im.resize((im.width*n, im.height*n),Image.Resampling.NEAREST)


def composite(im):
    screen = Image.new('RGB',(320,240))
    screen.paste(im,(0,22))
    screen.paste(panel(chrome),(0,0))
    return screen


font = ImageFont.truetype(str(root/'assets/fonts/DepartureMono-Regular.otf'),22)
display = ImageFont.truetype(str(root/'assets/fonts/Michroma-Regular.ttf'),38)
sheet = Image.new('RGB',(24 + 3*664, 100 + ((len(labels)+2)//3)*536),'#0a0f0d')
draw = ImageDraw.Draw(sheet)
draw.text((24,12),'TB-MPC-3-P-0',font=display,fill='#edf0db')
draw.text((24,64),'RGB565   /   2x',font=font,fill='#aab9a6')
for i,(name,label) in enumerate(labels.items()):
    dest = out/'states'/name
    dest.mkdir(parents=True,exist_ok=True)
    im = Image.open(frames/(name+'.ppm'))
    assert im.size == (320,218)
    im.save(dest/'1x.png')
    scale(im,3).save(dest/'3x.png')
    panel(im).save(dest/(name+'-panel.png'))
    shown = composite(panel(im))
    shown.save(dest/'composite.png')
    x,y = 24 + i%3*664, 102 + i//3*536
    sheet.paste(scale(shown,2),(x,y))
    draw.text((x,y+485),label,font=font,fill='#c6d2bb')
sheet.save(out/'contact-sheet-2x.png',optimize=True)

sequence = [composite(panel(Image.open(frames/f'step-{i:02d}.ppm'))) for i in range(1,17)]
strip = Image.new('RGB',(320*4,240))
for i in range(4):
    strip.paste(sequence[i+3],(i*320,0))
strip.save(out/'step-advance-strip.png')
sequence[0].save(out/'step-advance.gif',save_all=True,append_images=sequence[1:],
                 duration=125,loop=0,disposal=1,optimize=False)
