# AI usage

This plugin was built with generative AI.

- **Tool:** Claude Code, Anthropic's coding assistant, using Anthropic's Claude models (Opus).
- **Human role:** Davidian set the requirements and the design direction, supplied the reference schematic, chose the plugin's name, and decides what is released.
- **What the AI did:**
  - Read the component values and topology off the reference schematic.
  - Wrote the circuit model, the LV2 plugin and GUI code, and the panel layout (drawn in code, in the existing Davidian house style).
  - Wrote the build files, the LV2 metadata, the tests and the documentation.
  - Worked out the sweep oscillator's rate calibration from the model's own measurements.
- **How it was checked:**
  - Automated tests in `test/`: the audio path against an independently derived transfer function of the schematic, sweep rates against the requested speed, plugin and GUI loading, mouse handling in the GUI, and the lilv LV2 parser from Ardour 9.
  - `build.bat` builds the tests, and the README lists the commands that run them.
  - No listening test in a DAW has been recorded yet.
- **Training data and copyright:** no third-party code was knowingly copied. The only reproduced material is the LV2 interface declarations, credited in [NOTICE.md](NOTICE.md). The GUI house-style files are the author's own, shared across Davidian plugins. The GUI artwork and font are generated from code, not from images.

The AI's output is released under the project licence, GPL-3.0-or-later (see `LICENSE`). Under the GPL, there is no warranty.
