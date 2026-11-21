# TB-MPC-3-P-0 screen

The PATTERN page puts the four-digit seed above a score. A filled notehead opens the gate. A hollow notehead shows the pitch stored on a rest. A square on ACC marks an accent and an arrow on SLD marks a slide **out of** that step. The rightmost arrow leads back to the first step. One phosphor column follows playback, including rests.

Pitches use C2 for zero CV. The notehead follows the published pitch in 1/128-semitone units. Its small label gives the nearest semitone, including the octave. Sixteen steps fit on one staff. Steps 17–32 wrap to a second staff with the same column spacing and pitch range. Step and division readouts sit beside the transport state. The line below the seed shows scale, root, octave and density.

Dialling ahead changes the large digits to NEXT. PENDING identifies the seed whose score is still visible. The score advances to the new seed when the audio thread publishes it. In Notes mode the heading reads “MIDI NOTES / SEQ MUTED” and the playhead keeps counting. Before the first pattern snapshot the page can show the seed and requested length and displays “Awaiting clock” in the score.

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

The Folio study gave the seed and the uninterrupted score the most room at panel size. Its folded-ribbon mark survives reduction to 28 × 11 pixels. The alternatives organize the same pattern as a tracker ledger or a punched matrix. [Folio](../assets/explore/folio-320.png) · [Ledger](../assets/explore/ledger-320.png) · [Matrix](../assets/explore/matrix-320.png) · [Art and font credits](../THIRD_PARTY.md)

The contact sheet shows all 17 named fixture states. Each state has native RGB, nearest-neighbour 3×, RGB565 and a 320 × 240 Edit composite. The four-frame strip follows steps 4–7. The GIF follows a whole cycle. [2× contact sheet](../assets/previews/contact-sheet-2x.png) · [Step strip](../assets/previews/step-advance-strip.png) · [16-step loop](../assets/previews/step-advance.gif)

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

The last command runs the built plug-in through the sibling Melodee screen checkout, mounted read-only. Set `MELODEE_SCREEN_TREE` to use a different path to that build. It captures the first six prescribed states, then exercises the two other filters and both sequencer lists.

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
| Full PATTERN, 16 steps | 0.021838 | 0.025256 |
| Partial step, 16 steps | 0.019475 | 0.021346 |
| Full PATTERN, 32 steps | 0.023855 | 0.027406 |
| Partial step, 32 steps | 0.018296 | 0.020982 |
| Full VOICE, Twin Peak | 0.030854 | 0.034304 |
| Partial VOICE cursor move | 0.012697 | 0.014971 |
| Full VOICE while Resonance changes | 0.034878 | 0.039594 |
| Partial VOICE while Resonance changes | 0.029200 | 0.032226 |
| Idle | 0.000024 | 0.000049 |

Peak changed pixels between successive frames, counted on the plug-in canvas across a full cycle or a traversal of every list row. Both targets produced the same counts.

| Change | Maximum pixels |
|---|---:|
| Idle | 0 |
| 16-step advance, sequencer or Notes | 2,704 |
| 32-step advance | 1,236 |
| TB-3PO miniature advance | 758 |
| CLOCK miniature advance | 719 |
| VOICE cursor | 6,386 |
| TONE cursor | 6,441 |
| TB-3PO cursor | 6,485 |
| CLOCK cursor | 6,249 |

The page retains its own last pixels and renders invalidated regions into a fixed scratch buffer. A partial paint compares these with the retained image, then copies only changed spans to the host. Whole paints cover all 320 × 218 pixels and respect stride. Time alone leaves the canvas untouched. An active score asks for paints while its playhead is moving.

The full build passed 313,532 host checks including `testScreen`, 512 sequencer reference checks and all 72 sound fingerprints. The new CLAP measures 264,728 bytes, an increase of 48 bytes over a 264,680-byte build of the same base, `0d33460`, with the previous page. The preview test checks all 17 image hashes on both targets, cursor and edit behaviour, knob bindings, seed wrap, an unchanged pending score, strides, untouched idle buffers and full redraws. It also checks updates across every parameter and step counts from an unpublished snapshot through 32. The screen uses fixed storage. The preview host counts C++ allocations during the screen calls.

After RGB565 conversion the lowest contrast across the four text inks and five text backgrounds is 5.42:1. The body capitals and the compact note labels are eight native pixels tall. The top two and bottom three rows carry no essential information.

## Engine handoff and hardware status

The existing Screen calls, Host, Edits, parameter IDs, PatternView and seqlock keep their contract. The supplied response functions and pattern fields cover this page. It needs no additional engine data. The branch includes the silent-instrument glue fix from main.

Physical MPC testing is outstanding for the complete page, including on-device redraw time, SPI transfer time while playing, panel colour and readability, and the feel of encoder edits and pot pickup. The RGB565 previews, desktop timing runs and Melodee captures are the available checks.
