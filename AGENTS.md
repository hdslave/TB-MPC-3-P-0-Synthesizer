# Notes for coding agents

**hdslave Slink** is a 303-style bass synth played by a port of the TB-3PO random acid sequencer: a CLAP instrument for hdslaveOS on MPC Sample, freesp16 on SP-16, macOS and Windows. The MPC and SP-16 targets share the root engine. The desktop build lives in `mac-clap/`. Its CLAP id is `com.hdslave.tb-mpc-3-p-0`, and it builds to `build/Slink.clap`.

## Read the workspace guide first

On the author's machine, this repository sits in one folder beside the other hdslave projects. That folder's `../AGENTS.md` and `../Workflow/` hold the shared workflow:
- building in the Linux VM;
- what hdslaveOS does to plug-ins;
- screen pages;
- reviews;
- publishing, the hub repository and staging for the SD card.

Read `../AGENTS.md`, then `../Workflow/plugins.md` and `../Workflow/host-facts.md`, before you change anything. The rules below hold either way.

## Build and test

- `./build.sh` builds the plug-in in a Linux arm64 container (image `melodee-builder`, Colima VM `melodee`). It runs `make all test`: the host tests in `tests/host.cpp` and the sound's fingerprint. Pass make targets to run others (`./build.sh scales` regenerates `src/scales.h` with `tools/gen_scales.py`).
- The freesp16 toolchain’s `make sp16-test` target exercises the ARMv7 SP-16 build under QEMU. See the README for the target and panel controls.
- `./build.sh sanitize` runs the tests and the fingerprint's scenarios under AddressSanitizer and UBSan.
- `tools/bench.sh` counts the instructions of a 128-frame block with callgrind. A block is about 4.8 M cycles on one of the MPC's Cortex-A55 cores.
- Check the build's own exit code: `./build.sh | grep …` hides a failure.

## Rules

- **Fixed for saved projects.** Parameter ids, preset keys and choice orders never change once released, because saved projects and presets refer to them.
  - Add new ones at the end.
  - Ranges stay as they are too: the host stores bindings normalized to the range.
- **The sound is held to the bit** by `tests/fingerprint.txt`.
  - A refactor or speed-up must leave every hash unchanged. With `-ffp-contract=fast`, even reshaping a loop can change the last bit, so check after every edit.
  - Only a change meant to change the sound runs `make fingerprint-update`, and its commit says which hashes moved and why.
- **Real-time and determinism.** The output is the same at every block size. No allocation, locks or file I/O on the audio thread.
- **Notes** must survive how hdslaveOS sends them:
  - every MIDI event of a block arrives at frame 0;
  - a re-pressed key arrives as on, on, off;
  - CC 120 and 123 arrive at 0 on every channel when the host silences.
- **Commits** are made as `hdslave <111106843+hdslave@users.noreply.github.com>`, with Conventional Commits. Never push, publish or stage for the card without the owner's word.
- **Other agents' work.** Others may be working here. Check `git status` and `git worktree list`, and never edit, commit or reset files you did not change.
- **Names and art.** No trademarked names in the plug-in's names, presets, UI text or art. Art is original; fonts are OFL, with their licences in `licenses/`.
- **Read the README** before you change the sound. It lists the sources, the inferred values and the deliberate deviations. Keep it current: version, costs, and "not yet run on the MPC" until it has been.

## This plug-in

- **The applet reference.** `tests/reference.cpp` ticks the original applet's `Controller()` (`tests/applet.h`, copied from `tools/oc-source/`) against the plug-in's engine. Rerun it after any change to `src/tb3po.h` or `src/engine.h`: it must report 0 failed.
- **Firmware quirks kept on purpose.** Upward slides don't move, the quantizer's extra octave drop, octave bits drift, and the first step after a reset is index 1. Don't "fix" them without asking. The README lists them, and the deliberate deviations too.
- **The pads.** The MPC build embeds Melodee's drum map (`padMapJson` in `src/tbmpc.cpp`, kept by `padsJson()`), so hdslaveOS plays the pads as Drums: A01 - A16 send 36 - 51 whatever the octave or key, and - / + move the bank (B 52 - 67, C 68 - 83, D 84 - 99). In Mode: Sequence every key follows the bank rule (`padOf` in `src/params.h`): slot (key - 36) mod 16 is the step, played once (`Engine::note`, `playPad`), the bank (held to -2 .. +3: A - D are 0 .. +3 octaves) an octave per bank, without moving the play head, seed or transposition; no key transposes. Mode: Notes is chromatic, every key at its own pitch, exactly as 0.8.2 (52 plays 52): the bank rule is the step pads' alone (the user's choice, "Option A", 2026-11-27). The SP-16 build keeps its window of step pads from 60, transposing notes and a chromatic Mode: Notes (`padBanks` false); `mac-clap/` has its own engine. `testStepPads` and `testPadMap` in `tests/host.cpp` hold it.
- **The page.** It is designed on branch `screen`, which owns `src/screen.h`, `src/ui/`, `assets/`, `tools/ui_preview/` and `docs/screen.md`. The glue in `src/tbmpc.cpp`, the DSP, `tests/` and `Makefile` belong to `main`'s engine work.
- `src/ui/page.h` began as a copy of Anvil's and has since diverged (114 changed lines when last diffed): they are not in sync, and neither is a copy of the other now.

## Release packaging

Every release ZIP includes the full repository `LICENSE.txt` beside the third-party notices, outside signed plug-in bundles. Preserve both when packaging the plug-in for another project.
