# TB-MPC-3-P-0 screen

PATTERN opens with a melting yellow smiley in a silver and violet record groove. The full-width pitch roll below it uses 314 × 103 native pixels. All 16 or 32 steps share one timeline. The ruler numbers every fourth step. A yellow playhead and the numeric position follow the clock.

Up to eight distinct semitone pitches have labelled rows. Denser or microtonal phrases use an octave ruler with a compact note name for each step. The heads still follow the published CV. Labels stagger on 32-step phrases. Zero CV is C2. A filled mint head opens the gate. A hollow head marks the pitch of a rest. Pink pips mark accents. Pink connectors and orange SLD arrows follow outgoing slides, including the last step’s arrow back to the first.

The four large digits follow the seed dial. Beside them, the page shows transport, step, clock division, scale, root, octave and density. The GATE, ACC and SLD lamps show the current step’s flags during playback. Dialling ahead shows NEXT above the seed. PENDING and the published score’s seed take the place of the lamps. MIDI NOTES and SEQ MUTED share that space when the sequencer counts silently. Both messages fit together. An unpublished pattern displays “Awaiting clock”.

## Controls

| Group | Rows or performance controls |
|---|---|
| PATTERN | K1 Cutoff, K2 Resonance, K3 Env Mod. Wheel changes Seed by one per detent, or sixteen with Shift. Turning locks a Random seed. Push presses Regen. Shift + Push presses Seed Back. Minus / Plus changes group. |
| VOICE | Tuning, Cutoff, Resonance, Env Mod, Decay, Accent, Waveform, Volume |
| TONE | Filter, Peak Spread, Drive, Slide, Slide Time, Accent Decay |
| TB-3PO | Mode, Seed Mode, Seed, Regen, Seed Back, Mutate, Density, Scale, Root, Octave, Steps, Gate, Transpose |
| CLOCK | Clock Div, Swing, Reset |

On a list the wheel moves one row per detent and stops at the ends. Push enters or leaves editing. On Regen and Seed Back it presses the button. While editing, a detent moves a choice or integer by one, or a continuous value by 1/128 of its range. Shift gives 1/1024 for a continuous value. Minus / Plus moves an edited choice or integer by one and a continuous value by 1/16. Outside editing it jumps to the first row of the neighbouring group.

The pots turn the selected row and the two below it. Buttons and missing rows have no pot binding. The card rail uses the track colour. A hollow square shows a known physical pot position waiting for pickup. Shift replaces the cards with the controls for the current page and edit state. Melodee owns pot takeover, its soft keys, and Shift + wheel when the host uses it to fine-edit the last touched pot.

VOICE and TONE plot the selected filter from 20 Hz to 20 kHz on a logarithmic axis. The vertical range is −36 to +24 dB. The curve uses the synth's response equations at the current settings, before envelope and accent modulation. The dotted vertical marker and its foot show the frequency reported by Cutoff. Twin Peak marks its lower edge. TB-3PO and CLOCK show the published seed and a small score. CLOCK divides each two-step interval at Swing, with a fixed halfway tick for reference. [Parameter descriptions](../README.md#controls)

## Art and previews

PATTERN uses the yellow groove print. VOICE and TONE use its mint and lavender inks. TB-3PO has the clean pink checkerboard, and CLOCK uses the same plate in amber. Each 320 × 50 header is compiled at panel resolution with four bits per pixel. Page captions and hexadecimal digits use Barlow Condensed Bold Italic. The live readouts use Departure Mono. [Source art and prompts](../assets/explore/rave/prompts.md) · [Groove plate](../assets/art/groove-header.png) · [Checkerboard plate](../assets/art/checker-header.png) · [Credits](../THIRD_PARTY.md)

The contact sheet covers 20 fixture states, including a dense 32-step phrase, a pending seed in MIDI Notes mode, and the screen before the first clock. Each state has native RGB, nearest-neighbour 3×, RGB565 and a 320 × 240 Edit composite. The strip follows steps 4–7. The 2× GIF follows one complete cycle. [Contact sheet](../assets/previews/contact-sheet-2x.png) · [Step strip](../assets/previews/step-advance-strip.png) · [16-step loop](../assets/previews/step-advance.gif)

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

All ten host reports selected `screen=1` and recorded the knob bindings. The playing states in the gallery use an explicit fixture pattern. [Host log](../assets/previews/melodee/host.txt)

## Bench and changed pixels

Measured 2026-11-21. Each entry is the mean elapsed time for 2,000 calls to `update` plus `paint`. Event preparation and the pixel comparison used for reporting are outside the timer. The M4 Max runs native Apple Clang at `-O3`. The arm64 Colima VM runs GCC 12 at `-O3 -mcpu=cortex-a55` on the same Mac.

| Operation | M4 Max, ms | Linux VM, ms |
|---|---:|---:|
| Full PATTERN, 16 steps | 0.025568 | 0.031122 |
| Partial step, 16 steps | 0.012750 | 0.017174 |
| Full PATTERN, 32 steps | 0.028370 | 0.034263 |
| Partial step, 32 steps | 0.015660 | 0.018904 |
| Full VOICE, Twin Peak | 0.033723 | 0.034502 |
| Partial VOICE cursor move | 0.011232 | 0.012892 |
| Full VOICE while Resonance changes | 0.036552 | 0.039810 |
| Partial VOICE while Resonance changes | 0.027814 | 0.029297 |
| Idle | 0.000020 | 0.000042 |

Peak changed pixels between successive frames, counted on the plug-in canvas across a full cycle or a traversal of every list row. Both targets produced the same counts.

| Change | Maximum pixels |
|---|---:|
| Idle | 0 |
| 16-step advance | 2,009 |
| 16-step advance in Notes mode | 1,319 |
| 32-step advance | 1,704 |
| Dense microtonal 32-step advance | 1,701 |
| TB-3PO miniature advance | 855 |
| CLOCK miniature advance | 832 |
| VOICE cursor | 6,122 |
| TONE cursor | 6,228 |
| TB-3PO cursor | 6,214 |
| CLOCK cursor | 6,048 |

The paint code retains the last plug-in pixels separately from host overlays. For a clock advance, it compares the span covering the previous and current columns, including the overhang of the small pitch labels. A whole paint covers all 320 × 218 pixels and respects the host stride. Time alone leaves the canvas untouched.

The Linux build passed 732,233 host checks including `testScreen`, 512 sequencer reference checks and all 74 sound fingerprints from main. The arm64 CLAP measures 264,752 bytes, up 24 bytes from the 0.4.2 build. Both targets passed the 20 image hashes, all sixteen hex glyphs in all four positions, input, bindings, allocations, lengths 0–32 and idle, stride and whole-paint checks. The tests also compare partial playhead jumps to whole redraws on both normal and microtonal phrases.

After RGB565 conversion, the muted note text on the playhead measures 4.84:1, selected row text 8.02:1 and pot tags 9.87:1. The body and compact note capitals are eight native pixels tall. Essential information clears the host’s top two and bottom three overlay rows.

## Engine handoff and hardware status

The existing Screen calls, Host, Edits, parameter IDs, PatternView and seqlock keep their contract. The supplied response functions and pattern fields cover this page. It needs no additional engine data. This screen revision builds on main’s existing clock and silent-instrument fixes.

Physical MPC testing is outstanding for the complete page, including on-device redraw time, SPI transfer time while playing, panel colour and readability, and the feel of encoder edits and pot pickup. The RGB565 previews, desktop timing runs and Melodee captures are the available checks.
