# hdslave Slink desktop CLAP plug-in

Separate desktop CLAP build of **hdslave Slink** for **FL Studio** (2024+) and any other CLAP host on macOS (Apple Silicon / Intel) and Windows (x64).

![Slink’s Razor Choir panel playing the Default preset on macOS](assets/desktop-preview.gif)

[View the Windows panel at full size](assets/windows-preview.png)

Slink 0.7.3 on macOS and Windows uses the SP-16 Razor Choir score. Each note has its own chrome letter with a separate sharp and octave. The plate beside the seed enlarges the current note. Click a step to inspect it there. Key and scale sit below the score beside the animated chrome whips, pitch contour and position rail. The counter offers **Play** and **Stop** in hosts that support CLAP transport requests. Other hosts show their transport state there.

All 34 host parameters fit on one resizable panel down to 960 × 540.

- Start playback in the host to advance the cobalt playhead. The chrome face turns on its own 4.8-second cycle. The lower strip loops two metal whips and forked lightning. Click a step to inspect it. New instances start with a locked seed. Click Regen to roll another seed and keep it for the next Play. Patterns longer than 16 steps have two banks and a **Follow** switch.
- Click the enamel **Regen** button for a new pattern. Drag the seed under the Slink wordmark to set a seed. Click **Key** or **Scale** to choose from a list.
- Drag or scroll a control to edit it. Hold Shift or enable **Fine** for small adjustments. Clicking a choice opens its menu. Double-click a control to reset it, or right-click for reset and MIDI mapping.
- Drag the filter response horizontally for cutoff and vertically for resonance. Drag across the score for transpose and vertically for density. These gestures send both parameters to the host for automation.
- Click the crest to jump to a different camera angle. Shift-click it to pause or resume the camera independently. **Motion off** holds the artwork while playback and parameter displays continue to update.

Build from this checkout to use the updated editor. The [0.6.0 release](https://github.com/hdslave/hdslave-Slink-Synth/releases/tag/v0.6.0) contains the earlier desktop downloads.

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

Both builds embed the same SP-16 artwork bank, including 600 crest frames, 240 whip-storm frames and 33 poses of the chrome Regen face. The crest and storm play at 30 frames per second. The score, filter curve and controls follow the instrument.

Normal builds use the committed bank. To repack the desktop artwork, run `mac-clap/tools/bake_razor.py` with the SP-16 checkout containing `assets/sp16-thorn/panel.bank` and `tools/sp16_thorn`, followed by a Chromium executable. The baker uses Python, Pillow and Playwright.

The interface uses Barlow SemiBold and Barlow Condensed Bold Italic under the SIL Open Font License. Artwork and font credits are in [THIRD_PARTY.md](../THIRD_PARTY.md). The embedded PNG and JPEG decoder is Sean Barrett’s stb_image 2.30 under the MIT license. [Source and license](third_party/stb/stb_image.h)
