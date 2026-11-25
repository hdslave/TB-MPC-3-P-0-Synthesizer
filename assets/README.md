# MPC silver panel artwork

The nameplate uses the original blue-chrome Slink wordmark in `art/slink.png`. The master face in `art/satin-knob.png` comes from the approved satin knob study.

The knob art direction called for a front-facing blue-silver rotary dial with a broad upper-left reflection, a polished bevel and a short shaded sidewall. The white pointer, cobalt travel arc and pickup marker belong to the control itself.

Run `python3 tools/ui_preview/silver.py` with Pillow to rebuild `src/ui/silver_art.h`. The title and 49 pointer positions share one indexed palette. Identical dial scanlines are stored once.
