# rb-taskmanager
Task manager and recovery interface for RB-01.

## Build

```sh
make dependencies   # once, as root: compilers and GTK3 / gtk-layer-shell dev packages
make deb            # builds dist/rb-taskmanager-onlyserver_*.deb and dist/rb-taskmanager_*.deb
```

| Package | Contents |
|---|---|
| `rb-taskmanager-onlyserver` | systemd HTTP server on port 8180, used by Waydroid for shutdown, reboot and layout requests |
| `rb-taskmanager` | Wayfire layout switcher: overlay button plus localhost API on port 8080. Start it from `[autostart]` in `wayfire.ini` |

Build the packages on the target architecture (for example on the device itself).

## License

Dual-licensed: **GPL-3.0-only** ([COPYING](COPYING)) **or** a commercial license ([LICENSE-COMMERCIAL.md](LICENSE-COMMERCIAL.md)). See [LICENSE](LICENSE).
