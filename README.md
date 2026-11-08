# Davidian Tangerine Orbit

A Davidian LV2 circuit model of a classic orange one-knob phaser schematic (four JFET all-pass stages with the 22k feedback resistor). Mono, no latency.

## Build and test on Windows

```
build.bat
build-test\test_dsp.exe build\tangerine_orbit.lv2
build-test\lilv_check.exe build
build-test\ui_check.exe build\tangerine_orbit.lv2
```

`build.bat` needs Visual Studio's C++ tools. `lilv_check.exe` checks the built bundle with the lilv LV2 parser, without installing anything; it ignores any copy already installed. `ui_check.exe` hosts the GUI in a window and drives it with mouse messages.

## Install

Copy the whole `build/tangerine_orbit.lv2` folder (the plugin and GUI libraries and the two .ttl files) into one of the standard LV2 folders:

- Windows: `%APPDATA%\LV2` (current user) or `%COMMONPROGRAMFILES%\LV2` (all users)
- Linux: `~/.lv2` (current user) or `/usr/local/lib/lv2` (all users)
- macOS: `~/Library/Audio/Plug-Ins/LV2`

If your host doesn't scan that folder, add it to the host's LV2 search path or set the `LV2_PATH` environment variable. Close the host before copying and restart it afterwards. Replace any older copy instead of keeping two: when two bundles have the same URI, the loader uses one and ignores the other.

Look for **Davidian Tangerine Orbit** in your host's plugin list. The plugin URI is `urn:davidian:tangerine-orbit`.

## GUI

![GUI](docs/gui.png)

The bundle includes a native GUI (`tangerine_orbit_ui.dll`) in the same house style as the Dual Opto Phasor, using its `style.h`, `font.h` and `canvas.h` unchanged. The panel is called **Tangerine Orbit**, an original name with none of the original pedal's branding. It has no dependencies: it draws with its own software renderer and puts the result in a Win32 window (`ui:WindowsUI`) or an X11 window (`ui:X11UI` on Linux). macOS has no window layer yet, so CMake leaves the GUI out there.

- Drag a knob up or down to turn it. Hold Shift for fine control. Double-click resets it, and the mouse wheel works too.
- Click the footswitch to switch the effect on or off. Its red LED shows the state.
- The amber SWEEP lamp brightens and dims with the oscillator, read from the plugin's `sweep` output port. It goes dark while the effect is off.
- The readout shows the name and value of the last control you touched.

`build-test\ui_render.exe out.bmp` renders the panel to an image without opening a window, on any platform.

## Other platforms

```
cmake -S . -B build-cmake
cmake --build build-cmake --config Release
ctest --test-dir build-cmake -C Release
```

The bundle is written to `build-cmake/tangerine_orbit.lv2`, with the library named for the platform (`.dll`, `.so` or `.dylib`). Only the Windows build has been tested.

## Controls

| Control | Range | Default | Notes |
|---|---|---|---|
| Speed | 0.2–10 Hz | 0.5 Hz | Sets the 500k pot's resistance. In Hz rather than pot position, because the schematic's linear pot crowds the fast settings into the last few percent of its travel |
| Bias Trim | 0–1 | 0.46 | The 250k trim. Moves the whole sweep up or down |
| Volts at 0 dBFS | 0.1–10 | 1.0 | Voltage a full-scale digital signal represents. Higher drives the JFETs and op amps harder and comes out quieter; lower does the reverse |
| Output | -24 to +12 dB | -6 dB | Not on the pedal. The circuit peaks about 7 dB above its input, so -6 dB roughly levels it with bypass. It comes after the circuit and doesn't change the tone |
| Enabled | on/off | on | The host's bypass: 20 ms crossfade. The sweep keeps running while bypassed |
| Effect On | on/off | on | The GUI's footswitch, with the same crossfade. The effect is in only when this and Enabled are both on |
| Sweep Position | 0–1 | output | Where the oscillator is, for the GUI's lamp |

The output is inverted relative to bypass, because the schematic's mixing stage is inverting.

## Code layout

| File | Contents |
|---|---|
| `src/input_buffer.h` | Stage 1: input coupling and IC1a follower |
| `src/allpass.h` | Stage 2: four all-pass stages, JFETs, feedback |
| `src/mix_amp.h` | Stage 3: 2N4125 mixing stage and output coupling |
| `src/lfo.h` | Stage 4: IC3b oscillator, gate bus, bias trim |
| `src/phaser.h` | Audio path in schematic order |
| `src/plugin.c` | LV2 entry points, controls, bypass |
| `src/ports.h` | Port numbers shared by the plugin and the GUI |
| `src/ui/panel.h` | GUI layout, drawing and mouse handling |
| `src/ui/ui_lv2.c` | LV2 GUI entry points and the Win32/X11 window layer |
| `src/ui/style.h`, `font.h`, `canvas.h` | House style, copied unchanged from the Dual Opto Phasor |
| `src/lv2_abi.h` | The LV2 C interface, so no SDK is needed |
| `bundle/` | LV2 metadata |
| `test/test_dsp.c` | Compares the circuit code to the schematic's transfer function, and tests the built library |
| `test/lilv_check.c` | Checks the bundle and GUI with the lilv LV2 parser (Windows only) |
| `test/ui_check.c` | Hosts the GUI in a window and drives it with the mouse (Windows only) |
| `test/ui_render.c` | Renders the panel to an image |

## Values not from the schematic

These are estimates to tune by ear or against a real pedal:

- JFET Vp -2.0 V and Idss 2.5 mA, mid-range for the 2N5952 and the biggest influence on the sweep range (`src/allpass.h`)
- Zener 4.3 V, picked from the 3.6–4.7 V range on the parts list (`src/circuit.h`)
- TL072 output swing of 1.5–7.5 V (`src/circuit.h`)
- 2N4125 saturation current, beta and collector limit (`src/mix_amp.h`)
- Bias Trim default, set so the JFETs stay conducting across the sweep (`src/plugin.c`, `bundle/tangerine_orbit.ttl`)

## Simplifications

- Op amp clipping is applied at the input buffer and at stage 4's output, not inside each all-pass stage. This only matters at very high levels.
- Op amps are otherwise ideal, so their bandwidth and slew limits aren't modelled.
- Each channel in a stereo track gets its own copy from the host.
