# Third-party notices

Driftwall itself is MIT licensed (see [LICENSE](LICENSE)). It includes or is derived from the following works.

## GLSL Hills ("The Hill")

- **Original:** [GLSL Hills](https://codepen.io/ykob/pen/aBrjaR), Copyright (c) 2020 Yoichi Kobayashi. MIT License.
- **Lively's version:** Driftwall's built-in scene follows the version packaged for [Lively Wallpaper](https://github.com/rocksdanister/lively), which adds colour and opacity settings, a custom background and a clock and date display.
- **Driftwall's port:** a native Direct3D 11/HLSL reimplementation in `engine/scene.cpp`.
- **License text:** the full license and the list of changes ship with the wallpaper in `wallpapers/the-hill/LICENSE.txt`.

## Classic Perlin noise

The `cnoise` function in `engine/scene.cpp` is a port of "GLSL textureless classic 3D noise" by Stefan Gustavson, Copyright (c) 2011 Stefan Gustavson, MIT License.

## Fonts (in `wallpapers/the-hill/fonts/`)

| Font | License | License file |
|---|---|---|
| Roboto | Apache License 2.0 | `Roboto-LICENSE.txt` |
| Cinzel | SIL Open Font License 1.1 | `Cinzel-LICENSE.txt` |
| Dancing Script | SIL Open Font License 1.1 | `DancingScript-LICENSE.txt` |

## Build toolchain (not distributed)

[LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw) is downloaded by `scripts/get-toolchain.ps1` to build the engine. It is not part of this repository or of the release zips. The engine links statically against the MinGW-w64 runtime and libc++, whose licenses allow redistribution in binary form.
