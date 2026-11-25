# hdslave Slink — macOS (and Windows) Desktop CLAP Plug-in

Separate desktop CLAP build of **hdslave Slink** for **FL Studio** (2024+) and any other CLAP host on macOS (Apple Silicon / Intel) and Windows (x64).

![Slink acid bass sequencer and voice controls during playback](assets/desktop-preview.gif)

The desktop editor brings the MPC Sample’s silver Press Plate layout into a six-panel grid. The blue-chrome Slink logo sits above the grid, and **Zoom** opens a single panel with the full MPC nameplate. On Pattern, the three satin dials control Cutoff, Resonance and Env Mod. On the other panels they follow the selected row. Drag or scroll a dial to edit its value. Hold Shift while dragging for fine adjustment.

Click **Clock → Theme** to switch the whole editor to Blacklight Transfer. The two Sequencer panels keep all 13 sequencer settings accessible, and Voice and Tone each expose both LFO controls.

Build from this repository to include the shared MPC artwork. The [0.6.0 release](https://github.com/hdslave/hdslave-Slink-Synth/releases/tag/v0.6.0) has the previous desktop downloads.

Slink 0.6.0 is the new name of TB-MPC-3-P-0. Existing projects use the same CLAP ID and factory preset keys. On upgrade, replace the previous plug-in in place, keeping its installed filename if the host stores paths. Install one copy per host.

Version 0.5.5 added a sine pitch LFO on **Tone** and a separate sine cutoff LFO at the bottom of **Voice**.
Both Speed controls follow the host BPM and snap to 1/1, 3/4, 1/2, and 1/3 beat per cycle. Tuning sits directly above Volume.

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
