# rb-taskmanager

Task switcher and recovery interface for the RB-06 unit (Linux, **Wayland + Wayfire**), which shows a Waydroid Android VM next to the RB-01 web screens.

## What the product does

Cycles the visible window through a fixed set of layouts. For each one it looks for the window by Wayland app-id and/or title substring. If no window matches, it starts the program and applies the layout as soon as the window appears.

Reference layouts:

| Layout | Content |
|---|---|
| A | Waydroid, full screen |
| B | Chromium, full screen, opening `http://192.168.10.1/RB-01` |
| C | Split side by side: Waydroid at 75%, Chromium (`http://192.168.10.1/RB-01`) in the rest |

In code, the modes are `0=A full, 1=B full, 2=C full, 3=A|B split`, and the split width is in pixels.

Use cases:
1. A web interface that lets the Android VM (Waydroid) control the local Linux host: reboot, shutdown, and so on.
2. Switching between the Waydroid window and the other RB-01 remote screens, which run in Chromium.
3. A small GTK button at the TOP of the screen (gtk-layer-shell) for switching layouts.

## Architecture: two packages

```
Waydroid (Android) ──HTTP :8180──▶ rb-taskmanager-onlyserver   (systemd, user pi, 0.0.0.0)
                                      │  /api/shutdown → shutdown.sh   /api/reboot
                                      └─ /api/mode/N → mode.sh ──HTTP 127.0.0.1:8080──▶
                                                       rb-taskmanager (wayfire-layout-switcher,
                                                       started from Wayfire [autostart])
                                                         └─ Wayfire IPC ($WAYFIRE_SOCKET)
```

- `onlyserver/` → deb **rb-taskmanager-onlyserver**. See [onlyserver/CLAUDE.md](onlyserver/CLAUDE.md).
- `rbtaskmanager/` → deb **rb-taskmanager**. See [rbtaskmanager/CLAUDE.md](rbtaskmanager/CLAUDE.md).
- `common/`: `rb_http.{hpp,cpp}`, the HTTP server and detached-process spawning used by **both** binaries. A change here affects both packages.
- `packaging/`: `mkdeb.sh` (turns a staged tree into a .deb with `dpkg-deb`: library dependencies from `dpkg-shlibdeps`, changelog from `git log`, normalised permissions), per-package `control` and maintainer scripts, and the shared `copyright`.
- `VERSION`: the package version used by both debs.

## Build

```sh
make dependencies          # root: g++, cmake, libgtk-3-dev, libgtk-layer-shell-dev
make deb                   # both → dist/*.deb   (or: make deb-onlyserver / make deb-switcher)
make clean
```

- Staging goes in `build/stage/<pkg>` and output in `dist/`. Both are git-ignored.
- Build on the target architecture (arm64 on the Pi), because `dpkg-shlibdeps` pins library versions from the build host.
- Lintian's `dir-or-file-in-opt` errors are expected: `/opt/rb-taskmanager` is hard-coded in the service, the scripts and the config.

## Network and host facts

- RB-01 is at `192.168.10.1`. Its `/shutdown` URL is called during a clean power-off.
- Install prefix: `/opt/rb-taskmanager/{bin,www,switcher-www}`. Config lives in `/etc/rb-taskmanager/` and `/etc/default/rb-taskmanager`.
- Target is Raspberry Pi OS / Debian (`apt`, systemd).

## Conventions

- C++17 with g++. onlyserver uses a plain Makefile. rbtaskmanager uses CMake, with a Makefile wrapper. No external HTTP or JSON libraries.
- API responses are small JSON objects shaped like `{"ok":true,...}` / `{"ok":false,"error":"..."}`.
- License: dual **GPL-3.0-only OR LicenseRef-RB-Commercial** (`LICENSE`, `COPYING`, `LICENSE-COMMERCIAL.md`). New source files start with the SPDX header line.
- Don't run `make install*`, `dpkg -i` or `systemctl` unless asked. They change the system.
- Makefile `clean` targets must use literal paths with `rm -rf` (`./build`), never variables.
