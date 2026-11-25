# MPC silver panel artwork

The nameplate uses the original blue-chrome Slink wordmark in `art/slink.png`. The master face in `art/satin-knob.png` comes from the approved satin knob study.

The knob art direction called for a front-facing blue-silver rotary dial with a broad upper-left reflection, a polished bevel and a short shaded sidewall. The white pointer, cobalt travel arc and pickup marker belong to the control itself.

Run `python3 tools/ui_preview/silver.py` with Pillow to rebuild `src/ui/silver_art.h`. The MPC wordmark is drawn from its source at 216 × 27 pixels on a 34-pixel-high plate. Three rows of brushed silver sit above the lettering and two rows separate it from the lower bevel. A broad upper-left highlight fades into a cool silver shadow at the lower right. The complete plate is baked into its own indexed palette. The dials share an indexed palette across 49 pointer positions. Identical dial scanlines are stored once. The MPC faceplate uses a repeating horizontal brush pattern with a maximum change of three levels per RGB channel. The MPC draws its dials at 27 pixels.

Run `python3 tools/ui_preview/press_type.py` to rebuild the antialiased MPC labels, values and seed numerals from the Barlow fonts in `assets/fonts/`.
