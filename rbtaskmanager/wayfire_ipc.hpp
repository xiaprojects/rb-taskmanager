// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-RB-Commercial
// Copyright (C) 2026 xiaprojects
//
// Wayfire IPC client (needs the `ipc` + `ipc-rules` plugins; fullscreen and
// always-on-top need `wm-actions`). Protocol: 4-byte little-endian length + JSON,
// see https://github.com/WayfireWM/pywayfire/blob/main/wayfire/ipc.py
// Each call opens its own short-lived connection with a 2 s timeout, so a stuck
// compositor never freezes the GTK main loop for long.
#pragma once

#include <optional>
#include <string>
#include <vector>

struct ViewInfo
{
    int id = -1;
    std::string app_id;
    std::string title;
    std::string role; // "toplevel", "desktop-environment", ...
    bool fullscreen = false;
    bool mapped = true;
};

struct Geometry
{
    int x, y, width, height;
};

class WayfireIPC
{
public:
    // Socket path defaults to $WAYFIRE_SOCKET.
    explicit WayfireIPC(std::string socket_path = {});

    std::vector<ViewInfo> list_views();

    // First mapped toplevel matching all non-empty criteria: exact app-id and/or
    // case-sensitive title substring. Both empty -> no match.
    std::optional<ViewInfo> find_view(const std::string &app_id, const std::string &title_contains);

    // Leaves fullscreen first if `was_fullscreen` (configure-view is ignored for fullscreen views).
    bool set_view_geometry(int view_id, const Geometry &g, bool was_fullscreen = false);
    bool set_view_fullscreen(int view_id, bool fullscreen);
    bool set_view_always_on_top(int view_id, bool always_on_top);
    bool focus_view(int view_id);

    // Raw request/reply. Returns "" and sets last_error on transport errors.
    std::string call(const std::string &json);

    std::string last_error;

private:
    std::string socket_path_;
    bool call_ok(const std::string &json);
};
