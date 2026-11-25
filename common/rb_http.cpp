// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-RB-Commercial
// Copyright (C) 2026 xiaprojects

#include "rb_http.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace rbhttp
{

// ── helpers ──────────────────────────────────────────────────────────────────

static const char *status_text(int status)
{
    switch (status)
    {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    default:  return "Unknown";
    }
}

static bool send_all(int fd, const void *buf, size_t len)
{
    const char *p = static_cast<const char *>(buf);
    while (len > 0)
    {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (n == 0)
            return false;
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

static std::string header(int status, const std::string &content_type, long long length,
                          const std::string &extra = {})
{
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " " << status_text(status) << "\r\n"
        << "Connection: close\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << length << "\r\n"
        << "Cache-Control: no-store\r\n"
        << extra
        << "\r\n";
    return oss.str();
}

static bool send_response(int fd, const Response &r, bool head_only)
{
    const std::string hdr = header(r.status, r.content_type, static_cast<long long>(r.body.size()));
    if (!send_all(fd, hdr.data(), hdr.size()))
        return false;
    return head_only || send_all(fd, r.body.data(), r.body.size());
}

Response error(int status, const std::string &msg)
{
    return {status, "application/json", "{\"ok\":false,\"error\":\"" + json_escape(msg) + "\"}"};
}

Response ok_action(const std::string &action)
{
    return {200, "application/json", "{\"ok\":true,\"action\":\"" + json_escape(action) + "\"}"};
}

std::string json_escape(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            else
                out += static_cast<char>(c);
        }
    }
    return out;
}

std::string url_decode(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) &&
            isxdigit((unsigned char)s[i + 2]))
        {
            char hex[3] = {s[i + 1], s[i + 2], 0};
            out.push_back(static_cast<char>(std::strtol(hex, nullptr, 16)));
            i += 2;
        }
        else if (s[i] == '+')
            out.push_back(' ');
        else
            out.push_back(s[i]);
    }
    return out;
}

bool is_safe_relative_path(const std::string &path)
{
    return !path.empty() && path[0] != '/' && path.find("..") == std::string::npos &&
           path.find('\0') == std::string::npos;
}

std::string guess_content_type(const std::string &path)
{
    auto pos = path.rfind('.');
    std::string ext = (pos == std::string::npos) ? "" : path.substr(pos + 1);
    for (auto &c : ext)
        c = static_cast<char>(tolower((unsigned char)c));
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css")                  return "text/css; charset=utf-8";
    if (ext == "js" || ext == "mjs")   return "text/javascript; charset=utf-8";
    if (ext == "json")                 return "application/json";
    if (ext == "txt" || ext == "log" || ext == "csv") return "text/plain; charset=utf-8";
    if (ext == "svg")                  return "image/svg+xml";
    if (ext == "png")                  return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif")                  return "image/gif";
    if (ext == "webp")                 return "image/webp";
    if (ext == "ico")                  return "image/x-icon";
    if (ext == "woff2")                return "font/woff2";
    if (ext == "wasm")                 return "application/wasm";
    if (ext == "pdf")                  return "application/pdf";
    return "application/octet-stream";
}

int parse_index_suffix(const std::string &path, const std::string &prefix)
{
    if (path.compare(0, prefix.size(), prefix) != 0)
        return -1;
    const std::string p = path.substr(prefix.size());
    if (p.empty() || p.size() > 3)
        return -1;
    for (char c : p)
        if (c < '0' || c > '9')
            return -1;
    return std::atoi(p.c_str());
}

bool run_detached(const std::vector<std::string> &argv)
{
    if (argv.empty())
        return false;

    // Build argv before fork: only async-signal-safe calls are allowed in the
    // child of a multithreaded process (no malloc).
    std::vector<char *> args;
    for (auto &a : argv)
        args.push_back(const_cast<char *>(a.c_str()));
    args.push_back(nullptr);

    // Pipe reports exec failure from the grandchild (closed by CLOEXEC on success).
    int pfd[2];
    if (pipe2(pfd, O_CLOEXEC) != 0)
        return false;

    pid_t pid = fork();
    if (pid < 0)
    {
        close(pfd[0]);
        close(pfd[1]);
        return false;
    }

    if (pid == 0)
    {
        setsid();
        pid_t gpid = fork();
        if (gpid != 0)
            _exit(gpid < 0 ? 1 : 0);

        // Grandchild: clean signal state for the new program.
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        signal(SIGPIPE, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        execv(args[0], args.data());

        int err = errno;
        (void)!write(pfd[1], &err, sizeof(err));
        _exit(127);
    }

    close(pfd[1]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
    {
    }
    int err = 0;
    ssize_t n;
    while ((n = read(pfd[0], &err, sizeof(err))) < 0 && errno == EINTR)
    {
    }
    close(pfd[0]);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return false;
    if (n == sizeof(err))
    {
        std::fprintf(stderr, "exec %s failed: %s\n", argv[0].c_str(), std::strerror(err));
        return false;
    }
    return true;
}

bool run_shell_detached(const std::string &cmd)
{
    return run_detached({"/bin/sh", "-c", cmd});
}

// ── Server ───────────────────────────────────────────────────────────────────

Server::~Server() { stop(); }

bool Server::start(const ServerConfig &cfg, Handler handler)
{
    cfg_ = cfg;
    handler_ = std::move(handler);

    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
    {
        std::perror("socket");
        return false;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(cfg_.port));
    if (inet_pton(AF_INET, cfg_.bind.c_str(), &addr.sin_addr) != 1)
    {
        std::fprintf(stderr, "Invalid bind address: %s\n", cfg_.bind.c_str());
        close(fd);
        return false;
    }
    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
    {
        std::fprintf(stderr, "bind %s:%d: %s\n", cfg_.bind.c_str(), cfg_.port, std::strerror(errno));
        close(fd);
        return false;
    }
    if (listen(fd, 16) != 0)
    {
        std::perror("listen");
        close(fd);
        return false;
    }

    fd_ = fd;
    running_ = true;
    thread_ = std::thread(&Server::loop, this);
    std::fprintf(stderr, "HTTP server listening on %s:%d, static root %s\n",
                 cfg_.bind.c_str(), cfg_.port, cfg_.root.c_str());
    return true;
}

void Server::stop()
{
    if (!running_.exchange(false))
        return;
    if (fd_ >= 0)
        shutdown(fd_, SHUT_RDWR); // wakes accept()
    if (thread_.joinable())
        thread_.join();
    if (fd_ >= 0)
        close(fd_);
    fd_ = -1;
}

void Server::loop()
{
    while (running_.load())
    {
        int client = accept4(fd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0)
        {
            if (errno == EINTR || errno == ECONNABORTED)
                continue;
            if (!running_.load())
                break;
            std::perror("accept");
            usleep(100 * 1000); // avoid a busy loop on e.g. EMFILE
            continue;
        }
        handle_client(client);
        close(client);
    }
}

void Server::handle_client(int fd)
{
    timeval tv{5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    // Read until the end of the request line (bodies are ignored).
    std::string req;
    char buf[2048];
    while (req.find('\n') == std::string::npos && req.size() < 8192)
    {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        req.append(buf, static_cast<size_t>(n));
    }
    size_t eol = req.find('\n');
    if (eol == std::string::npos)
    {
        if (!req.empty())
            send_response(fd, error(400, "malformed request"), false);
        return;
    }

    std::istringstream iss(req.substr(0, eol));
    Request r;
    std::string target, version;
    iss >> r.method >> target >> version;
    if (r.method.empty() || target.empty() || version.compare(0, 5, "HTTP/") != 0)
    {
        send_response(fd, error(400, "invalid request line"), false);
        return;
    }
    const bool head = (r.method == "HEAD");
    if (r.method != "GET" && !head && r.method != "POST" && r.method != "PUT")
    {
        send_response(fd, error(405, "method not allowed"), head);
        return;
    }

    size_t q = target.find('?');
    r.path = (q == std::string::npos) ? target : target.substr(0, q);
    r.query = (q == std::string::npos) ? "" : target.substr(q + 1);

    if (handler_)
    {
        if (auto resp = handler_(r))
        {
            send_response(fd, *resp, head);
            return;
        }
    }
    if (r.path.compare(0, 5, "/api/") == 0)
    {
        send_response(fd, error(404, "unknown endpoint"), head);
        return;
    }
    serve_static(fd, r);
}

void Server::serve_static(int fd, const Request &r)
{
    const bool head = (r.method == "HEAD");
    std::string rel = url_decode(r.path.substr(1));
    if (rel.empty() || rel.back() == '/')
        rel += "index.html";

    if (!is_safe_relative_path(rel))
    {
        send_response(fd, error(400, "invalid file path"), head);
        return;
    }

    std::string full = cfg_.root;
    if (!full.empty() && full.back() != '/')
        full += '/';
    full += rel;

    int file = open(full.c_str(), O_RDONLY | O_CLOEXEC);
    struct stat st;
    if (file < 0 || fstat(file, &st) != 0 || !S_ISREG(st.st_mode))
    {
        if (file >= 0)
            close(file);
        send_response(fd, {404, "application/json",
                           "{\"ok\":false,\"error\":\"file not found\",\"file\":\"" + json_escape(rel) + "\"}"},
                      head);
        return;
    }

    const std::string hdr = header(200, guess_content_type(full), static_cast<long long>(st.st_size));
    if (send_all(fd, hdr.data(), hdr.size()) && !head)
    {
        char buf[8192];
        for (;;)
        {
            ssize_t n = read(file, buf, sizeof(buf));
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0 || !send_all(fd, buf, static_cast<size_t>(n)))
                break;
        }
    }
    close(file);
}

} // namespace rbhttp
