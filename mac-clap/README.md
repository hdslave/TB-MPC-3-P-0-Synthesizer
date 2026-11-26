# hdslave Slink desktop CLAP plug-in

Separate desktop CLAP build of **hdslave Slink** for **FL Studio** (2024+) and any other CLAP host on macOS (Apple Silicon / Intel) and Windows (x64).

![Slink’s Thorn Crown Pattern page on macOS](assets/thorn-crown-desktop.png)

[See all six desktop pages](assets/thorn-crown-desktop-pages.jpg)

Slink **0.8.1** for macOS and Windows uses the same **Thorn Crown** panel, artwork and pointer gestures as the [freesp16 build](../README.md#freesp16-on-sp-16). The editor resizes down to 960 × 540. The six-control strip below the panel follows the selected page; it replaces the hardware encoder strip.

| Page | What you can edit directly | Desktop strip |
| --- | --- | --- |
| Perform | Audition headers, pitch ribbon, cutoff and resonance readouts | Cutoff, Resonance, Env Mod, Decay, Accent, Volume |
| Voice | Filter response: drag across for cutoff and up for resonance | Waveform, Tuning, Filter, Peak Spread, Drive, Volume |
| Glide | Audition headers, pitch ribbon, slide and slide time readouts | Slide, Slide Time, Accent Decay, Gate, Accent, Decay |
| LFO | Pitch and cutoff plots: drag across for rate and up for depth | Pitch Amount, Pitch Speed, Cutoff Amount, Cutoff Speed, Tuning, Transpose |
| Pattern | Full Razor Choir score, with editable pitches and Gate, Accent and Slide lanes | Seed Mode, Seed, Mutate, Density, Scale, Root |
| Clock | Audition headers, pitch ribbon, division and swing readouts | Mode, Clock Div, Swing, Reset, Steps, Octave |

- **Edit the score:** Click or scrub across numbered headers to audition. Drag a pitch up or down in semitones. Click a Gate, Accent or Slide cell to toggle it, or drag across its row to paint the same setting. The first edit to a random pattern locks its displayed seed; the project saves edits with that seed. Long patterns follow playback into steps 17–32. When stopped, click the large counter to switch halves.
- **Play and seed:** Click **Play** to preview through the plug-in while the DAW is stopped; click **Stop** to end the preview. The DAW takes over on transport start and the button reads **Sync**. Shift-click that button to control the DAW in hosts that expose CLAP transport control. Click **Regen** for a fresh seed, click the seed number to recall a previous played seed, or drag the barcode to set the seed.
- **Motion:** Click the crest to pause or resume its camera without stopping the smiley and whip storm. Shift-click the crest for a manual camera cut. **Motion off** holds all artwork without freezing the controls or playhead.
- **Desktop controls:** Drag or scroll the strip; hold Shift or enable **Fine** for smaller adjustments. Click a choice for its menu, double-click to reset, or right-click for reset and MIDI mapping. Presets and MIDI learn remain in the toolbar. Left and Right switch pages; Up and Down adjust the last selected parameter.

The CLAP ID, parameter IDs and choice order, factory preset keys, MIDI note behavior, and desktop state envelope stay compatible with previous builds. Older projects load without manual step edits. A raw state from the freesp16 build can also carry the shared step-edit records.

Build from this checkout for the new editor. The [0.6.0 release](https://github.com/hdslave/hdslave-Slink-Synth/releases/tag/v0.6.0) contains the older desktop downloads. On upgrade, keep the installed filename if the host stores plug-ins by path. Install one copy per host.

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


## Shared artwork and validation

Both desktop targets embed the same `assets/sp16-thorn/panel.bank` as freesp16. That bank holds the chrome lettering, crest, counter, face poses and storm. The shared renderer draws the score and plots, while Cocoa and Win32 add their host window, toolbar, six-control strip and popup menus. The older desktop artwork banks are retained as design references, not loaded by current builds.

Normal builds use the committed bank. Rebuild it with `tools/sp16_thorn/bake_native.py` from the repository root. Artwork and font credits are in [THIRD_PARTY.md](../THIRD_PARTY.md).

The desktop host test exercises all six pages, parameter automation, MIDI learn, step audition and state, transport takeover, motion holds and window resizing. Use `host_test --snap <directory> <plugin>` to capture the six desktop pages.
