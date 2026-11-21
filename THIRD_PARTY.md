# Screen artwork and type

| Material | Origin and use |
|---|---|
| Departure Mono | Copyright 2022–2024 Helena Zhang. SIL Open Font License 1.1. The instrument uses fixed 7 × 12 body cells and a narrower bitmap for note names. [Source font](assets/fonts/DepartureMono-Regular.otf) · [Licence](licenses/OFL-departure-mono.txt) |
| Michroma | Copyright 2011 The Michroma Project Authors. SIL Open Font License 1.1. The instrument uses a raster wordmark and a sixteen-glyph, fixed-width hexadecimal alphabet. [Source font](assets/fonts/Michroma-Regular.ttf) · [Licence](licenses/OFL-michroma.txt) |
| Folded ribbon | Original artwork generated for hdslave's TB-MPC-3-P-0 with OpenAI's built-in image generator. The source was cropped, reduced to four inks and hand-cleaned at its corners and fold. The embedded version measures 28 × 11 pixels. [Original](assets/explore/ribbon-original.png) · [Final art](assets/art/fold.png) |
| Screen studies | Three original full-screen directions generated for this instrument. The native-size studies use the supplied Melodee soft keys. The generator prompts and source images are in the exploration directory. [Prompts](assets/explore/prompts.md) |
| Host chrome | The preview compositor uses the top 22 rows of the supplied Melodee Edit reference. The integration captures show Melodee's own Edit and Stage chrome. These files are preview fixtures. [Soft-key strip](tools/ui_preview/edit-keys.png) |

The score, articulation marks, filter plots, pickup indicators, controls and layout are drawn specifically for this instrument. The plug-in embeds native bitmaps compiled from C++ headers. The font binaries and exploration PNGs stay in the source tree. [Asset generator](tools/ui_preview/assets.py)
