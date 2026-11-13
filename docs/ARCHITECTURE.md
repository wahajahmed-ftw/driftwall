# Driftwall architecture

This document is for people who want to understand or change Driftwall. For what Driftwall does and how to use it, see the [README](../README.md).

## Design goals

1. **Low steady-state cost.** The program that runs all day must be small: no browser engine, no runtime, no idle CPU.
2. **Nothing heavy in the always-on process.** All UI lives in a separate app that runs only while it is open.
3. **No surprises on the desktop.** Wallpapers survive Explorer restarts, monitor changes, sleep and GPU resets, and only safe, scanned media files get into the library.
4. **Easy to build.** No Visual Studio and no system-wide installs. One portable toolchain and Windows' own C# compiler.

## Processes

```
                    config.json  (written atomically)
  Driftwall.exe  ───────────────────────────────────►  DriftwallEngine.exe
  (gallery, C#)     PostMessage(WM_APP+1 reload)        (engine, C++)
                    PostMessage(WM_APP+2 exit)          tray icon, one renderer per monitor
```

- **The engine** is a single-threaded Win32 program. Media Foundation decodes video on its own worker threads. The engine owns a hidden top-level window of class `DriftwallEngine`. It is a real top-level window, not a message-only one, so it receives the `TaskbarCreated` broadcast when Explorer restarts. A mutex named `Local\DriftwallEngineInstance` makes sure only one engine runs. A second launch forwards its request to the running engine (reload, or exit with `--exit`) and quits.
- **The gallery** is a WinForms app. It has a single-instance mutex, `Local\DriftwallGallery`. It never draws wallpapers itself. It edits files and tells the engine to reload.

Both programs treat **the folder containing their exe** as the data folder, so a release unzipped anywhere works without installing.

```
<install folder>\
  DriftwallEngine.exe
  Driftwall.exe
  config.json          which wallpaper is on which monitor, plus settings
  engine.log           engine log, rewritten at each start (crashes are logged here too)
  gallery.log          only created if the gallery hits an unexpected error
  Library\<id>\        one folder per wallpaper
```

## File formats

### `config.json`

```json
{
  "sceneFps": 30,
  "pauseWhenCovered": true,
  "monitors": {
    "\\\\?\\DISPLAY#ABC1234#5&12345678&0&UID4353#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}": "the-hill"
  }
}
```

- **Monitor keys** are device interface paths, as returned by `EnumDisplayDevices(..., EDD_GET_DEVICE_INTERFACE_NAME)`. These are the same IDs `IDesktopWallpaper` uses. Unlike `\\.\DISPLAY1`, they stay the same when monitors are re-plugged or rearranged.
- **How the gallery writes it:** to `config.json.tmp` first, then it swaps the file in with `File.Replace`. The engine never sees a half-written file.

### `Library\<id>\wallpaper.json`

```json
{
  "title": "Water",
  "type": "video",
  "file": "video.mp4",
  "thumbnail": "thumb.jpg",
  "source": "Added from water.mp4",
  "added": "2026-11-12T03:10:00"
}
```

- **`type`** is `video`, `image` or `scene`.
- **Scenes** use `scene` (the built-in scene name, e.g. `hill`) and `params` (scene settings) instead of `file`. See [`wallpapers/the-hill/wallpaper.json`](../wallpapers/the-hill/wallpaper.json).
- **`file`** must be a plain file name inside the wallpaper's folder. The engine rejects path separators and `..`. It rejects library IDs containing them too.
- **`thumb.jpg`** is 480×270 and is used only by the gallery.

## Engine

The engine source is in [`engine/`](../engine):

| File | What it holds |
|---|---|
| `main.cpp` | Entry point, monitor slots, the desktop layer, pause logic, the tray, the message loop |
| `gfx.cpp` | The shared D3D11 device, surface windows, swap chains, the shader compiler, PNG output |
| `video.cpp` | The video renderer (Media Foundation media engine) |
| `scene.cpp` | Built-in scenes (*The Hill*) and `--snapshot` |
| `util.cpp`, `json.h` | UTF-8 helpers, logging, a small JSON reader, wallpaper loading |
| `engine.h` | Shared types: `Gfx`, `Monitor`, `Wallpaper`, `Renderer` |

### The desktop layer

Wallpapers have to be drawn *behind* the desktop icons but *above* the static wallpaper. Explorer creates a `WorkerW` window for that purpose when Progman receives the undocumented message `0x052C`:

- **Windows 11 24H2 and later:** the `WorkerW` is a **child of Progman**.
- **Earlier builds:** it is the **top-level** `WorkerW` that follows the window hosting `SHELLDLL_DefView`.

`FindWallpaperParent()` handles both. The engine creates one child window per monitor, class `DriftwallSurface`, inside it. These windows are click-through (`HTTRANSPARENT`) and never take focus.

### Monitor slots

Each monitor gets a `Slot`: the monitor's ID, rectangle, work area and DPI, the assigned wallpaper, and a `Renderer`.

`Apply(force)` rebuilds the slot list from the current monitors and `config.json`:

- **Unchanged slots keep their renderer.** Switching the wallpaper on one monitor doesn't restart the others.
- **Images** are not rendered. They are set per monitor with `IDesktopWallpaper::SetWallpaper`, so Windows draws them and they cost nothing.
- **Removed renderers:** once they are destroyed, each monitor's Windows wallpaper is set again so the area repaints.

### Render loop

Each renderer reports an `Interval()`, the number of seconds between ticks; `0` means it doesn't need ticks right now. The main loop:

1. finds the earliest `nextTick` across all renderers;
2. arms a high-resolution waitable timer for it (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`);
3. waits in `MsgWaitForMultipleObjectsEx` on the timer **and** the message queue;
4. ticks the renderers that are due, then drains messages.

**Display-paced renderers:**
- A scene running at the display's refresh rate is not timed by the clock.
- **Setup:** its swap chain is created with `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` and a maximum frame latency of 1, and it presents with sync interval 1.
- **Signal:** the waitable object is signaled once per vblank of *its own* monitor. A 144 Hz monitor and a 60 Hz monitor each get exactly their own rate.
- **Waiting:** those handles go into the same `MsgWaitForMultipleObjectsEx` call as the timer.
- **Why not a timer:** a timer at 1/144 s would drift against the real vblank and show an occasional doubled frame. The waitable can't drift.
- **Fallback:** a blt-model swap chain has no waitable, so the scene uses the timer at the refresh rate instead.

When every renderer is paused there is no timer to arm. The engine blocks on messages alone and uses no CPU.

### Pausing

A slot is paused when any of these is true:

| Condition | How it is detected |
|---|---|
| A window covers the monitor's whole work area (maximized or fullscreen) | `EnumWindows` every 500 ms. Cloaked windows (other virtual desktops, suspended UWP apps), minimized windows, shell windows and click-through overlays are ignored. |
| The session is locked | `WTSRegisterSessionNotification` |
| The display is off | `RegisterPowerSettingNotification(GUID_CONSOLE_DISPLAY_STATE)` |
| The user paused from the tray | Tray menu |

What a pause does depends on the wallpaper:

- **Videos** call `IMFMediaEngine::Pause()`, which stops decoding.
- **Scenes** stop ticking. When a scene resumes, its clock does not jump forward.

### Video renderer

- **Pipeline:** `IMFMediaEngine` in **frame-server mode**, sharing the engine's D3D11 device through an `IMFDXGIDeviceManager`. The engine is created with `MF_MEDIA_ENGINE_REAL_TIME_MODE`, which cut memory use from about 55 MB to about 32 MB for a 1080p H.264 file.
- **Each tick:**
  1. `OnVideoStreamTick` reports whether a new frame is ready.
  2. If one is, `TransferVideoFrame` scales it straight into the swap chain's back buffer, using a source rectangle that crops the video to fill the monitor (like CSS `object-fit: cover`).
  3. `Present`.
- **Polling rate:** twice the video's frame rate, read from `MF_MT_FRAME_RATE` and clamped between 30 and 240 Hz.
- **Playback:** muted and looping.

### Scene renderer (*The Hill*)

- **Terrain:** a 257×257 grid displaced in the vertex shader by three octaves of classic Perlin noise. It is drawn with alpha blending that matches three.js `NormalBlending`, with the camera and projection of the original three.js scene.
- **Shaders:** compiled at startup with `d3dcompiler_47.dll`, which ships with Windows. The DLL is unloaded once setup is done.
- **Clock and date:** drawn with GDI into a DIB, using the scene's fonts loaded privately with `AddFontResourceEx(FR_PRIVATE)`. The DIB is converted to an alpha texture and drawn as a quad. The texture is redrawn only when the text changes, about once a minute.
- **Frame rate:** 30 fps by default. With *Smooth live scenes* on (`"sceneFps": 0`), each scene runs at its own monitor's refresh rate, paced by the display (see *Render loop*).

### Recovery

| Event | What the engine does |
|---|---|
| Explorer restarts (`TaskbarCreated`) | Re-adds the tray icon, finds the new `WorkerW` and rebuilds every slot |
| Monitor added, removed or rearranged; DPI or work area changes | Rebuilds after a 1.5 s debounce |
| Resume from sleep | Rebuilds after 3 s |
| GPU device removed (driver update or reset) | Detected on the 500 ms timer. Drops every renderer and the device, then rebuilds. |
| Graphics can't be created at all | Keeps running with image wallpapers only, and retries on the next rebuild |
| Unhandled exception | `CRASH <code> at <module>+<offset>` is written to `engine.log` |

## Gallery

The gallery source is in [`gallery/`](../gallery):

| File | What it holds |
|---|---|
| `Program.cs` | Single instance, DPI awareness, the global error handler (`gallery.log`) |
| `MainForm.cs` | The window: header, displays panel, library grid, menus, settings and dialogs |
| `Controls.cs` | The dark theme and the owner-drawn controls (`DarkButton`, `DisplayBar`, `WallpaperTile`, the menu renderer) |
| `Library.cs` | Reads the library, edits `config.json`, starts and stops the engine, the startup registry entry |
| `Importer.cs` | Content sniffing, the Defender scan, copying, thumbnails, Lively import |
| `Native.cs` | P/Invoke: window messages, dark title bar, monitor IDs, shell thumbnails |

### Adding files

`Importer` runs on a background STA thread. The shell thumbnail API, `IShellItemImageFactory`, requires STA. For each file it:

1. **Sniffs the first 16 bytes:**
   - video: `ftyp` (MP4, MOV), EBML (MKV, WebM), `RIFF…AVI `, ASF;
   - image: JPEG, PNG, `RIFF…WEBP`, BMP.

   Anything else is refused.
2. **Scans** the file with `MpCmdRun.exe -Scan -ScanType 3 -File <path>`. It looks for the newest Defender platform folder first, then falls back to Program Files. Exit code `0` means clean and `2` means a threat. On any other result, the user is asked whether to add the file anyway.
3. **Copies** the file to `Library\<slug>-<random hex>\video.<ext>` or `image.<ext>`.
4. **Makes a 480×270 thumbnail.** It uses Lively's thumbnail if one exists, otherwise Windows' shell thumbnail, which can extract a frame from a video.
5. **Writes `wallpaper.json`.**

If any step after the scan fails, the half-made folder is deleted.

### Talking to the engine

- **Saving:** every change goes through `Library.SaveConfig()`, which writes `config.json` atomically and then posts `WM_APP+1` to the `DriftwallEngine` window. If the engine isn't running, the gallery starts it.
- **Deleting a wallpaper:** the gallery first unassigns it, so the engine closes the file, then removes the folder, retrying for a few seconds.

## Build

- **Engine:** `scripts/build.ps1` compiles with LLVM-MinGW's `clang++`, using `-std=c++17 -O2 -static -s` with `-Wall -Wextra`. Linking statically means the engine has no runtime DLLs to ship.
- **Gallery:** `csc.exe` from .NET Framework 4.8, which is part of every Windows 10 and 11 install, targeting C# 5.
- **Versions:** both version numbers come from the `VERSION` file. The build passes it to the engine's resource script as `DW_VER_*` defines and generates `AssemblyInfo.g.cs` for the gallery.
- **CI:** [`.github/workflows/build.yml`](../.github/workflows/build.yml) builds on `windows-latest` with a cached toolchain. A `v*` tag that matches `VERSION` is published as a GitHub release.
