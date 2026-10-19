# Needle for Linux — v0

Needle places a photographic listening-room scene and animated turntable on the Wayland background layer and reads the active track from MPRIS. It is a small native GTK app; no Electron runtime, Spotify account, or cloud service is required.

## Current scope

- Wayland layer-shell background surface, with one surface per display.
- X11 desktop-window fallback.
- MPRIS metadata, local and HTTPS album-art URLs, and previous/play-pause/next controls.
- Three photographic room scenes, turntable size, live album sleeve, caption, paused visibility, and login-start settings.
- Initial compatibility targets: Hyprland, Niri, and other compositors exposing `zwlr_layer_shell_v1`.
- GNOME Wayland is not a v0 target.

## Build

On Debian/Ubuntu, install the GTK 3, GTK Layer Shell, libsoup 3, and GLib development packages, plus a C compiler and pkg-config. For example:

```sh
sudo apt install build-essential pkg-config libgtk-3-dev libgtk-layer-shell-dev libsoup-3.0-dev
make
```

Then run `./needle` from the project directory. Open the settings window with `./needle --settings`. The app uses the session D-Bus for MPRIS; any compatible player can provide track data.

The app and its image assets can be installed with `sudo make install` (default prefix `/usr/local`). Enable “Launch on login” in settings to create a per-user XDG autostart entry.

## Notes

- The Wayland layer-shell protocol is not available on every compositor. Needle reports a warning and tries a normal desktop-window fallback; background placement on that fallback depends on the compositor.
- The background surface is non-interactive. Use the “Needle Settings” app launcher entry to change options.
- Album artwork is loaded from local `file://` or HTTP(S) MPRIS art URLs and shown on the sleeve. Remote fetches are asynchronous, time out after eight seconds, and are limited to 12 MiB.
- The supplied video is used as a reference, not played as a wallpaper clip. The room and turntable images are local project assets; only current-player cover art may be downloaded.

## macOS direction

This v0 is a Linux GTK app. macOS will need its own desktop-surface host and playback metadata adapter. The visual scene is drawn locally with Cairo and can be reused or ported when that host is added.
