# rbtaskmanager (full implementation → package `rb-taskmanager`)

The `wayfire-layout-switcher` binary is a GTK3 + gtk-layer-shell overlay button that switches layouts through Wayfire IPC, plus an HTTP API built on the shared `../common/rb_http`.

## Files

- `wayfire-layout-switcher.cpp`: argument parsing, the `Mode` enum, layout logic (`request_mode` / `apply_mode` / retry), the GTK button and the HTTP routes.
- `wayfire_ipc.{hpp,cpp}`: Wayfire IPC client.
  - One short-lived connection per call, `SOCK_CLOEXEC`, 2 s timeout. Errors are reported in `last_error`.
  - `find_view(app_id, title_substr)` returns mapped toplevels only.
  - `set_view_geometry` takes a view out of fullscreen first.
- `json_mini.hpp`: small JSON parser for IPC replies (escaped titles, nesting). Use it instead of string scanning.
- `CMakeLists.txt`: **the source of truth for the build**, including `install()` rules (prefix `/opt/rb-taskmanager`, config in `/etc/rb-taskmanager`). Also compiles `../common/rb_http.cpp`.
- `Makefile`: thin wrapper around CMake (`make`, `make install DESTDIR=…`, `make clean`, `make dependencies`). The build dir is always `./build`.
- `packaging/`: files installed by the deb:
  - `rb-layout-switcher`: wrapper that sources `/etc/rb-taskmanager/layout-switcher.conf` and execs the binary.
  - `rb-launch-chromium URL`: Chromium kiosk launcher.
  - `layout-switcher.conf`: default config, marked as a conffile.
- `index.html`: debug page. Installed to `/opt/rb-taskmanager/switcher-www`.
- **Not built, dev only:**
  - `web_ipc.cpp`: an old libmicrohttpd version that doesn't compile.
  - `start.sh`, `build.sh`, `launch-app.sh`, `a/b/c.html`: dev samples with paths under `/home/stefano`. They still work with the current command-line options.

## Behaviour

- `enum class Mode { A_FULL=0, B_FULL, C_FULL, AB_SPLIT, _COUNT }`.
- The button cycles A→B→C, adding →A|B with `--enable-combi 1`. From any mode outside the cycle, the next click goes to A. `/api/mode/N` accepts 0–3.
- Window matching: `--app-X-id` must equal the app-id **and** `--app-X-title` must be contained in the title. Empty criteria are ignored, and at least one is required.
- **Missing window**: `--app-X-cmd` runs once (through `rbhttp::run_shell_detached`, double fork, so no zombies or fd leaks). `request_mode` then retries `apply_mode` every 500 ms until the window shows up or `--launch-timeout` (default 20 s) expires. A new mode request cancels the pending retry. An app isn't relaunched until its timeout has passed.
- Output geometry comes from the button's monitor. If the button isn't realized (`--hidden 1`), it falls back to the primary or first monitor.
- AB_SPLIT puts A at `x..split_x` and B on the rest (`split_x` in **pixels**), then focuses both, A last.
- Threads: the GTK thread owns all layout state. The HTTP thread only calls `g_idle_add(request_mode_idle)` and runs read-only IPC for `/api/status`. Never call GTK from the HTTP thread.
- HTTP endpoints:
  - `/api/shutdown|stop`: `--shutdown-cmd`, default `/opt/rb-taskmanager/bin/shutdown.sh` (graceful: stops Waydroid, then RB-01, then halts).
  - `/api/reboot`: `--reboot-cmd`.
  - `/api/mode/N`.
  - `/api/status`: JSON with the current mode, the configured apps, and every Wayfire view (id, app_id, title, role, fullscreen). Returns 503 with `error` if IPC fails.
  - Static files.
- SIGTERM and SIGINT quit cleanly through `g_unix_signal_add`.

## Runtime requirements

- It must run inside the Wayfire session (it needs `WAYLAND_DISPLAY` and `WAYFIRE_SOCKET`). Start it from `[autostart]` in `wayfire.ini` with `/opt/rb-taskmanager/bin/rb-layout-switcher`. If the compositor lacks layer-shell, it exits with an error.
- Wayfire plugins `ipc` and `ipc-rules` are required. `wm-actions` is needed to take windows out of fullscreen.
- The packaged config binds the API to `127.0.0.1:8080`. onlyserver (port 8180) forwards Waydroid's `/api/mode/N` here.

## Testing without the device

You can test against a headless Wayfire:

```sh
WLR_BACKENDS=headless WLR_RENDERER=pixman wayfire -c test.ini   # plugins = ipc ipc-rules wm-actions
export WAYLAND_DISPLAY=wayland-1 WAYFIRE_SOCKET=$XDG_RUNTIME_DIR/wayfire-wayland-1-.socket
```

Any GTK test window works as an app (`g_set_prgname()` sets its app-id). Check geometry with `window-rules/list-views`.

## Known limitations

- Commands run through `/bin/sh -c`, so the API reports `ok` even when the command later fails. Failures only show in the log. Treat the config file as trusted, root-owned input.
- The default app-ids in `layout-switcher.conf` (`Waydroid`, `chrome-192.168.10.1__RB-01-Default`) haven't been checked on the device yet.
