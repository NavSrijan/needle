# Needle for Linux

Needle places a programmatically drawn listening room and an animated vinyl record on the desktop background layer. It reads track details and album art from any MPRIS-compatible player. It is a small native GTK application with no web runtime or online account requirement.

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
show-title=true
show-artist=true
show-album=true
show-status=false
show-player=false
hide-paused=false
launch-login=false
```

- `framing`: `wide`, `mid`, or `close`.
- `transparent-background`: set to `true` to float the record, recent covers, sleeve, and player display over the desktop.
- `show-art`: show recent covers in the wall frames and the current sleeve artwork.
- `show-title`, `show-artist`, `show-album`: independently show these fields on the turntable's front display.
- `show-status`, `show-player`: optionally show playback status and the player name on that display.
- `show-caption`: retained for compatibility with older configs.
- `hide-paused`: hide the scene when the player is not playing.
- `launch-login`: create or remove the per-user XDG autostart entry.

## Desktop support

Wayland layer-shell is the priority and targets compositors such as Hyprland and Niri that expose `zwlr_layer_shell_v1`. GNOME Wayland is not supported in this v0. X11 uses a desktop-window fallback; placement depends on the desktop environment.

The wallpaper surface is non-interactive so desktop icons and pointer actions pass through it. MPRIS artwork may come from a local file or HTTP(S) URL; remote fetches are asynchronous, time out after eight seconds, and are limited to 12 MiB.

The current track appears on the table sleeve and front display. The three wall frames show the previous three tracks observed during the current Needle session.

## macOS

This release is Linux-only. macOS will need a separate desktop-surface host and playback metadata adapter.
