# Notices and licences

## Davidian Tangerine Orbit

Copyright (C) 2026 Davidian

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program (`LICENSE`). If not, see <https://www.gnu.org/licenses/>.

GPL-3.0-or-later covers everything in this repository unless listed below: the DSP and GUI source, the tests, the build files, the LV2 metadata (`bundle/`), the GUI artwork and stroke font (both drawn in code), `docs/gui.png` (rendered by the plugin's own GUI code) and the documentation.

AI was used to make this software. See [AI_USAGE.md](AI_USAGE.md).

## Shared with other Davidian plugins

`src/ui/style.h`, `src/ui/font.h` and `src/ui/canvas.h` are the Davidian GUI house style, shared unchanged across Davidian plugins. Same author, same licence (GPL-3.0-or-later). The build approach (a copied LV2 interface header, `build.bat`, and tests that load the built library and use Ardour's lilv) follows the earlier Davidian plugins.

## Third-party material

### LV2 interface declarations (ISC)

`src/lv2_abi.h` and the UI ABI block at the top of `src/ui/ui_lv2.c` declare the LV2 C interface with the same names and layout as the LV2 headers, so the build needs no LV2 SDK. No other LV2 code is included. The LV2 specification is by the LV2 project, <https://lv2plug.in/>.

- `lv2/core/lv2.h`: Copyright 2006-2020 Steve Harris, David Robillard
- `lv2/ui/ui.h`: Copyright 2009-2016 David Robillard, Lars Luthman

> Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted, provided that the above copyright notice and this permission notice appear in all copies.
>
> THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

ISC is compatible with the GPL, so the combined work is distributed under GPL-3.0-or-later.

### Used by the build but not included

| Component | Licence | How it is used |
|---|---|---|
| Microsoft C runtime | Microsoft Visual Studio licence | Linked statically into the Windows DLLs by `build.bat` (`/MT`). It is a system library under GPL-3.0 section 1. |
| Xlib (libX11) | MIT/X11 | Linked dynamically by the Linux GUI. |
| lilv, from the Ardour 9 install | ISC | Loaded at run time by `test/lilv_check.c` only, which also reads the LV2 specification bundles that ship with Ardour. Not shipped in the bundle. |
| Win32 user32/gdi32, C maths library | System libraries | Linked dynamically. |

Thanks to the [Ardour](https://ardour.org/) project, whose DAW and bundled lilv were used to check the plugin, and to the authors of lilv and the LV2 specification.

## Circuit and reference sources

- **Circuit:** the model follows the component values and topology of a published schematic of a classic four-stage JFET phaser pedal ("script" version with the 22k feedback resistor), viewed on schemesnet.com. The schematic's drafter is not credited on that page. Component values and circuit topology are facts used to write an original simulation. No copy of the schematic image is included in this repository or the bundle.
- **Component data:** the 2N5952 JFET and TL072 values are typical figures from the manufacturers' published datasheets. Values that are estimates rather than from the schematic are listed in the README.
- **Trademarks:** the original pedal's brand and model names are trademarks of their owners. They are not used in the plugin's name, URI, files or GUI, and Davidian is not affiliated with them. "Tangerine Orbit" is an original name.
- **GUI design:** original. It does not copy the original pedal's logo, wordmark, lettering or artwork.
