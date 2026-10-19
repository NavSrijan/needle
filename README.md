# Needle for Linux

Needle places a photographic listening room and an animated vinyl record on the desktop background layer. It reads track details and album art from any MPRIS-compatible player. It is a small native GTK application with no web runtime or online account requirement.

## Build

On Debian or Ubuntu, install GTK 3, GTK Layer Shell, libsoup 3, GLib development files, a C compiler, and pkg-config:

```sh
sudo apt install build-essential pkg-config libgtk-3-dev libgtk-layer-shell-dev libsoup-3.0-dev
make
./needle
```

Install system-wide with `sudo make install` (default prefix `/usr/local`).

## Configuration

Settings live in `~/.config/needle/config.ini`. Needle creates the file the first time it starts and watches it for changes, so edits take effect immediately. There is no settings window.

```ini
[Needle]
framing=mid
transparent-background=false
show-art=true
show-caption=true
hide-paused=false
launch-login=false
```

- `framing`: `wide`, `mid`, or `close`.
- `transparent-background`: set to `true` to show only the record and sleeve over the desktop.
- `show-art`: show the current album cover on the desk.
- `show-caption`: show track and artist details.
- `hide-paused`: hide the scene when the player is not playing.
- `launch-login`: create or remove the per-user XDG autostart entry.

## Desktop support

Wayland layer-shell is the priority and targets compositors such as Hyprland and Niri that expose `zwlr_layer_shell_v1`. GNOME Wayland is not supported in this v0. X11 uses a desktop-window fallback; placement depends on the desktop environment.

The wallpaper surface is non-interactive so desktop icons and pointer actions pass through it. MPRIS artwork may come from a local file or HTTP(S) URL; remote fetches are asynchronous, time out after eight seconds, and are limited to 12 MiB.

## macOS

This release is Linux-only. macOS will need a separate desktop-surface host and playback metadata adapter.
