#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-RB-Commercial
# Called by rb-taskmanager-onlyserver as `mode.sh N` for /api/mode/N.
# Forwards the request to the in-session wayfire-layout-switcher (package rb-taskmanager).
PORT="${RB_SWITCHER_PORT:-8080}"
exec curl -fsS -m 3 -o /dev/null "http://127.0.0.1:${PORT}/api/mode/$1"
