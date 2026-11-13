# Changelog

## 1.1.0 (2026-11-14)

- **Smooth live scenes now match your display:**
  - With *Smooth live scenes* on, The Hill runs at each monitor's own refresh rate, for example 144 Hz on one monitor and 60 Hz on another.
  - It is paced by each display's vblank, so no frame is shown twice.
  - With the setting off, scenes still run at 30 fps.
- **Frame-rate logging:** the engine log records each scene's measured frame rate a few seconds after it starts.

## 1.0.0 (2026-11-13)

The first public release.

### Engine

- Plays live wallpapers behind the desktop icons, with a separate wallpaper on each monitor. Works with both the Windows 11 24H2 desktop layout and earlier ones.
- **Video wallpapers** (MP4, MOV, M4V, MKV, WebM, AVI, WMV): hardware decoding through Media Foundation, cropped to fill the monitor, muted and looping.
- **Built-in live scene *The Hill*:** native Direct3D 11, with a clock and date overlay, at 30 or 60 fps.
- **Image wallpapers:** set per monitor through Windows itself, at no cost to the engine.
- **Pausing:** wallpapers pause when a window covers the monitor, when the PC is locked, when the display is off, or from the tray. While paused the engine uses no CPU.
- **Recovery:**
  - Explorer restarts, monitor and DPI changes, sleep and resume, and GPU device loss.
  - If graphics can't be created at all, image wallpapers still work.
- **Crash logging:** crashes are written to `engine.log`.

### Gallery

- A dark-themed library with a live view of your monitor layout.
- Filters, plus rename, delete and set on one or all monitors.
- **Adding files:** drag and drop or a file picker. Every file is identified by its content and scanned with Microsoft Defender before it is added.
- One-click import of Lively Wallpaper video and image wallpapers.
- **Settings:** start with Windows, pause when covered, 60 fps scenes, start or stop the engine.
- Unexpected errors are logged to `gallery.log` instead of crashing the gallery.

### Distribution

- A per-user installer and uninstaller (no admin rights), plus a portable release zip.
- Pinned, checksum-verified toolchain download, and CI builds and releases with GitHub Actions.
