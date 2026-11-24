# hdslave TB-MPC-3-P-0 — macOS (and Windows) Desktop CLAP Plug-in

Separate desktop CLAP build of **hdslave TB-MPC-3-P-0** for **FL Studio** (2024+) and any other CLAP host on macOS (Apple Silicon / Intel) and Windows (x64).

![TB-MPC-3-P-0 acid bass sequencer and voice controls during playback](assets/desktop-preview.gif)

- **Multi-Screen MPC Dashboard:** displays all of hdslave TB-MPC-3-P-0's native MPC screens simultaneously on one resizable Retina/HiDPI panel (with the hardware `K1`–`K3` footer panels removed) and direct mouse, trackpad, and touch control over every parameter, plot, sequencer, and pad.
- **Standalone from the MPC Sample build:** this directory is completely separate from the MPC Sample build in the repository root (`../src`, `../build.sh`). GitHub releases remain MPC Sample-only for now while the desktop builds are being finalized.

Version 0.5.5 adds a sine pitch LFO on **Tone** and a separate sine cutoff LFO at the bottom of **Voice**.
Both Speed controls follow the host BPM and snap to 1/1, 3/4, 1/2, and 1/3 beat per cycle. Tuning sits directly above Volume.

## Build & Install (macOS)

Requires Xcode Command Line Tools and CMake 3.20+ (Ninja optional):

```bash
cd mac-clap
./build-macos.sh
./install-macos.sh
```

`install-macos.sh` builds, ad-hoc signs, tests, and installs `TB-MPC-3-P-0.clap` to `/Library/Audio/Plug-Ins/CLAP/` (pass `--user` to install to `~/Library/Audio/Plug-Ins/CLAP/`). Quit and reopen FL Studio after reinstalling so macOS reloads the updated bundle.

## Build (Windows x64)

```powershell
cd mac-clap
.\build-windows.ps1
```

Copy `build\windows\clap\Release\TB-MPC-3-P-0.clap` to `C:\Program Files\Common Files\CLAP\`.
