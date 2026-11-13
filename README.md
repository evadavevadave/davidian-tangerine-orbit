# Davidian Tangerine Orbit

A circuit-modelled phaser pedal plugin (LV2, VST3 and Audio Unit), modelled on a classic orange one-knob phaser: four JFET all-pass stages with regeneration through the 22k feedback resistor, swept by an op-amp oscillator, with a transistor mixing stage.

**A close model, not an exact copy.** It is a simulation of the circuit from its component values, not a sampled or measured pedal, and some details are reasoned estimates (listed under [Estimates](#estimates)). It has not been compared side by side with a built unit, so expect it to sound close to the hardware rather than identical.

![The GUI](docs/gui.png)

A mono effect with no latency (mono or stereo tracks work too), with the same GUI in every format. GPL-3.0-or-later.

| Format | Windows | macOS | Linux |
|---|---|---|---|
| LV2 | yes | yes | yes |
| VST3 | yes | | |
| Audio Unit (AUv2) | | yes | |

## Download

No release has been published yet. Every push builds a zip for each platform, attached to its run on the repository's [Actions page](https://github.com/evadavevadave/davidian-tangerine-orbit/actions) (open the latest run, then **Artifacts**). Pushing a `v*` tag will publish them on the [Releases page](https://github.com/evadavevadave/davidian-tangerine-orbit/releases):

| Download | Contains | For |
|---|---|---|
| `…-windows-x64` | LV2, VST3 | Windows 10/11, 64-bit hosts |
| `…-macos-universal` | LV2, Audio Unit | macOS, Apple silicon and Intel |
| `…-linux-x64` | LV2 | 64-bit Intel/AMD Linux |
| `…-linux-arm64` | LV2 | 64-bit ARM Linux, e.g. Raspberry Pi 4/5 |

Unzip, copy the plugin folder for your host's format into its plugin folder, and restart your host (or rescan plugins):

| Format | Copy | Into |
|---|---|---|
| VST3 (Windows) | `tangerine_orbit.vst3` | `C:\Program Files\Common Files\VST3` |
| Audio Unit (macOS) | `tangerine_orbit.component` | `~/Library/Audio/Plug-Ins/Components` |
| LV2 (Windows) | `tangerine_orbit.lv2` | `%APPDATA%\LV2` (create it if it doesn't exist) |
| LV2 (macOS) | `tangerine_orbit.lv2` | `~/Library/Audio/Plug-Ins/LV2` |
| LV2 (Linux) | `tangerine_orbit.lv2` | `~/.lv2` |

In the host it is listed as **Davidian Tangerine Orbit** (VST3, LV2) or under **Davidian** as **Tangerine Orbit** (Audio Unit). Replace any older copy instead of keeping two. The LV2 URI is `urn:davidian:tangerine-orbit`.

The macOS build isn't notarized, so macOS may block it after download. Run these once in Terminal after copying:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/tangerine_orbit.component
```

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/LV2/tangerine_orbit.lv2
```

Logic Pro and GarageBand only show an Audio Unit once macOS has registered it. If it doesn't appear, log out and back in, or restart the Mac.

## Controls

| Panel | Control | Circuit part |
|---|---|---|
| Sweep | Speed | The 500k rate pot, shown in Hz (0.2 to 10 Hz, default 0.5 Hz) rather than pot position, because the schematic's linear pot crowds the fast settings into the last few percent of its travel |
| Sweep | Sweep lamp | Not on the pedal: brightens and dims with the oscillator, and goes dark while the effect is off |
| Calibrate | Bias | The internal 250k trim (0 to 1, default 0.46). Moves the whole sweep up or down |
| Levels | Input V | Volts at the input jack for a 0 dBFS sample (0.1 to 10, default 1.0). Higher drives the JFETs and op amps harder and comes out quieter; lower does the reverse |
| Levels | Output | Not on the pedal: -24 to +12 dB, default -6 dB. The circuit peaks about 7 dB above its input, so -6 dB roughly levels it with bypass. It comes after the circuit and doesn't change the tone |
| Footswitch | Effect | The pedal's footswitch, with a 20 ms crossfade; its red LED shows the state |
| (host) | Enabled | Plugin bypass, with the same crossfade. The sweep keeps running while bypassed. In VST3 it is the host's bypass switch, and in an Audio Unit host's bypass turns it off. The effect is in only when this and the footswitch are both on |

The output is inverted relative to bypass, because the schematic's mixing stage is inverting. On a stereo track the VST3 and Audio Unit versions mix the two inputs to mono and send the output to both sides; LV2 hosts run one copy per channel. The sweep position is also an LV2 output port, for the panel's lamp; the VST3 and AU panels read it from the plugin directly.

Drag a knob up or down, hold Shift to fine-tune, use the mouse wheel, and double-click to reset. The readout shows the name and value of the last control you touched. In VST3 and Audio Unit hosts, each drag or click is one automation gesture.

## How it is modelled

The audio path runs in schematic order (`src/phaser.h`), with the circuit's part values:

- **Input buffer** (`src/input_buffer.h`): input coupling and the IC1a follower.
- **All-pass stages** (`src/allpass.h`): four first-order all-pass stages, each with a 2N5952 JFET as its voltage-controlled resistor, and the 22k feedback resistor that adds regeneration.
- **Mixing stage** (`src/mix_amp.h`): the 2N4125 stage that sums the dry and phase-shifted signals, and the output coupling.
- **Oscillator** (`src/lfo.h`): the IC3b oscillator, the gate bus that drives the JFETs, and the bias trim.

### Checked against the schematic

`test_dsp` compares the circuit code with the schematic's transfer function, and checks the sweep rates and range.

### Estimates

These values are not on the schematic, so they are reasoned estimates:

- JFET Vp -2.0 V and Idss 2.5 mA, mid-range for the 2N5952 and the biggest influence on the sweep range (`src/allpass.h`).
- Zener 4.3 V, picked from the 3.6–4.7 V range on the parts list (`src/circuit.h`).
- TL072 output swing of 1.5–7.5 V (`src/circuit.h`).
- 2N4125 saturation current, beta and collector limit (`src/mix_amp.h`).
- Bias Trim default, set so the JFETs stay conducting across the sweep (`src/engine.h`, `bundle/tangerine_orbit.ttl`).

### Simplifications

- Op amp clipping is applied at the input buffer and at the oscillator's output, not inside each all-pass stage. This only matters at very high levels.
- Op amps are otherwise ideal, so their bandwidth and slew limits aren't modelled.

## Build

The code is plain C11 (Objective-C for the macOS GUI and Audio Unit) with no dependencies beyond the platform's own libraries: Win32 on Windows, Cocoa and AudioToolbox on macOS, Xlib on Linux and BSD. No LV2, VST3 or AU SDK is needed: the VST3 version uses Steinberg's one-file C API header, included in `src/vst3/`.

| Source | What it is |
|---|---|
| `src/input_buffer.h`, `src/allpass.h`, `src/mix_amp.h`, `src/lfo.h`, `src/phaser.h`, `src/circuit.h` | The circuit model, stage by stage |
| `src/engine.h` | The engine shared by every format: controls, smoothing, bypass, sweep output |
| `src/ports.h` | Port numbers, also the VST3 and AU parameter IDs |
| `src/plugin.c`, `src/ui/ui_lv2.c` | LV2 plugin and GUI |
| `src/vst3/vst3.c` | VST3 plugin, with its editor |
| `src/au/au.m` | Audio Unit, with its Cocoa view |
| `src/ui/panel.h`, `src/ui/window.h` | The panel and the window layer under it (Win32, Cocoa, X11), shared by every GUI |
| `src/ui/style.h`, `font.h`, `canvas.h` | Davidian GUI house style, shared unchanged across Davidian plugins |
| `src/lv2_abi.h` | The LV2 C interface, so no SDK is needed |
| `bundle/` | LV2 metadata (the manifest is a template the build fills in per platform) and the Audio Unit's `Info.plist` |

### Windows (Visual Studio)

Needs Visual Studio 2019 or later with the C++ workload. `build.bat` finds it on its own, and builds `build\tangerine_orbit.lv2` and `build\tangerine_orbit.vst3`.

```bash
build.bat
```

### macOS, Linux, BSD, MinGW

Needs a C compiler and `make`. On Linux, also the X11 headers (`libx11-dev` on Debian/Ubuntu). On macOS, the Xcode command-line tools; the build is universal (Apple silicon and Intel) and also makes the Audio Unit, `build/tangerine_orbit.component`.

```bash
make
```

```bash
make install
```

`make install` copies the LV2 bundle to your user LV2 folder, and on macOS the Audio Unit to `~/Library/Audio/Plug-Ins/Components`.

## Tests

Windows:

```bash
build.bat test
```

macOS, Linux:

```bash
make test
```

- `test_dsp` compares the circuit code with the schematic's transfer function, checks sweep rates and range, and tests the built plugin: bypass, footswitch, sweep output, silence, bad input, every sample rate and CPU cost.
- `ui_render` draws the panel to a BMP file.
- `ui_check` (Windows) and `ui_check_mac` (macOS) load the GUI the way a host does, drive it with mouse events and check the values it sends back.
- `load_check` (Linux, macOS) loads the plugin and GUI with `dlopen` and runs them.
- `lilv_check` (Windows) validates the bundle with the LV2 parser from an Ardour 9 install: `build-test\lilv_check.exe build`. On Linux, CI checks it with `lv2info`.
- `vst3_check` (Windows) loads the VST3 the way a host does and checks the factory, buses, parameters, audio, latency, both bypasses and saved state, then opens the editor and drives it with the mouse, checking each edit reaches the host inside a begin/end gesture.
- `au_check_mac` (macOS) opens the installed Audio Unit and checks the same things, including its Cocoa view. Run `make install` first.

CI also runs Steinberg's VST3 validator on the Windows build and Apple's `auval -strict` on the Audio Unit.

GitHub Actions runs all of these on Windows, macOS, Linux x64 and Linux arm64 for every push (see `.github/workflows/build.yml`). Pushing a `v*` tag publishes a release with a zip for each platform.

## Use of AI

Made with generative AI (Claude Code), directed and reviewed by Davidian. See [AI_USAGE.md](AI_USAGE.md) for what the author and the AI each did, and what has and hasn't been checked.

## References

- Circuit: the component values and topology of a published schematic of a classic four-stage JFET phaser pedal ("script" version, with the 22k feedback resistor). See [NOTICE.md](NOTICE.md) for where it was viewed.
- Component data: typical figures from the manufacturers' datasheets for the 2N5952 JFET and the TL072 op amp.

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE), [NOTICE.md](NOTICE.md) and [AI_USAGE.md](AI_USAGE.md). Every source file carries an `SPDX-License-Identifier` line. `src/lv2_abi.h` is under the ISC licence and `src/vst3/vst3_c_api.h` under Steinberg's 3-clause BSD licence, both GPL-compatible. The build copies `LICENSE`, `NOTICE.md` and `AI_USAGE.md` into every bundle.
