# TB-MPC-3-P-0 screen

The Press Plate screen puts a lime melting smiley on the right end of a stamped cobalt and aluminium panel. PATTERN uses the dark window for the four-digit seed, seed mode, transport and position. Turning the seed dial before publication shows PENDING and the seed of the score still on screen. In Notes mode the title bar reads “MIDI / SEQ MUTED”. Before the first pattern arrives the score displays “Awaiting clock”.

Each column of the punched-metal matrix shows one step's number, nearest note, gate, accent and outgoing slide. A filled rectangle opens the gate, a diamond marks an accent, and an arrow marks a slide. A small cobalt dot under a note indicates fractional-semitone CV. Zero CV is C2. The lime column follows playback. Patterns up to 16 steps use one bank. Longer patterns stack two banks so all 32 steps stay visible. The dark strip above the matrix lists scale, root, octave, density and clock division.

On all five pages, two narrow slices through the smiley’s lower drips move sideways on a four-step downbeat or a gated accent. The offset grows from one native pixel to two, returns to one, and settles after 144 ms. Closely spaced hits share the current pulse. Stop clears it immediately. The motion uses Melodee’s display clock and the published sequencer position.

## Controls

| Group | Rows or performance controls |
|---|---|
| PATTERN | K1 Cutoff, K2 Resonance, K3 Env Mod. Wheel changes Seed by one per detent, or sixteen with Shift. Turning locks a Random seed. Push presses Regen. Shift + Push presses Seed Back. Minus / Plus changes group. |
| VOICE | Tuning, Cutoff, Resonance, Env Mod, Decay, Accent, Waveform, Volume |
| TONE | Filter, Peak Spread, Drive, Slide, Slide Time, Accent Decay |
| TB-3PO | Mode, Seed Mode, Seed, Regen, Seed Back, Mutate, Density, Scale, Root, Octave, Steps, Gate, Transpose |
| CLOCK | Clock Div, Swing, Reset |

On a list the wheel moves one row per detent and stops at the ends. Push enters or leaves editing. On Regen and Seed Back it presses the button. While editing, a detent moves a choice or integer by one, or a continuous value by 1/128 of its range. Shift gives 1/1024 for a continuous value. Minus / Plus moves an edited choice or integer by one and a continuous value by 1/16. Outside editing it jumps to the first row of the neighbouring group.

The pots turn the selected row and the two below it. Buttons and missing rows have no pot binding. A cobalt arc and an ivory pointer show each dial setting. A small mark beneath the dial carries the track colour. A hollow square shows a known physical pot position waiting for pickup. Shift replaces the cards with the controls for the current page and edit state. Melodee owns pot takeover, its soft keys, and Shift + wheel when the host uses it to fine-edit the last touched pot.

VOICE and TONE plot the selected filter from 20 Hz to 20 kHz on a logarithmic axis. The vertical range is −36 to +24 dB. The curve uses the synth's response equations at the current settings, before envelope and accent modulation. The dotted vertical marker and its foot show the frequency reported by Cutoff. Twin Peak marks its lower edge. TB-3PO and CLOCK show the published seed and a small score. CLOCK divides each two-step interval at Swing, with a fixed halfway tick for reference. [Parameter descriptions](../README.md#controls)

## Art and previews

The approved master supplies the 320 × 64 PATTERN plate and the 320 × 42 editor plate. The asset generator extends a straight section of rail to fill the panel and preserves the smiley’s proportions at both heights. Both plates share a 128-colour palette. Michroma supplies the wordmark, Barlow Condensed Bold Italic the seed and values, and Departure Mono the labels. [Master](../assets/explore/press-plate/master.png) · [Pattern plate](../assets/art/press-hero.png) · [Editor plate](../assets/art/press-editor.png) · [Credits](../THIRD_PARTY.md)

The contact sheet covers 20 fixture states, including a dense 32-step phrase, a pending seed in MIDI Notes mode, and the screen before the first clock. Each state has native RGB, nearest-neighbour 3×, RGB565 and a 320 × 240 Edit composite. The strip follows steps 4–7. The 2× GIF follows one complete cycle sampled every 30 ms, including the frames between steps. [Contact sheet](../assets/previews/contact-sheet-2x.png) · [Step strip](../assets/previews/step-advance-strip.png) · [16-step loop](../assets/previews/step-advance.gif)

From the checkout root, run the C++17 preview host on macOS. Rendering the gallery also uses Python with Pillow or the launcher's uv fallback.

```sh
tools/ui_preview/run --render
tools/ui_preview/run --test
tools/ui_preview/run --bench
```

Rebuild the native-size art and type with `python3 tools/ui_preview/assets.py` in a Pillow environment. The PNGs and glyph arrays are committed. The gallery and test runner use the same screen class as the plug-in. The hash check reads the checked-in expectations. After an intentional visual change, `--hashes` prints candidate replacements for review.

For Linux use the running Colima profile, Docker context and builder image below. Start the profile with `colima start melodee` if needed.

```sh
./build.sh
docker --context colima-melodee run --rm --platform linux/arm64 \
    -v "$PWD:/work" -w /work -e CPU_FLAGS=-mcpu=cortex-a55 melodee-builder \
    sh -c 'tools/ui_preview/run --test && tools/ui_preview/run --bench'
tools/ui_preview/melodee
```

The last command runs the built plug-in through the sibling hdslaveOS checkout (`../melodee-hdslave`, branch `hdslave`), mounted read-only. Set `MELODEE_SCREEN_TREE` to use a different path to that build. It captures the first six prescribed states, then exercises the two other filters and both sequencer lists.

| Capture | Live host state |
|---|---|
| [1](../assets/previews/melodee/1@2x.png), [2](../assets/previews/melodee/2@2x.png) | Edit opens on seed 3039 in Random. Two wheel detents dial 303B, lock it and leave 3039 on the score. |
| [3](../assets/previews/melodee/3@2x.png), [4](../assets/previews/melodee/4@2x.png) | VOICE opens on Tuning. Select and edit Cutoff, which moves from 535 Hz to 672 Hz. |
| [5](../assets/previews/melodee/5@2x.png), [6](../assets/previews/melodee/6@2x.png) | Shift shows the list legend. Releasing Shift and switching to Stage restores the cards under Stage's soft keys. |
| [7](../assets/previews/melodee/7@2x.png), [8](../assets/previews/melodee/8@2x.png) | TONE selects Twin Peak, then 18 dB. The trace, marker and value follow. |
| [9](../assets/previews/melodee/9@2x.png), [10](../assets/previews/melodee/10@2x.png) | TB-3PO and CLOCK each open on their first row and retain the published score. |

All ten host reports selected `screen=1` and recorded the knob bindings. A separate live transport check on VOICE captured the resting image and both one- and two-pixel offsets. Across 26 playback samples, differences inside the plug-in’s overlay-safe area were confined to the drip tips. Stop restored the original image and a later idle capture matched it. The playing states in the gallery use an explicit fixture pattern. [Host log](../assets/previews/melodee/host.txt)

## Bench and changed pixels

Measured 2026-11-21. Each entry is the mean elapsed time for 2,000 calls to `update` plus `paint`. Event preparation and the pixel comparison used for reporting are outside the timer. The M4 Max runs native Apple Clang at `-O3`. The arm64 Colima VM runs GCC 12 at `-O3 -mcpu=cortex-a55` on the same Mac.

| Operation | M4 Max, ms | Linux VM, ms |
|---|---:|---:|
| Full PATTERN, 16 steps | 0.031232 | 0.034124 |
| Idle | 0.000022 | 0.000039 |
| Partial step, 16 steps | 0.025809 | 0.027790 |
| Full PATTERN, 32 steps | 0.034661 | 0.037237 |
| Partial step, 32 steps | 0.025652 | 0.027637 |
| Full VOICE, Twin Peak | 0.039112 | 0.040289 |
| Partial VOICE cursor move | 0.021031 | 0.022213 |
| Full VOICE while Resonance changes | 0.041171 | 0.044543 |
| Partial VOICE while Resonance changes | 0.037124 | 0.037728 |

Peak changed pixels between successive frames, counted across a full clock cycle or a traversal of every editor row. Both targets produced the same counts.

| Change | Maximum pixels |
|---|---:|
| Idle | 0 |
| Drip motion alone on PATTERN | 170 |
| Drip motion alone on an editor | 71 |
| 16-step advance, sequencer or Notes mode | 2,581 |
| 32-step advance | 1,339 |
| Dense microtonal 32-step advance | 1,312 |
| TB-3PO miniature advance | 1,078 |
| CLOCK miniature advance | 1,059 |
| VOICE cursor | 5,910 |
| TONE cursor | 6,228 |
| TB-3PO cursor | 6,375 |
| CLOCK cursor | 5,892 |

Paint retains the last plug-in pixels independently of host overlays. Only invalidated regions are rendered. It compares their scanlines against the retained image before writing to the host. A whole paint covers all 320 × 218 pixels and respects the supplied stride.

The Linux ARM build passed 732,233 host checks, 512 sequencer reference checks and all 74 sound fingerprints. The CLAP measures 264,768 bytes. Both targets passed the 20 image hashes, all sixteen hex digits in all four positions, controls, bindings, allocation checks, lengths 0–32, full/partial redraw comparisons and stride guards. Motion checks exercise both plate sizes, including ordinary gates, off-beat gated accents, intermediate offsets, settling and stopping mid-pulse.

## Engine handoff and hardware status

The existing Screen calls, Host, Edits, parameter IDs, PatternView and seqlock keep their contract. The supplied response functions and pattern fields cover this page. It needs no additional engine data. This screen revision builds on main’s existing clock and silent-instrument fixes.

Physical MPC testing is outstanding for the complete page, including on-device redraw time, SPI transfer time while playing, panel colour and readability, and the feel of encoder edits and pot pickup. The RGB565 previews, desktop timing runs and Melodee captures are the available checks.
