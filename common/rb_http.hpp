// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-RB-Commercial
// Copyright (C) 2026 xiaprojects
//
// Minimal HTTP/1.1 server shared by rb-taskmanager-onlyserver and wayfire-layout-switcher.
// One request per connection, handled sequentially on a background thread.
// API routes come from a callback; everything else is served as a static file.
#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace rbhttp
{

struct Request
{
    std::string method; // GET, HEAD, POST, PUT
    std::string path;   // without query string, not URL-decoded
    std::string query;
};

struct Response
{
    int status = 200;
    std::string content_type = "application/json";
    std::string body;
};

// Return a Response to handle the request, std::nullopt to fall through to static files.
using Handler = std::function<std::optional<Response>(const Request &)>;

struct ServerConfig
{
    std::string bind = "0.0.0.0";
    int port = 8080;
    std::string root = "."; // static file root
};

class Server
{
public:
    ~Server();
    // Binds synchronously (returns false and prints the reason on failure),
    // then serves on a background thread.
    bool start(const ServerConfig &cfg, Handler handler);
    void stop();

private:
    void loop();
    void handle_client(int fd);
    void serve_static(int fd, const Request &req);

    ServerConfig cfg_;
    Handler handler_;
    std::atomic<bool> running_{false};
    int fd_ = -1;
    std::thread thread_;
};

// {"ok":false,"error":"<msg>"} with the given status.
Response error(int status, const std::string &msg);
// {"ok":true,"action":"<action>"}
Response ok_action(const std::string &action);

std::string json_escape(const std::string &s);
std::string url_decode(const std::string &s);
bool is_safe_relative_path(const std::string &path);
std::string guess_content_type(const std::string &path);

// Parses "/prefix/N" (N = 1..3 decimal digits). Returns N or -1.
int parse_index_suffix(const std::string &path, const std::string &prefix);

// Runs argv[0] with argv as arguments in its own session, fully detached
// (double fork: no zombies, no SIGCHLD handling needed). Signal dispositions
// are reset and no file descriptors besides 0-2 are inherited (all sockets
// are opened CLOEXEC). Returns false if the first fork fails.
bool run_detached(const std::vector<std::string> &argv);

// run_detached({"/bin/sh", "-c", cmd})
bool run_shell_detached(const std::string &cmd);

} // namespace rbhttp
