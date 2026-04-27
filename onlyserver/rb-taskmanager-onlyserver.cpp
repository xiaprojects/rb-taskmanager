
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

struct Config
{
    int api_port = 8080;
    std::string api_bind = "0.0.0.0";
    std::string download_root = ".";
};


static Config g_cfg;

static std::atomic<bool> g_server_running{true};
static int g_server_fd = -1;
static std::thread g_server_thread;

static bool parse_int_arg(const char *value, int &out)
{
    char *end = nullptr;
    long v = std::strtol(value, &end, 10);
    if (!value || *value == '\0' || !end || *end != '\0')
        return false;
    out = static_cast<int>(v);
    return true;
}

static void print_usage(const char *progname)
{
    std::fprintf(stderr,
                 "Usage:\n"
                 " %s [--api-port N] [--api-bind ADDR] [--download-root DIR]\n\n"
                 "Example:\n"
                 " %s --api-port 8080 --api-bind 127.0.0.1 --download-root /opt/rb-taskmanager/www\n",
                 progname, progname);
}

static std::string json_escape(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

static bool parse_args(int argc, char *argv[], Config &cfg)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        auto require_value = [&](const char *name) -> const char *
        {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "Missing value for %s\n", name);
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--api-port")
        {
            const char *v = require_value("--api-port");
            if (!v || !parse_int_arg(v, cfg.api_port))
                return false;
        }
        else if (arg == "--api-bind")
        {
            const char *v = require_value("--api-bind");
            if (!v)
                return false;
            cfg.api_bind = v;
        }
        else if (arg == "--download-root")
        {
            const char *v = require_value("--download-root");
            if (!v)
                return false;
            cfg.download_root = v;
        }
        else if (arg == "--help" || arg == "-h")
        {
            print_usage(argv[0]);
            std::exit(0);
        }
        else
        {
            std::fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
            return false;
        }
    }

    if (cfg.api_port <= 0 || cfg.api_port > 65535)
    {
        std::fprintf(stderr, "api-port must be in range 1..65535\n");
        return false;
    }

    return true;
}

static bool run_command_detached(const char *cmd, const char *arg)
{
    pid_t pid = fork();
    if (pid < 0)
        return false;

    if (pid == 0)
    {
        setsid();
        execl(cmd, cmd, arg, (char *)nullptr);
        _exit(127);
    }

    return true;
}


static bool send_all(int fd, const void *buf, size_t len)
{
    const char *p = static_cast<const char *>(buf);
    while (len > 0)
    {
        ssize_t n = send(fd, p, len, 0);
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

static bool send_response(int fd,
                          int status,
                          const char *status_text,
                          const std::string &content_type,
                          const std::string &body)
{
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " " << status_text << "\r\n"
        << "Connection: close\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Cache-Control: no-store\r\n"
        << "\r\n";
    const std::string hdr = oss.str();
    return send_all(fd, hdr.data(), hdr.size()) && send_all(fd, body.data(), body.size());
}

static std::string guess_content_type(const std::string &path)
{
    auto pos = path.rfind('.');
    std::string ext = (pos == std::string::npos) ? "" : path.substr(pos + 1);
    if (ext == "txt" || ext == "log" || ext == "csv")
        return "text/plain";
    if (ext == "json")
        return "application/json";
    if (ext == "pdf")
        return "application/pdf";
    if (ext == "png")
        return "image/png";
    if (ext == "jpg" || ext == "jpeg")
        return "image/jpeg";
    if (ext == "html")
        return "text/html";
    return "application/octet-stream";
}

static std::string url_decode(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '%' && i + 2 < s.size())
        {
            char hex[3] = {s[i + 1], s[i + 2], 0};
            char *end = nullptr;
            long v = std::strtol(hex, &end, 16);
            if (end && *end == '\0')
            {
                out.push_back(static_cast<char>(v));
                i += 2;
                continue;
            }
        }
        if (s[i] == '+')
            out.push_back(' ');
        else
            out.push_back(s[i]);
    }
    return out;
}

static bool is_safe_relative_path(const std::string &path)
{
    if (path.empty())
        return false;
    if (path[0] == '/')
        return false;
    if (path.find("..") != std::string::npos)
        return false;
    if (path.find('\0') != std::string::npos)
        return false;
    return true;
}

static bool send_file_response(int fd, const std::string &full_path, const std::string &download_name)
{
    int file_fd = open(full_path.c_str(), O_RDONLY);
    if (file_fd < 0)
        return false;

    struct stat st;
    if (fstat(file_fd, &st) != 0 || !S_ISREG(st.st_mode))
    {
        close(file_fd);
        return false;
    }

    std::ostringstream oss;
    oss << "HTTP/1.1 200 OK\r\n"
        << "Connection: close\r\n"
        << "Content-Type: " << guess_content_type(full_path) << "\r\n"
        << "Content-Length: " << st.st_size << "\r\n"
        << "Content-Disposition: inline; filename=\"" << download_name << "\"\r\n"
        << "Cache-Control: no-store\r\n"
        << "\r\n";
    const std::string hdr = oss.str();
    if (!send_all(fd, hdr.data(), hdr.size()))
    {
        close(file_fd);
        return false;
    }

    char buf[8192];
    while (true)
    {
        ssize_t n = read(file_fd, buf, sizeof(buf));
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            close(file_fd);
            return false;
        }
        if (n == 0)
            break;
        if (!send_all(fd, buf, static_cast<size_t>(n)))
        {
            close(file_fd);
            return false;
        }
    }

    close(file_fd);
    return true;
}

static void handle_client(int client_fd)
{
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    char buf[4096 + 1];
    ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0)
        return;
    buf[n] = '\0';

    std::string req(buf, static_cast<size_t>(n));
    std::string first_line;
    size_t eol = req.find("\r\n");
    if (eol == std::string::npos)
        eol = req.find('\n');
    if (eol == std::string::npos)
    {
        send_response(client_fd, 400, "Bad Request", "application/json",
                      "{\"ok\":false,\"error\":\"malformed request\"}");
        return;
    }
    first_line = req.substr(0, eol);

    std::istringstream iss(first_line);
    std::string method, target, version;
    iss >> method >> target >> version;
    if (method.empty() || target.empty() || version.empty())
    {
        send_response(client_fd, 400, "Bad Request", "application/json",
                      "{\"ok\":false,\"error\":\"invalid request line\"}");
        return;
    }

    if (method != "GET" && method != "POST" && method != "PUT")
    {
        send_response(client_fd, 405, "Method Not Allowed", "application/json",
                      "{\"ok\":false,\"error\":\"only GET supported\"}");
        return;
    }

    size_t qpos = target.find('?');
    std::string path = (qpos == std::string::npos) ? target : target.substr(0, qpos);
    std::string query = (qpos == std::string::npos) ? "" : target.substr(qpos + 1);

    if (path == "/api/shutdown" || path == "/api/stop")
    {
        bool ok = run_command_detached("/opt/rb-taskmanager/bin/shutdown.sh","");
        send_response(client_fd,
                      ok ? 200 : 500,
                      ok ? "OK" : "Internal Server Error",
                      "application/json",
                      ok ? "{\"ok\":true,\"action\":\"shutdown\"}" : "{\"ok\":false,\"error\":\"failed to launch halt\"}");
        return;
    }

    if (path == "/api/reboot")
    {
        bool ok = run_command_detached("/usr/bin/sudo", "/usr/sbin/reboot");
        send_response(client_fd,
                      ok ? 200 : 500,
                      ok ? "OK" : "Internal Server Error",
                      "application/json",
                      ok ? "{\"ok\":true,\"action\":\"reboot\"}" : "{\"ok\":false,\"error\":\"failed to launch reboot\"}");
        return;
    }


    if (true)
    {
        std::string file_param;
        const std::string prefix = "/";
        file_param = url_decode(path.substr(prefix.size()));

        if (file_param == "")
        {
            file_param = "index.html";
        }

        if (!is_safe_relative_path(file_param))
        {
            send_response(client_fd, 400, "Bad Request", "application/json",
                          "{\"ok\":false,\"error\":\"invalid file path\"}");
            return;
        }

        std::string full_path = g_cfg.download_root;
        if (!full_path.empty() && full_path.back() != '/')
            full_path += '/';
        full_path += file_param;

        struct stat st;
        if (stat(full_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
        {
            std::string body = "{\"ok\":false,\"error\":\"file not found\",\"file\":\"" + json_escape(file_param) + "\"}";
            send_response(client_fd, 404, "Not Found", "application/json", body);
            return;
        }

        if (!send_file_response(client_fd, full_path, file_param))
        {
            send_response(client_fd, 500, "Internal Server Error", "application/json",
                          "{\"ok\":false,\"error\":\"failed to send file\"}");
        }
        return;
    }

    send_response(client_fd, 404, "Not Found", "application/json",
                  "{\"ok\":false,\"error\":\"not found\"}");
}

static void api_server_loop()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        std::perror("socket");
        return;
    }

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(g_cfg.api_port));
    if (inet_pton(AF_INET, g_cfg.api_bind.c_str(), &addr.sin_addr) != 1)
    {
        std::fprintf(stderr, "Invalid api-bind address: %s\n", g_cfg.api_bind.c_str());
        close(fd);
        return;
    }

    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
    {
        std::perror("bind");
        close(fd);
        return;
    }

    if (listen(fd, 16) != 0)
    {
        std::perror("listen");
        close(fd);
        return;
    }

    g_server_fd = fd;
    std::fprintf(stderr, "API server listening on %s:%d for %s\n", g_cfg.api_bind.c_str(), g_cfg.api_port,g_cfg.download_root.c_str());

    while (g_server_running.load())
    {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(fd, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
        if (client_fd < 0)
        {
            if (errno == EINTR)
                continue;
            if (!g_server_running.load())
                break;
            std::perror("accept");
            continue;
        }

        handle_client(client_fd);
        close(client_fd);
    }

    close(fd);
    g_server_fd = -1;
}

static void start_api_server()
{
    g_server_running = true;
    g_server_thread = std::thread(api_server_loop);
}

static void stop_api_server()
{
    g_server_running = false;
    if (g_server_fd >= 0)
        shutdown(g_server_fd, SHUT_RDWR);
    if (g_server_thread.joinable())
        g_server_thread.join();
}


int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);

    if (!parse_args(argc, argv, g_cfg))
    {
        print_usage(argv[0]);
        return 1;
    }


    start_api_server();

    while(g_server_running)
    {
        sleep(1);
    }

    return 0;
}
