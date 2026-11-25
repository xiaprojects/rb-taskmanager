// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-RB-Commercial
// Copyright (C) 2026 xiaprojects

#include "wayfire_ipc.hpp"
#include "json_mini.hpp"

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace
{

constexpr uint32_t kMaxReply = 16u << 20; // sanity limit

bool send_all(int fd, const void *buf, size_t len)
{
    const char *p = static_cast<const char *>(buf);
    while (len > 0)
    {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

bool recv_all(int fd, void *buf, size_t len)
{
    char *p = static_cast<char *>(buf);
    while (len > 0)
    {
        ssize_t n = recv(fd, p, len, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

// Wayfire uses host byte order in practice, but the protocol is defined as little-endian.
uint32_t to_le(uint32_t v)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return __builtin_bswap32(v);
#else
    return v;
#endif
}

} // namespace

WayfireIPC::WayfireIPC(std::string socket_path) : socket_path_(std::move(socket_path))
{
    if (socket_path_.empty())
    {
        const char *env = std::getenv("WAYFIRE_SOCKET");
        if (env)
            socket_path_ = env;
    }
}

std::string WayfireIPC::call(const std::string &json)
{
    if (socket_path_.empty())
    {
        last_error = "WAYFIRE_SOCKET not set";
        return "";
    }

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
    {
        last_error = std::string("socket: ") + std::strerror(errno);
        return "";
    }
    timeval tv{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        last_error = "connect " + socket_path_ + ": " + std::strerror(errno);
        close(fd);
        return "";
    }

    std::string reply;
    uint32_t len = to_le(static_cast<uint32_t>(json.size()));
    if (!send_all(fd, &len, 4) || !send_all(fd, json.data(), json.size()))
        last_error = "send failed";
    else if (!recv_all(fd, &len, 4))
        last_error = "no reply (timeout or closed)";
    else if ((len = to_le(len)) > kMaxReply)
        last_error = "reply too large";
    else
    {
        reply.resize(len);
        if (!recv_all(fd, reply.data(), len))
        {
            last_error = "truncated reply";
            reply.clear();
        }
    }
    close(fd);
    return reply;
}

bool WayfireIPC::call_ok(const std::string &json)
{
    const std::string r = call(json);
    if (r.empty())
        return false;
    jsonmini::Value v;
    if (!jsonmini::parse(r, v))
    {
        last_error = "invalid JSON reply";
        return false;
    }
    if (v["result"].as_string() == "ok")
        return true;
    last_error = v["error"].as_string(r);
    return false;
}

std::vector<ViewInfo> WayfireIPC::list_views()
{
    std::vector<ViewInfo> result;
    const std::string r = call(R"({"method":"window-rules/list-views"})");
    if (r.empty())
        return result;

    jsonmini::Value v;
    if (!jsonmini::parse(r, v) || !v.is_array())
    {
        last_error = "unexpected list-views reply: " + r.substr(0, 200);
        return result;
    }
    for (const auto &e : v.arr)
    {
        ViewInfo info;
        info.id = e["id"].as_int(-1);
        info.app_id = e["app-id"].as_string();
        info.title = e["title"].as_string();
        info.role = e["role"].as_string();
        info.fullscreen = e["fullscreen"].as_bool(false);
        info.mapped = e["mapped"].as_bool(true);
        if (info.id >= 0)
            result.push_back(std::move(info));
    }
    return result;
}

std::optional<ViewInfo> WayfireIPC::find_view(const std::string &app_id, const std::string &title_contains)
{
    if (app_id.empty() && title_contains.empty())
        return std::nullopt;
    for (auto &v : list_views())
    {
        if (!v.mapped || (!v.role.empty() && v.role != "toplevel"))
            continue;
        if (!app_id.empty() && v.app_id != app_id)
            continue;
        if (!title_contains.empty() && v.title.find(title_contains) == std::string::npos)
            continue;
        return v;
    }
    return std::nullopt;
}

bool WayfireIPC::set_view_geometry(int view_id, const Geometry &g, bool was_fullscreen)
{
    if (was_fullscreen)
        set_view_fullscreen(view_id, false); // best effort; needs wm-actions

    std::ostringstream ss;
    ss << R"({"method":"window-rules/configure-view","data":{"id":)" << view_id
       << R"(,"geometry":{"x":)" << g.x << R"(,"y":)" << g.y
       << R"(,"width":)" << g.width << R"(,"height":)" << g.height << "}}}";
    return call_ok(ss.str());
}

bool WayfireIPC::set_view_fullscreen(int view_id, bool fullscreen)
{
    std::ostringstream ss;
    ss << R"({"method":"wm-actions/set-fullscreen","data":{"view_id":)" << view_id
       << R"(,"state":)" << (fullscreen ? "true" : "false") << "}}";
    return call_ok(ss.str());
}

bool WayfireIPC::set_view_always_on_top(int view_id, bool always_on_top)
{
    std::ostringstream ss;
    ss << R"({"method":"wm-actions/set-always-on-top","data":{"view_id":)" << view_id
       << R"(,"state":)" << (always_on_top ? "true" : "false") << "}}";
    return call_ok(ss.str());
}

bool WayfireIPC::focus_view(int view_id)
{
    std::ostringstream ss;
    ss << R"({"method":"window-rules/focus-view","data":{"id":)" << view_id << "}}";
    return call_ok(ss.str());
}
