// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-RB-Commercial
// Copyright (C) 2026 xiaprojects
//
// rb-taskmanager-onlyserver: HTTP server that lets the Waydroid Android VM control the host.
// No window management: /api/mode/N is forwarded to --mode-cmd (see mode.sh).

#include "../common/rb_http.hpp"

#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>

struct Config
{
    rbhttp::ServerConfig http{"0.0.0.0", 8080, "."};
    std::string shutdown_cmd = "/opt/rb-taskmanager/bin/shutdown.sh";
    std::string reboot_cmd = "/usr/bin/sudo /usr/sbin/reboot";
    std::string mode_cmd; // executable, called as `mode_cmd N`
};

static Config g_cfg;
static volatile sig_atomic_t g_quit = 0;

static void print_usage(const char *progname)
{
    std::fprintf(stderr,
                 "Usage:\n"
                 " %s [--api-port N] [--api-bind ADDR] [--download-root DIR]\n"
                 "    [--mode-cmd EXE] [--shutdown-cmd CMD] [--reboot-cmd CMD]\n\n"
                 "  --mode-cmd EXE      run as `EXE N` for /api/mode/N (unset: 501)\n"
                 "  --shutdown-cmd CMD  shell command for /api/shutdown (default %s)\n"
                 "  --reboot-cmd CMD    shell command for /api/reboot (default %s)\n\n"
                 "Example:\n"
                 " %s --api-port 8180 --api-bind 0.0.0.0 --download-root /opt/rb-taskmanager/www \\\n"
                 "    --mode-cmd /opt/rb-taskmanager/bin/mode.sh\n",
                 progname, g_cfg.shutdown_cmd.c_str(), g_cfg.reboot_cmd.c_str(), progname);
}

static bool parse_args(int argc, char *argv[], Config &cfg)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            print_usage(argv[0]);
            std::exit(0);
        }
        if (i + 1 >= argc)
        {
            std::fprintf(stderr, "Missing value for %s\n", arg.c_str());
            return false;
        }
        const char *v = argv[++i];
        if (arg == "--api-port")
        {
            char *end = nullptr;
            long p = std::strtol(v, &end, 10);
            if (*v == '\0' || *end != '\0' || p <= 0 || p > 65535)
            {
                std::fprintf(stderr, "--api-port must be in range 1..65535\n");
                return false;
            }
            cfg.http.port = static_cast<int>(p);
        }
        else if (arg == "--api-bind")
            cfg.http.bind = v;
        else if (arg == "--download-root")
            cfg.http.root = v;
        else if (arg == "--mode-cmd")
            cfg.mode_cmd = v;
        else if (arg == "--shutdown-cmd")
            cfg.shutdown_cmd = v;
        else if (arg == "--reboot-cmd")
            cfg.reboot_cmd = v;
        else
        {
            std::fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
            return false;
        }
    }
    return true;
}

static std::optional<rbhttp::Response> handle_api(const rbhttp::Request &r)
{
    if (r.path == "/api/shutdown" || r.path == "/api/stop")
    {
        return rbhttp::run_shell_detached(g_cfg.shutdown_cmd)
                   ? rbhttp::ok_action("shutdown")
                   : rbhttp::error(500, "failed to launch shutdown command");
    }

    if (r.path == "/api/reboot")
    {
        return rbhttp::run_shell_detached(g_cfg.reboot_cmd)
                   ? rbhttp::ok_action("reboot")
                   : rbhttp::error(500, "failed to launch reboot command");
    }

    if (r.path.compare(0, 10, "/api/mode/") == 0)
    {
        int mode = rbhttp::parse_index_suffix(r.path, "/api/mode/");
        if (mode < 0)
            return rbhttp::error(400, "invalid mode");
        if (g_cfg.mode_cmd.empty())
            return rbhttp::error(501, "mode switching not configured (--mode-cmd)");
        if (!rbhttp::run_detached({g_cfg.mode_cmd, std::to_string(mode)}))
            return rbhttp::error(500, "failed to launch mode command");
        return rbhttp::Response{200, "application/json",
                                "{\"ok\":true,\"action\":\"mode\",\"mode\":" + std::to_string(mode) + "}"};
    }

    if (r.path == "/api/status")
        return rbhttp::Response{200, "application/json",
                                std::string("{\"ok\":true,\"server\":\"onlyserver\",\"mode_forwarding\":") +
                                    (g_cfg.mode_cmd.empty() ? "false" : "true") + "}"};

    return std::nullopt;
}

static void on_signal(int) { g_quit = 1; }

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);
    if (!parse_args(argc, argv, g_cfg))
    {
        print_usage(argv[0]);
        return 1;
    }

    rbhttp::Server server;
    if (!server.start(g_cfg.http, handle_api))
        return 1; // systemd restarts us (Restart=always)

    struct sigaction sa{};
    sa.sa_handler = on_signal;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    while (!g_quit)
        pause();

    server.stop();
    return 0;
}
