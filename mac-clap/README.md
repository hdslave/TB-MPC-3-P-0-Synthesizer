# hdslave Slink — macOS (and Windows) Desktop CLAP Plug-in

Separate desktop CLAP build of **hdslave Slink** for **FL Studio** (2024+) and any other CLAP host on macOS (Apple Silicon / Intel) and Windows (x64).

![Slink acid bass sequencer and voice controls during playback](assets/desktop-preview.gif)

Desktop 0.7.0 uses the SP-16 Thorn Crown artwork on one resizable panel. The chrome crest and camshaft move above the live score. Voice, Tone, Sequencer and Clock keep all 34 host parameters in reach, down to a 960 × 540 window.

- Start playback in the host to advance the cobalt playhead. **CODE** encodes the pitch, gate, accent and slide under each step. Click a step to inspect it. Patterns longer than 16 steps have two banks and a **Follow** switch.
- Click the enamel **Regen** button for a new pattern. Drag the seed under the Slink wordmark to set a seed. Click **Key** or **Scale** to choose from a list.
- Drag or scroll a control to edit it. Hold Shift or enable **Fine** for small adjustments. Clicking a choice opens its menu. Double-click a control to reset it, or right-click for reset and MIDI mapping.
- Drag the filter response horizontally for cutoff and vertically for resonance. Drag across the score for transpose and vertically for density. These gestures send both parameters to the host for automation.
- Click the crest to jump to a different camera angle. **Motion off** holds the artwork while playback and parameter displays continue to update.

Build from this checkout to use the Thorn Crown panel. The [0.6.0 release](https://github.com/hdslave/hdslave-Slink-Synth/releases/tag/v0.6.0) contains the earlier desktop downloads.

Slink 0.6.0 is the new name of TB-MPC-3-P-0. Existing projects use the same CLAP ID and factory preset keys. On upgrade, replace the previous plug-in in place, keeping its installed filename if the host stores paths. Install one copy per host.

Version 0.5.5 added a sine pitch LFO on **Tone** and a separate sine cutoff LFO at the bottom of **Voice**.
Both Speed controls follow the host BPM and snap to 1/1, 3/4, 1/2, and 1/3 beat per cycle.

## Build & Install (macOS)

Requires Xcode Command Line Tools and CMake 3.20+ (Ninja optional):

```bash
cd mac-clap
./build-macos.sh
./install-macos.sh
```

`install-macos.sh` builds, ad-hoc signs, tests, and installs `Slink.clap` to `/Library/Audio/Plug-Ins/CLAP/` (pass `--user` to install to `~/Library/Audio/Plug-Ins/CLAP/`). When upgrading an existing installation, the installer keeps the old bundle filename for project recall. Quit and reopen the host after installing.

## Build (Windows x64)

```powershell
cd mac-clap
.\build-windows.ps1
```

Copy `build\windows\clap\Release\Slink.clap` to `C:\Program Files\Common Files\CLAP\`.


## Desktop artwork

The plug-in embeds the Thorn Crown camera film, camshaft loop, chrome wordmark, enamel smiley and satin dial. The score, code strip, filter curve and controls are drawn from live instrument values. The bundled art bank is sufficient for macOS and Windows builds. To repack it from the original motion-study exports, run the following from the repository root with Python, Pillow and FFmpeg available.

```sh
python3 mac-clap/tools/bake_thorn.py camera-film.mp4 "Camshaft loop.mp4" assets.json
```

The interface uses Barlow SemiBold and Barlow Condensed Bold Italic under the SIL Open Font License. Artwork and font credits are in [THIRD_PARTY.md](../THIRD_PARTY.md). The embedded JPEG and PNG decoder is Sean Barrett’s stb_image 2.30 under the MIT license. [Source and license](third_party/stb/stb_image.h)
