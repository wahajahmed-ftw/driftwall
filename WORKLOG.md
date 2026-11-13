# Driftwall work log

Decisions, measurements and open items, newest first. Release notes live in [CHANGELOG.md](CHANGELOG.md).

## 2026-11-13: v1.0.0, production pass and public release

**Hardening**
- **Graphics failures:**
  - The engine survives D3D11 device creation failing. Image wallpapers still apply, and graphics are retried on every rebuild.
  - A lost GPU device is torn down and recreated through the same path.
  - `GfxInit` now checks every interface it queries and cleans up after a partial failure.
- **Scene text:** the overlay bails out cleanly if `CreateDIBSection` fails.
- **Gallery errors and instancing:**
  - A global exception handler writes `gallery.log` and shows one message instead of the .NET crash dialog.
  - A second launch now finds the first instance by process, not by window title. An Explorer window titled "Driftwall" used to match.
- **Defender path:** the platform folder is now found through `CommonApplicationData`, not a hard-coded `C:\ProgramData`.
- **Readability:** icon glyphs are named constants instead of invisible private-use characters in the source.

**Build and distribution**
- **Version:** a single `VERSION` file feeds the engine's resources (`DW_VER_*` defines into windres) and a generated `AssemblyInfo.g.cs`.
- **Warnings:** the engine builds clean with `-Wall -Wextra`.
- **Toolchain:** `scripts/get-toolchain.ps1` downloads LLVM-MinGW 20261104 and checks its SHA-256 against the release digest.
- **Install:**
  - `install.ps1` installs per user to `%LOCALAPPDATA%\Programs\Driftwall` by default, or `-InstallDir`.
  - It works from the repo or from a release zip, and seeds the built-in wallpapers.
  - `uninstall.ps1` added.
- **Release:** `package.ps1` builds `dist/Driftwall-<version>-win-x64.zip` with `.cmd` launchers and a `.sha256`.
- **CI:** GitHub Actions builds every push. A `v*` tag publishes a release.
- **Built-in wallpaper:**
  - *The Hill* now ships in `wallpapers/the-hill`; its sources are MIT and the fonts are Apache/OFL.
  - Its thumbnail was re-rendered with `--snapshot`, so it no longer reuses Lively's image.

**Docs:** a public README, `docs/ARCHITECTURE.md`, CHANGELOG, LICENSE (MIT) and THIRD-PARTY-NOTICES.

**Re-checked after the changes:** a fresh install over the existing one kept the library and config, the engine reports v1.0.0, and it uses ~39 MB with both wallpapers loaded.

## 2026-11-11/12: v0.1, first working version

**Goal:** replace Lively Wallpaper (~670 MB RAM, slow) with a lightweight live wallpaper app that starts with Windows, plus a gallery to manage a library.

**Decisions**
- **Two programs:** an always-on C++ engine (no UI, tray only) and an on-demand C# gallery. The gallery talks to the engine through `config.json` plus `WM_APP+1` posted to the engine's hidden window (class `DriftwallEngine`).
- **Compiler:** portable LLVM-MinGW in `tools\` instead of Visual Studio Build Tools, to avoid a 5 GB install. The gallery uses Windows' built-in csc.
- **Video:** Media Foundation media engine in frame-server mode, sharing the D3D11 device.
  - `MF_MEDIA_ENGINE_REAL_TIME_MODE` cut video memory from ~55 MB to ~32 MB.
  - Deselecting the audio stream made it *worse* (~86 MB, because the pipeline was rebuilt), so that was reverted.
- **"The Hill":** the Lively web wallpaper (three.js) was ported to native D3D11 plus a GDI text overlay. It costs ~5 MB instead of a browser engine. Checked against the original with `--snapshot`.
- **Images:** go to Windows via `IDesktopWallpaper`, at no engine cost.
- **Desktop layer:** Windows 11 24H2 puts the WorkerW *inside* Progman. The engine handles both that and the older top-level WorkerW. Verified on build 26100.
- **Imports:** content sniffing plus a Microsoft Defender scan for every file.

**Measured** (private working set, GTX 1660 Super, 1920×1080 + 1200×1920):

| What is showing | Memory |
|---|---|
| Engine alone | 8.7 MB |
| The Hill | 13.7 MB |
| 1080p video | 40.8 MB |
| Both | ≈ 44 MB |

CPU is ≈ 1–2.5 % with both animating, and ~0 when paused.

**Bugs fixed during bring-up**
- Shader blobs were used after `d3dcompiler_47.dll` was unloaded (crash).
- The overlay quad was culled by the terrain's rasterizer state.
- A `DrawText` name clashed with the Win32 macro.
- Dancing Script's GDI family name is "Dancing Script OT".

## Open items

- **Engine stop:** the engine stopped once during v0.1 testing with no log line and no WER report. A crash logger was added (`CRASH ...` lines in engine.log), and it hasn't happened again since. Keep watching.
- **Discover tab:** well-known sources only, with a Defender scan on every download. Candidates:
  - Wallpaper Engine Workshop video items, for owners of Wallpaper Engine, through the Steam Web API and SteamCMD;
  - Shadertoy, which needs a GLSL→HLSL path;
  - Pexels and Pixabay videos;
  - Wallhaven images.
- **GIF support** and on-import conversion (ffmpeg) to monitor-sized H.264/HEVC.
- **Web wallpapers** through WebView2, loaded only while a web wallpaper is active.
- **Lower video memory:** possibly replace the media engine with IMFSourceReader plus our own NV12→RGB shader.
- **Code signing** for the release binaries, to avoid the SmartScreen warning.
