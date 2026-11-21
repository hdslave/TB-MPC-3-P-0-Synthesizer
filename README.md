# TB-MPC-3-P-0 Synthesizer

A TB-303-style bass synthesizer as a CLAP plug-in, played by a port of **TB-3PO**, the random acid-pattern
sequencer of the Ornament & Crime module's Hemisphere firmware (as in the Benisphere Suite). Built for the Akai
MPC Sample running Melodee (64-bit ARM Linux, Cortex-A55); it runs in any CLAP host on Linux arm64. Hosts list it as
**hdslave TB-MPC-3-P-0** (id `com.hdslave.tb-mpc-3-p-0`).

TB-3PO's pattern generator, random numbers, quantizer and step, gate and slide logic are the applet's, line for
line: the same seed, density and scale give the same pattern as on the module. The tests hold the port against the
applet's own code run tick by tick (see [Tests](#tests)). The host's transport stands in for the module's clock and
reset inputs, the host's MIDI notes for its transpose CV.

## Signal flow

```
host transport ──clock (Clock Div)──► TB-3PO ──pitch CV (with its slide)──► VCO (saw or square)
               ──reset (Reset)─────►        ──gate (3 V; 5 V = accent)──► MEG, VEG, accent
MIDI note ──transpose (as CV 1)───►

VCO ──► Filter: 303 diode ladder / Twin Peak / 18 dB (cutoff: Cutoff, Env Mod x MEG, accent sweep)
    ──► VCA (VEG + accent) ──► Drive ──► Volume ──► out
```

On the MPC it draws its own page on the screen (see [The screen](#the-screen)).

## Controls

In the order Melodee's Edit page lists them, three to a page.

### The voice (the 303's panel)

| Control | Range | What it does |
|---|---|---|
| Tuning | ±12 semitones | The VCO's pitch. 0 V from the sequencer is C2 (65.4 Hz). |
| Cutoff | 7 octaves | The filter's cutoff before the envelope and accent open it, calibrated as the 303's service notes set the 303's: at its middle, with Resonance full, the resonant peak is at 500 Hz, in each filter. It reads what you hear (see [Filter](#filter)), so its text moves with Resonance and Filter. |
| Resonance | 0 – 100 % | The filter's feedback: the 303's and Twin Peak's up to just short of self-oscillation, 18 dB's into it at the top (from 86 %); the passband thins (about -6 dB) as on a 303. It also slows the accent sweep. |
| Env Mod | 0 – 100 % | How far the MEG opens the filter (up to 4.5 octaves) and how far it lowers the cutoff's floor (up to 1.3 octaves), as on the 303. |
| Decay | 200 ms – 2 s | The MEG's decay (to -40 dB). An accented step takes Accent Decay's (by default the shortest, as on the 303). |
| Accent | 0 – 100 % | How much an accented step (TB-3PO's 5 V gate) lifts the VCA (up to +7 dB) and opens the filter through the accent sweep. Accents in a row at high resonance build up. |
| Waveform | Saw / Square | |
| Volume | 0 – 100 % | |
| Slide | TB-3PO / 303 | **TB-3PO**: the applet's slide as the module plays it (see below). **303**: a glide both ways, as long as Slide Time. |
| Slide Time | 15 – 600 ms | — (new): the 303 glide's time to arrive (95 %), for Slide: 303 and the glides of Mode: Notes. 60 ms (a time constant of 20 ms) is the 303's and the default. The applet's slide keeps its own speed. |
| Accent Decay | 200 ms – 2 s | — (new): the MEG's decay on an accented step. Its default, the shortest, is the 303's, whose accented steps always take it; longer lets an accent's sweep hang. |
| Drive | 0 – 100 % | — (new): after the VCA, a soft clipper driven up to +30 dB, anti-aliased (first-order antiderivative). Its level is made up so a playing pattern stays within about -2 to +3 dB of the dry voice; a held, dark note (few harmonics of its own) still grows louder as it gains them, up to about +12 dB. 0 is no drive at all. |
| Filter | 303 / Twin Peak / 18 dB | — (new): see [Filter](#filter). |
| Peak Spread | 0.5 – 4 octaves | — (new): Twin Peak's upper peak, this far over the lower (Cutoff's). |

The VCA's own envelope is fixed, as on the 303: it opens in 1 ms, decays slowly while the gate is held and closes
in a few ms when the gate shuts.

### Filter

- **303**: the 303's 4-pole diode ladder (as before). Its stages are coupled, so with Resonance at 0 its passband
  ends at about a ninth of its nominal frequency (an octave or so of 6 dB/octave slope first); from about 10 %
  Resonance on, the feedback flattens it out close to the resonant peak. Its range is 38 Hz – 4.9 kHz nominal: the
  top where it was before the calibration, more room at the bottom. (The presets kept their sound: their Cutoff
  moved to where the same filter now sits. So do projects saved by 0.1 - 0.3: their Cutoff is moved as they load. A
  host's automation of Cutoff recorded before 0.4 is not, and plays about an octave darker.)
- **Twin Peak**: two 4-pole transistor-style ladders on the same input, each with its own feedback and clip, the
  lower subtracted from the upper, after Rob Hordijk's TwinPeak arrangement. Below both they cancel; between them the
  band passes, a resonant peak at each edge (Cutoff the lower, Peak Spread the upper); above, 24 dB/octave. At the
  very top the lower peak stops half an octave under the upper's ceiling, so the band never closes. Vowel-like
  with resonance, thin in the bass by design. About as loud as the 303 filter (-1.6 dB on the tests' pattern).
- **18 dB**: three equal stages in a loop, the 18 dB/octave ladder of acid filters such as Doepfer's A-103. Brighter
  than the diode ladder with Resonance low, and it self-oscillates at the top of Resonance (from 86 %), at about
  the peak's pitch.

Calibration: the 303's service notes ask for the resonance to sit at about 500 Hz with Cutoff at its middle and
Resonance full. Each filter is placed so (Env Mod at 0; the tests play C1 and find the strongest harmonic at 490 Hz,
the nearest to 500 of C1's, in all three). Cutoff's text is the frequency you hear as the edge, worked out from the
filter's response with Resonance as it is and Env Mod at 0: the top of the passband (3 dB under its highest point)
for 303 and 18 dB, the band's lower edge for Twin Peak. So with Resonance at 0 a 303 Cutoff at its middle reads
51 Hz, at 60 % 490 Hz. (Env Mod lowers the floor further, as on the 303; the mod wheel raises it.)

### TB-3PO

| Control | Range | The applet's |
|---|---|---|
| Mode | TB-3PO / Notes | — (new). **TB-3PO** plays the voice. **Notes**: your MIDI notes play it instead (see [Mode: Notes](#mode-notes)); TB-3PO goes on counting unheard, so switching back lands in time. |
| Seed Mode | Random / Locked | The die / the lock. **Random**: every reset picks a new seed (a new pattern); the Seed shows it. **Locked**: the seed stays. |
| Seed | 0000 – ffff | The 16-bit seed, shown in hex as on the module. A new seed plays from the next clock. |
| Regen | button | Turning the encoder past the die or the lock: a reset now. |
| Mutate | 0 – 100 % | — (new): at a reset that brings a new seed (Seed Mode: Random), only this share of the steps take the new seed's pattern; the rest keep the one playing, so the line changes a little each time. Which steps change comes from the new seed. 0 % is the applet. Seed and Seed Back give a seed's own pattern, not the mutated mix. |
| Seed Back | button | — (new): back to the seed before, and Seed Mode to Locked so the next Play keeps it. It remembers the last 16 seeds whose patterns played (a knob turned through seeds remembers only the ones that played); press again to go further back. |
| Density | -7 – +7 | Density. Its distance from 0 sets how many steps play (0: 10 %, ±7: all); left of +1, fewer different pitches and more repeated notes (-7: the root alone). |
| Scale | 128 scales | The firmware's quantizer scales, from Semitone on (its four user scales and Off are left out). Starts on Gunakri, as the applet does. |
| Root | C – B | The root, a step of the scale (a scale with fewer than 12 notes has that many roots). |
| Octave | -3 – +3 | The octave offset (one span of the scale). |
| Steps | 1 – 32 | The pattern's length. |
| Gate | 10 – 95 % | — (new): how long a step's gate stays open, as a share of the step (50 %: the applet's half clock). |
| Transpose | ±36 semitones | CV 1: added to the last MIDI note played (C4 plays the pattern as it is) and quantized to the scale from C, as the applet does with its transpose CV. |

CV 2 (density) has no control of its own: a host's automation of Density does what it did.

Pitch bend (±2 semitones) and the mod wheel (up to 3 octaves more cutoff) work in either mode: from a keyboard, or from
the MPC's faders (Melodee's fader can send both to a plug-in track).

### The clock

| Control | Range | What it does |
|---|---|---|
| Clock Div | 1/2 – 1/64, with triplets | One clock a step, on every multiple of this from the song's top while the host plays. 1/16 by default. Changed while playing, the clocks count on the new grid from there; one due on that frame comes. |
| Swing | 50 – 75 % | The second clock of each pair comes late, at that share of the pair, as an MPC's swing does (66 %: a triplet shuffle). A swung step's gate is half of its own length. Melodee's own swing moves only its notes, so set this to match. |
| Reset | Play / 1, 2, 4, 8 Bars / Off | The applet's Regen input. **Play**: a reset when the host starts. **N Bars**: a reset on every N bars from the song's top; a Play or a jump between them places the pattern as if it had run from the last one. **Off**: only Regen resets. |

A loop that wraps inside a block is told only at the next block (Melodee tells the song's place at its own blocks,
even when it plays a plug-in in smaller parts), so the clocks run on to the loop's end, which is its start: the clock
played there stands for the downbeat, and the pattern does not step twice (a bar reset the downbeat would have made
comes when the host tells, at most a block late). A tempo change inside a block moves the host's place a few frames
from where the clocks counted: followed as it is, no clock played twice.

## Mode: Notes

Melodee's notes (its roll, its pads, a keyboard) play the voice, the way MIDI-played 303-style synths do:

- A note on its own opens the gate (both envelopes start) at its pitch: C2 (MIDI 36) is the sequencer's 0 V.
- A note played while another is held glides to its pitch with the 303 glide (whatever Slide is set to) and keeps the
  gate open: no new envelope, as a 303's slide.
- Velocity 100 or more accents the note.
- Letting go of the newest note glides back to the newest one still held; letting go of the last closes the gate.
- The host stopping does not stop a held note. Switching Mode stops the note sounding.

Tuning, the voice's knobs and Accent work as in TB-3PO mode; Transpose, Octave and the pattern's settings do not.

## How TB-3PO plays

A reset makes the pattern from the seed (a new seed first, in Random mode) and goes back to step 0. Each clock moves
to the next step, so the first step played after a reset is the pattern's second (index 1), as on the module. A step
plays its note if it is gated; a gate lasts half a clock, unless the step slides, when it stays open into the next
step (legato: no new envelope). Accented steps send the voice a 5 V gate. The pattern holds 32 steps of notes,
gates, slides, accents and octave flags; Steps sets how many play.

The pattern is made again from the seed at the next clock when Density, the scale's number of notes or the seed
changes. Root, Octave, Transpose and a scale of the same size only move its pitches.

### What the module does, kept

These come from the applet's code and are kept so the plug-in plays what the module plays:

- **Slides only go down.** The applet's slide was meant as an exponential glide; its integer arithmetic moves down by
  3/128 semitone each 1/16666 s (an octave in about 31 ms) and never moves up: a step slid into from below holds the
  step before's pitch. Slide: 303 glides both ways.
- **Some low notes drop an extra octave.** The quantizer gives a note exactly a whole number of octaves below the
  root (an octave-down root note, or the root with Octave below 0) one octave too low, plus whatever note a longer
  scale chosen earlier left in its table.
- **Octave flags drift from their steps.** The octave-up/down bits move on only for new notes, but are read by step.

### Where it differs from the module, and why

- **Steps land on the clock.** The applet waits 2 ms after a clock for its CV inputs to settle; here the inputs are
  exact.
- **The gate's length comes from the tempo.** The applet measures half a clock from the last two clocks: the same
  while the tempo holds (on the module the first note after the clock starts is long).
- **A new seed plays from the next clock.** On the module a typed-in seed waits for a Regen pulse (its later
  firmware changed this too).
- **The host stopping shuts the gate.** The module would hold a sliding step's gate until the next clock.
- **Root keeps its knob.** The applet moves its root setting into a shorter scale's range; here the knob stays and
  the root used is limited.

## The screen

In hdslaveOS (melodee-hdslave until 2026-11-21), the plug-in draws its own page in place of the parameter list:

- **PATTERN**, TB-3PO's own screen: the pattern as a full-width pitch roll across the top (all 16 or 32 steps on
  one timeline, pitches named on the left, a filled head for a gated step and a hollow one for a rest's pitch,
  accents marked on ACC, slides drawn as a line to the next pitch and marked on SLD, a playhead following the
  clock), and below it the seed, locked or random, the transport and step, the division, scale, root, octave and
  density ([docs/screen.md](docs/screen.md) has the details). The wheel dials the seed and locks it, so the pattern you dial is kept; it plays from the next clock (until
  then the seed shows pale, marked NEXT, over the pattern still playing); Push is Regen; Shift + Push is Seed Back.
  K1-K3: Cutoff, Resonance, Env Mod.
- **VOICE** and **TONE**: the voice's knobs, beside the filter's live response (Cutoff's reading marked).
- **TB-3PO** and **CLOCK**: the sequencer's, beside the pattern.

On a list the controls are hdslave Machine's: the wheel moves a row; Push edits it (the wheel then changes it: a
choice a step, a knob 1/128, 1/1024 with Shift; Push again leaves), or presses a button; Minus / Plus change group
(while editing, a choice or a whole number a step, a knob a sixteenth of its range); K1-K3 turn the cursor's row and the two under it, with Melodee's takeover
(a hollow mark shows where to move a pot). Hold Shift for these. Another host never asks for the page.

## Presets

| Preset | Seed | Patch |
|---|---|---|
| Default | Random | The applet's start: Gunakri, density +5, 16 steps; a new pattern every Play. |
| Acid Line | 5a17 | Aeolian, density +5, low cutoff, high resonance and accent. |
| Root Pulse | 0303 | Semitone, density -7: every step plays, the root alone, with octave jumps and full accent. |
| Square Lead | beef | Phrygian, square wave an octave up, the 303 glide. |
| Dub Steps | 1d0b | Dorian, density +3, 12 steps an octave down, long decay. |
| Note Bass | — | Mode: Notes: a dark, resonant bass to play from Melodee's notes (glides and accents as above). |

All the TB-3PO presets but Default lock their seed, so they play the same pattern every time.

## Cost

Instructions a 128-frame block (48 kHz), counted by callgrind (`tools/bench.sh`):

| Case | Instructions | Share of a block on one A55 core |
|---|---|---|
| Worst: every step gated at 1/64, full resonance, accent and decay, sliding | 27,780 | 0.58 % |
| The same through Twin Peak (two ladders), Drive full | 37,150 | 0.77 % |
| The same through 18 dB, Drive full | 27,590 | 0.57 % |
| Typical: the default patch | 17,938 | 0.37 % |
| Stopped | 612 | 0.01 % |

The voice runs at the host's rate. A sounding sample costs one exp2 for the cutoff and one division: the diode
ladder's zero-delay solve has five denominators that are polynomials in the cutoff, so their reciprocals come from
one division of their product (about 107 cycles a sample on llvm-mca's Cortex-A55 model, against 140 with a
division each). The pitch's exp2 runs only while the pitch slides or Tuning moves; with the VCA shut only the
envelopes run, then nothing. The voice's sample is compiled into the run's loop rather than called.

## Install

Copy `TB-MPC-3-P-0.clap` to the MPC's card as `Melodee/Plugins/hdslave TB-MPC-3-P-0/hdslave TB-MPC-3-P-0.clap`.

## Build

```sh
colima start melodee            # the Docker VM with Melodee's build images
./build.sh                      # builds build/TB-MPC-3-P-0.clap and runs the tests
tools/bench.sh                  # instruction counts (needs Melodee's melodee-profiler image)
make scales                     # remakes src/scales.h from tools/oc-source/ (any machine with Python 3)
```

`build.sh` runs `make` in Melodee's arm64 build image (Debian bookworm, g++ 12): the plug-in links libstdc++ and libgcc
statically and needs only the MPC's libc and libm.

## Tests

`make test` runs all three:

- `tests/reference.cpp` holds the port against the applet: `tests/applet.h` is the applet's `Controller()` and the
  code it calls, copied from the module's source with only the hardware replaced, ticked 16,667 times a second with
  clock and reset pulses. It checks the random numbers (against Park and Miller's published value) and the
  quantizer; the pattern of 13,860 seed, density and scale settings, field by field; and 192 whole runs with settings
  changed mid-run (density, scale, root, octave, length, the transpose CV, resets every bar): the same steps,
  pitches, slides and gate changes at the same frames, and the slide's pitch every tick. It also checks that a
  mismatch would be caught.
- `tests/host.cpp` loads the plug-in as a host does: ports, parameters and their text, state, presets, every note
  where the pattern puts it at several tempos and rates, gates half a step long, stop, restart and jumps, a loop
  wrapping inside a block and told a block late (every note where a song with no loop puts it, and the very sound of a
  host telling the wrap on its frame, in whole blocks and in Melodee's smaller parts, with Reset at Play and every
  bar), a tempo change inside a block, Clock Div changed on a downbeat, a button pressed through a flush and one
  stamped past its block, the same output for any block size, Random and Locked seeds and Regen, Seed Back (locked and random, twice, with nothing to go
  back to), Swing (notes and gate lengths), Mode: Notes (a note's pitch, a glide without a new note, back to a held
  note, the accent at velocity 100, TB-3PO silent, a switch back), Gate (20 and 90 %), Mutate (each step from the new
  seed's pattern or the old one's, as the new seed picks; 0 and 100 % whole; Locked unchanged), pitch bend and the mod
  wheel (the same sound as Tuning and Cutoff turned as far; all notes off), transposing by MIDI note and by knob, the accent,
  Cutoff, both slides, Drive (brighter as it turns, its level within -3 .. +6 dB of the dry voice), Slide Time (15 ms
  there, 600 ms under half way, 100 ms on), Accent Decay, the filters (the 500 Hz calibration in each, Twin Peak's band,
  their levels, Cutoff's text by Resonance and Filter and the host asked to read it again), the screen's page (it
  draws, its controls set the values the host then hears, its buttons press; pictures in `build/screen-*.png`), bounded
  output at random settings, and the demo renders of the presets.
- `tests/fingerprint.cpp` renders 74 fixed scenarios (each preset, the bench's patches, 24 random patches with Regen,
  MIDI and knobs moving, other block sizes and rates, stop and jumps with both kinds of reset, Swing, Seed Back, a
  MIDI line in Mode: Notes and a switch between the modes, Gate, pitch bend and the mod wheel, Drive, each filter and
  switches between them, Twin Peak's spread moving, 18 dB ringing, Slide Time, Accent Decay, random 0.4 patches, a
  loop wrapping inside a block in whole blocks and in parts), every
  seed locked,
  and hashes every output sample's bits against `tests/fingerprint.txt`: work meant to keep the sound, such as a
  speed-up, must leave every hash as it is. A change meant to change the sound updates them with
  `make fingerprint-update` (inside the build image). The hashes hold for Melodee's build image and these flags.

## Credits

- TB-3PO: Logarhythm, for the Hemisphere Suite (Benisphere Suite), MIT license.
- The quantizer and its scales: Emilie Gillet (Mutable Instruments Braids), re-implemented by Bryan Head; the
  Hemisphere framework: Jason Justian. MIT license; see `licenses/MIT-oc-firmware.txt`.
- The diode ladder's linear model and the zero-delay ladders: Vadim Zavalishin, *The Art of VA Filter Design*.
- The screen's type: Departure Mono, copyright 2022–2024 Helena Zhang, SIL Open Font License 1.1
  (`licenses/OFL-departure-mono.txt`); its glyphs are rasterized into `src/ui/font.h`. The screen extension's header,
  `src/melodee_screen.h`, is melodee-hdslave's, MIT.
- CLAP headers: MIT license, `third_party/clap/LICENSE`.

TB-303 is a trademark of Roland Corporation and MPC of inMusic Brands; this project is not affiliated with either.
