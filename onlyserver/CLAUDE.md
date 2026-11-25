# onlyserver (→ package `rb-taskmanager-onlyserver`)

A minimal HTTP server for the Waydroid ↔ Linux host interaction. It runs as a systemd service (port 8180, user `pi`), serves the button page in `www/`, runs power actions and forwards mode requests. It does **not** manage windows (that is `rbtaskmanager/`).

## Files

- `rb-taskmanager-onlyserver.cpp`: argument parsing and API routes only. The HTTP server, static files and process spawning come from `../common/rb_http.{hpp,cpp}`, which is shared with the switcher.
- `www/index.html`: the touch UI shown inside Android. Buttons: NAV, PNL, RDO (→ `/api/mode/0|1|2`), OFF (→ `/api/shutdown`) and RBT (→ `/api/reboot`).
- `shutdown.sh`: `waydroid session stop`, then `curl http://192.168.10.1/shutdown` (RB-01), then `sudo halt`.
- `mode.sh N`: forwards to the switcher with `curl http://127.0.0.1:${RB_SWITCHER_PORT:-8080}/api/mode/N`.
- `rb-taskmanager.service`: the systemd unit. It reads the optional `/etc/default/rb-taskmanager` (for example `RB_SWITCHER_PORT`).
- `Makefile`: `make` builds the binary. `make install DESTDIR=… [PREFIX=/opt/rb-taskmanager] [SYSTEMD_DIR=/usr/lib/systemd/system]` does a staged install with no systemctl. `install-onlyserver` installs straight onto this machine and enables the service.

## Build / run

```sh
make
./rb-taskmanager-onlyserver --api-port 8180 --api-bind 127.0.0.1 --download-root ./www \
    [--mode-cmd ./mode.sh] [--shutdown-cmd CMD] [--reboot-cmd CMD]
make -C .. deb-onlyserver      # → ../dist/rb-taskmanager-onlyserver_<ver>_<arch>.deb
```

## HTTP API

| Path | Action |
|---|---|
| `/api/shutdown`, `/api/stop` | `--shutdown-cmd` (default `/opt/rb-taskmanager/bin/shutdown.sh`), run detached |
| `/api/reboot` | `--reboot-cmd` (default `sudo reboot`), run detached |
| `/api/mode/N` | N must be 1 to 3 digits. Runs `--mode-cmd N` detached → 200. Returns 501 if `--mode-cmd` isn't set, 400 if N is invalid. The response doesn't wait for the switcher. |
| `/api/status` | `{"ok":true,"server":"onlyserver","mode_forwarding":bool}` |
| other `/api/*` | 404 `unknown endpoint` |
| anything else | Serves a static file from `--download-root`, where `/` → `index.html` |

- GET, HEAD, POST and PUT are accepted. Request bodies are ignored.
- There is no authentication, and the server listens on `0.0.0.0`. Anyone on the network can reboot or halt the host. `sudo halt` and `sudo reboot` need passwordless sudo for `pi`.

## Implementation notes

- Exits cleanly on SIGTERM and SIGINT. If bind fails it exits with status 1, and systemd restarts it.
- Shared server (`common/rb_http`):
  - One request at a time, 5 s timeout, reads only up to the request line.
  - Sockets are `CLOEXEC`, so spawned scripts never inherit the listening port.
  - `run_detached` double-forks: no zombies, signals reset, exec errors reported.
  - Paths are URL-decoded, then a leading `/`, any `..` and NUL are rejected. A trailing `/` serves `index.html`.
  - Add new file types in `guess_content_type`.
- When adding an endpoint, add a branch in `handle_api` that returns a `rbhttp::Response`. Returning `std::nullopt` falls through to static files.
