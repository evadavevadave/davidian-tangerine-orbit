# AI usage

This plugin was built with generative AI.

- **Tool:** Claude Code, Anthropic's coding assistant, using Anthropic's Claude models (Opus).
- **Human role:** Davidian set the requirements and the design direction, supplied the reference schematic, chose the plugin's name, and decides what is released.
- **What the AI did:**
  - Read the component values and topology off the reference schematic.
  - Wrote the circuit model, the LV2, VST3 and Audio Unit plugin and GUI code, and the panel layout (drawn in code, in the existing Davidian house style). The VST3 and Audio Unit wrappers, the shared window layer, the Makefile and the CI workflow were adapted from the author's earlier Davidian plugins.
  - Wrote the build files for Windows, macOS and Linux, the LV2 metadata, the tests and the documentation.
  - Worked out the sweep oscillator's rate calibration from the model's own measurements.
- **How it was checked:**
  - Automated tests in `test/`: the audio path against an independently derived transfer function of the schematic, sweep rates against the requested speed, bypass and footswitch, plugin and GUI loading, mouse handling in the GUI on Windows and macOS, load checks on Linux and macOS, and the lilv LV2 parser from Ardour 9. The VST3 and Audio Unit versions have their own host tests (`vst3_check`, `au_check_mac`), and Steinberg's VST3 validator and Apple's `auval` are run on them.
  - `build.bat test` (Windows) and `make test` (macOS, Linux) run them, and GitHub Actions runs them on every platform for each push.
  - The Windows builds were tested on the development machine, including Steinberg's validator. The macOS and Linux builds are only checked by CI.
  - No listening test in a DAW has been recorded yet.
- **Training data and copyright:** no third-party code was knowingly copied. The only reproduced material is the LV2 interface declarations and Steinberg's VST3 C API header, credited in [NOTICE.md](NOTICE.md). The GUI house-style files are the author's own, shared across Davidian plugins. The GUI artwork and font are generated from code, not from images.

The AI's output is released under the project licence, GPL-3.0-or-later (see `LICENSE`). Under the GPL, there is no warranty.
