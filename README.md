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

VCO ──► 4-pole diode ladder (cutoff: Cutoff, Env Mod x MEG, accent sweep) ──► VCA (VEG + accent) ──► Volume ──► out
```

## Controls

In the order Melodee's Edit page lists them, three to a page.

### The voice (the 303's panel)

| Control | Range | What it does |
|---|---|---|
| Tuning | ±12 semitones | The VCO's pitch. 0 V from the sequencer is C2 (65.4 Hz). |
| Cutoff | 130 Hz – 5.1 kHz | The diode ladder's cutoff before the envelope and accent open it. |
| Resonance | 0 – 100 % | The ladder's feedback, up to just short of self-oscillation; the passband thins (about -6 dB) as on a 303. It also slows the accent sweep. |
| Env Mod | 0 – 100 % | How far the MEG opens the filter (up to 4.5 octaves) and how far it lowers the cutoff's floor (up to 1.3 octaves), as on the 303. |
| Decay | 200 ms – 2 s | The MEG's decay (to -40 dB). An accented step always takes the shortest. |
| Accent | 0 – 100 % | How much an accented step (TB-3PO's 5 V gate) lifts the VCA (up to +7 dB) and opens the filter through the accent sweep. Accents in a row at high resonance build up. |
| Waveform | Saw / Square | |
| Volume | 0 – 100 % | |
| Slide | TB-3PO / 303 | **TB-3PO**: the applet's slide as the module plays it (see below). **303**: a fixed-time glide both ways (time constant 20 ms, there in about 60 ms). |

The VCA's own envelope is fixed, as on the 303: it opens in 1 ms, decays slowly while the gate is held and closes
in a few ms when the gate shuts.

### TB-3PO

| Control | Range | The applet's |
|---|---|---|
| Mode | TB-3PO / Notes | — (new). **TB-3PO** plays the voice. **Notes**: your MIDI notes play it instead (see [Mode: Notes](#mode-notes)); TB-3PO goes on counting unheard, so switching back lands in time. |
| Seed Mode | Random / Locked | The die / the lock. **Random**: every reset picks a new seed (a new pattern); the Seed shows it. **Locked**: the seed stays. |
| Seed | 0000 – ffff | The 16-bit seed, shown in hex as on the module. A new seed plays from the next clock. |
| Regen | button | Turning the encoder past the die or the lock: a reset now. |
| Seed Back | button | — (new): back to the seed before, and Seed Mode to Locked so the next Play keeps it. It remembers the last 16 seeds whose patterns played (a knob turned through seeds remembers only the ones that played); press again to go further back. |
| Density | -7 – +7 | Density. Its distance from 0 sets how many steps play (0: 10 %, ±7: all); left of +1, fewer different pitches and more repeated notes (-7: the root alone). |
| Scale | 128 scales | The firmware's quantizer scales, from Semitone on (its four user scales and Off are left out). Starts on Gunakri, as the applet does. |
| Root | C – B | The root, a step of the scale (a scale with fewer than 12 notes has that many roots). |
| Octave | -3 – +3 | The octave offset (one span of the scale). |
| Steps | 1 – 32 | The pattern's length. |
| Transpose | ±36 semitones | CV 1: added to the last MIDI note played (C4 plays the pattern as it is) and quantized to the scale from C, as the applet does with its transpose CV. |

CV 2 (density) has no control of its own: a host's automation of Density does what it did.

### The clock

| Control | Range | What it does |
|---|---|---|
| Clock Div | 1/2 – 1/64, with triplets | One clock a step, on every multiple of this from the song's top while the host plays. 1/16 by default. |
| Swing | 50 – 75 % | The second clock of each pair comes late, at that share of the pair, as an MPC's swing does (66 %: a triplet shuffle). A swung step's gate is half of its own length. Melodee's own swing moves only its notes, so set this to match. |
| Reset | Play / 1, 2, 4, 8 Bars / Off | The applet's Regen input. **Play**: a reset when the host starts. **N Bars**: a reset on every N bars from the song's top; a Play or a jump between them places the pattern as if it had run from the last one. **Off**: only Regen resets. |

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
| Worst: every step gated at 1/64, full resonance, accent and decay, sliding | 25,988 | 0.54 % |
| Typical: the default patch | 16,460 | 0.34 % |
| Stopped | 567 | 0.01 % |

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
  where the pattern puts it at several tempos and rates, gates half a step long, stop, restart and jumps, the same
  output for any block size, Random and Locked seeds and Regen, Seed Back (locked and random, twice, with nothing to go
  back to), Swing (notes and gate lengths), Mode: Notes (a note's pitch, a glide without a new note, back to a held
  note, the accent at velocity 100, TB-3PO silent, a switch back), transposing by MIDI note and by knob, the accent,
  Cutoff, both slides, bounded output at random settings, and the demo renders of the presets.
- `tests/fingerprint.cpp` renders 45 fixed scenarios (each preset, the bench's patches, 24 random patches with Regen,
  MIDI and knobs moving, other block sizes and rates, stop and jumps with both kinds of reset, Swing, Seed Back, a
  MIDI line in Mode: Notes and a switch between the modes), every seed locked,
  and hashes every output sample's bits against `tests/fingerprint.txt`: work meant to keep the sound, such as a
  speed-up, must leave every hash as it is. A change meant to change the sound updates them with
  `make fingerprint-update` (inside the build image). The hashes hold for Melodee's build image and these flags.

## Credits

- TB-3PO: Logarhythm, for the Hemisphere Suite (Benisphere Suite), MIT license.
- The quantizer and its scales: Emilie Gillet (Mutable Instruments Braids), re-implemented by Bryan Head; the
  Hemisphere framework: Jason Justian. MIT license; see `licenses/MIT-oc-firmware.txt`.
- The diode ladder's linear model: Vadim Zavalishin, *The Art of VA Filter Design*.
- CLAP headers: MIT license, `third_party/clap/LICENSE`.

TB-303 is a trademark of Roland Corporation and MPC of inMusic Brands; this project is not affiliated with either.
