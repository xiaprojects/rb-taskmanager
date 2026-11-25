// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-RB-Commercial
// Copyright (C) 2026 xiaprojects
//
// wayfire-layout-switcher: overlay button (GTK3 + gtk-layer-shell) that cycles the
// visible layout via Wayfire IPC, plus a small HTTP API (/api/mode/N, /api/status, ...).
//
// Threads: GTK main loop owns all UI and layout state. The HTTP server thread only
// posts mode requests with g_idle_add(), and queries Wayfire directly for /api/status.

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <glib-unix.h>
#include <gtk-layer-shell.h>

#include <signal.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>

#include "../common/rb_http.hpp"
#include "wayfire_ipc.hpp"

enum class Mode
{
    A_FULL = 0,
    B_FULL,
    C_FULL,
    AB_SPLIT,
    _COUNT
};
constexpr int kModeCount = static_cast<int>(Mode::_COUNT);

struct AppConfig
{
    std::string letter;
    std::string app_id;         // exact Wayland app-id (optional if title given)
    std::string title;          // title substring (optional if app_id given)
    std::string startup_cmd;    // /bin/sh -c command
    gint64 last_launch_us = 0;  // monotonic time of last launch (GTK thread only)
};

struct Config
{
    int button_x = 10;
    int button_y = 10;
    int button_w = 80;
    int button_h = 36;
    int split_x = 280;
    int hidden = 0;
    int enable_combi = 0;
    int launch_timeout_s = 20; // wait this long for a launched app before giving up / relaunching
    int verbose = 0;

    rbhttp::ServerConfig http{"0.0.0.0", 8080, "."};
    std::string shutdown_cmd = "/opt/rb-taskmanager/bin/shutdown.sh";
    std::string reboot_cmd = "/usr/bin/sudo /usr/sbin/reboot";

    std::string mode_labels[kModeCount] = {"[ A ]", "[ B ]", "[ C ]", "[ A|B ]"};
    AppConfig app[3];
};

static Config g_cfg;
static std::atomic<int> g_mode{static_cast<int>(Mode::A_FULL)};
static GtkWidget *g_button = nullptr;
static GtkWidget *g_window = nullptr;
static guint g_retry_source = 0;
static gint64 g_retry_deadline_us = 0;

#define LOG(...) std::fprintf(stderr, "layout-switcher: " __VA_ARGS__)
#define VLOG(...) do { if (g_cfg.verbose) LOG(__VA_ARGS__); } while (0)

// ── Arguments ────────────────────────────────────────────────────────────────

static void print_usage(const char *progname)
{
    std::fprintf(stderr,
                 "Usage: %s [options]\n"
                 "Apps (X = a, b, c; each needs a command and an id and/or title):\n"
                 "  --app-X-id ID        exact Wayland app-id\n"
                 "  --app-X-title TEXT   window title substring\n"
                 "  --app-X-cmd CMD      shell command that starts the app\n"
                 "Layout / button:\n"
                 "  --split-x N          width of A in A|B split (px)\n"
                 "  --enable-combi 0|1   include A|B split in the button cycle\n"
                 "  --label-a|-b|-c|-combi TEXT\n"
                 "  --button-x N --button-y N --button-w N --button-h N\n"
                 "  --hidden 0|1         start without showing the button\n"
                 "  --launch-timeout S   seconds to wait for a launched app (default 20)\n"
                 "HTTP API:\n"
                 "  --api-port N --api-bind ADDR --download-root DIR\n"
                 "  --shutdown-cmd CMD   (default /opt/rb-taskmanager/bin/shutdown.sh)\n"
                 "  --reboot-cmd CMD     (default sudo reboot)\n"
                 "Other:\n"
                 "  --verbose 0|1\n",
                 progname);
}

static bool parse_int(const char *v, int &out, int min, int max)
{
    char *end = nullptr;
    long n = std::strtol(v, &end, 10);
    if (*v == '\0' || *end != '\0' || n < min || n > max)
        return false;
    out = static_cast<int>(n);
    return true;
}

static bool parse_args(int argc, char *argv[], Config &cfg)
{
    cfg.app[0].letter = "a";
    cfg.app[1].letter = "b";
    cfg.app[2].letter = "c";

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

        auto int_opt = [&](int &out, int min, int max)
        {
            if (parse_int(v, out, min, max))
                return true;
            std::fprintf(stderr, "Invalid value for %s: %s (expected %d..%d)\n", arg.c_str(), v, min, max);
            return false;
        };

        // --app-{a,b,c}-{id,title,cmd}
        if (arg.size() > 8 && arg.compare(0, 6, "--app-") == 0 && arg[7] == '-' &&
            arg[6] >= 'a' && arg[6] <= 'c')
        {
            AppConfig &app = cfg.app[arg[6] - 'a'];
            const std::string field = arg.substr(8);
            if (field == "id")
                app.app_id = v;
            else if (field == "title")
                app.title = v;
            else if (field == "cmd")
                app.startup_cmd = v;
            else
            {
                std::fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
                return false;
            }
            continue;
        }

        bool ok = true;
        if (arg == "--label-a") cfg.mode_labels[0] = v;
        else if (arg == "--label-b") cfg.mode_labels[1] = v;
        else if (arg == "--label-c") cfg.mode_labels[2] = v;
        else if (arg == "--label-combi") cfg.mode_labels[3] = v;
        else if (arg == "--hidden") ok = int_opt(cfg.hidden, 0, 1);
        else if (arg == "--enable-combi") ok = int_opt(cfg.enable_combi, 0, 1);
        else if (arg == "--verbose") ok = int_opt(cfg.verbose, 0, 1);
        else if (arg == "--button-x") ok = int_opt(cfg.button_x, -100000, 100000);
        else if (arg == "--button-y") ok = int_opt(cfg.button_y, -100000, 100000);
        else if (arg == "--button-w") ok = int_opt(cfg.button_w, 1, 100000);
        else if (arg == "--button-h") ok = int_opt(cfg.button_h, 1, 100000);
        else if (arg == "--split-x") ok = int_opt(cfg.split_x, 0, 100000);
        else if (arg == "--launch-timeout") ok = int_opt(cfg.launch_timeout_s, 1, 600);
        else if (arg == "--api-port") ok = int_opt(cfg.http.port, 1, 65535);
        else if (arg == "--api-bind") cfg.http.bind = v;
        else if (arg == "--download-root") cfg.http.root = v;
        else if (arg == "--shutdown-cmd") cfg.shutdown_cmd = v;
        else if (arg == "--reboot-cmd") cfg.reboot_cmd = v;
        else
        {
            std::fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
            return false;
        }
        if (!ok)
            return false;
    }

    for (const auto &app : cfg.app)
    {
        if (app.startup_cmd.empty() || (app.app_id.empty() && app.title.empty()))
        {
            std::fprintf(stderr, "App %s needs --app-%s-cmd and --app-%s-id and/or --app-%s-title\n",
                         app.letter.c_str(), app.letter.c_str(), app.letter.c_str(), app.letter.c_str());
            return false;
        }
    }
    return true;
}

// ── Layout ───────────────────────────────────────────────────────────────────

static Geometry output_geometry()
{
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = nullptr;
    // The button's monitor if it is realized; otherwise (e.g. --hidden 1) primary / first.
    if (GdkWindow *w = g_window ? gtk_widget_get_window(g_window) : nullptr)
        monitor = gdk_display_get_monitor_at_window(display, w);
    if (!monitor)
        monitor = gdk_display_get_primary_monitor(display);
    if (!monitor && gdk_display_get_n_monitors(display) > 0)
        monitor = gdk_display_get_monitor(display, 0);

    GdkRectangle r{0, 0, 0, 0};
    if (monitor)
        gdk_monitor_get_geometry(monitor, &r);
    return {r.x, r.y, r.width, r.height};
}

// Returns the view if present. Otherwise launches the app (at most once per
// launch_timeout) and returns nullopt.
static std::optional<ViewInfo> find_or_launch(WayfireIPC &ipc, AppConfig &app)
{
    ipc.last_error.clear();
    if (auto v = ipc.find_view(app.app_id, app.title))
        return v;

    if (!ipc.last_error.empty())
        LOG("IPC: %s\n", ipc.last_error.c_str());

    const gint64 now = g_get_monotonic_time();
    if (app.last_launch_us == 0 || now - app.last_launch_us >= gint64(g_cfg.launch_timeout_s) * G_USEC_PER_SEC)
    {
        LOG("app %s (id='%s' title='%s') not found, launching: %s\n", app.letter.c_str(),
            app.app_id.c_str(), app.title.c_str(), app.startup_cmd.c_str());
        if (!rbhttp::run_shell_detached(app.startup_cmd))
            LOG("failed to launch app %s\n", app.letter.c_str());
        app.last_launch_us = now;
    }
    return std::nullopt;
}

static bool place(WayfireIPC &ipc, const ViewInfo &v, const Geometry &g, bool focus)
{
    bool ok = ipc.set_view_geometry(v.id, g, v.fullscreen);
    if (!ok)
        LOG("configure-view %d failed: %s\n", v.id, ipc.last_error.c_str());
    if (focus && !ipc.focus_view(v.id))
        LOG("focus-view %d failed: %s\n", v.id, ipc.last_error.c_str());
    return ok;
}

// Applies `mode`. Returns true when every window of the layout was found and placed.
static bool apply_mode(Mode mode)
{
    const Geometry out = output_geometry();
    if (out.width <= 0 || out.height <= 0)
    {
        LOG("no output geometry yet\n");
        return false;
    }
    WayfireIPC ipc;

    switch (mode)
    {
    case Mode::A_FULL:
    case Mode::B_FULL:
    case Mode::C_FULL:
    {
        auto v = find_or_launch(ipc, g_cfg.app[static_cast<int>(mode)]);
        return v && place(ipc, *v, out, true);
    }
    case Mode::AB_SPLIT:
    {
        const int split = std::min(std::max(g_cfg.split_x, 0), out.width);
        auto a = find_or_launch(ipc, g_cfg.app[0]);
        auto b = find_or_launch(ipc, g_cfg.app[1]);
        bool ok = a && b;
        if (b)
            ok = place(ipc, *b, {out.x + split, out.y, out.width - split, out.height}, true) && ok;
        if (a)
            ok = place(ipc, *a, {out.x, out.y, split, out.height}, true) && ok;
        return ok;
    }
    default:
        return false;
    }
}

static gboolean retry_apply(gpointer)
{
    if (apply_mode(static_cast<Mode>(g_mode.load())))
    {
        VLOG("mode %d applied after launch\n", g_mode.load());
        g_retry_source = 0;
        return G_SOURCE_REMOVE;
    }
    if (g_get_monotonic_time() >= g_retry_deadline_us)
    {
        LOG("mode %d: windows did not appear within %d s\n", g_mode.load(), g_cfg.launch_timeout_s);
        g_retry_source = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

// GTK thread only. Sets the mode, updates the label and applies the layout; if some
// window is still starting, keeps retrying until it appears or the timeout expires.
static void request_mode(Mode mode)
{
    if (g_retry_source)
    {
        g_source_remove(g_retry_source);
        g_retry_source = 0;
    }
    g_mode = static_cast<int>(mode);
    if (g_button)
        gtk_button_set_label(GTK_BUTTON(g_button), g_cfg.mode_labels[static_cast<int>(mode)].c_str());

    if (!apply_mode(mode))
    {
        g_retry_deadline_us = g_get_monotonic_time() + gint64(g_cfg.launch_timeout_s) * G_USEC_PER_SEC;
        g_retry_source = g_timeout_add(500, retry_apply, nullptr);
    }
}

static gboolean request_mode_idle(gpointer data)
{
    request_mode(static_cast<Mode>(GPOINTER_TO_INT(data)));
    return G_SOURCE_REMOVE;
}

static void on_button_clicked(GtkWidget *, gpointer)
{
    const int count = g_cfg.enable_combi ? kModeCount : kModeCount - 1;
    const int cur = g_mode.load();
    request_mode(static_cast<Mode>(cur + 1 < count ? cur + 1 : 0));
}

// ── HTTP API (runs on the server thread) ─────────────────────────────────────

static rbhttp::Response status_response()
{
    WayfireIPC ipc;
    const auto views = ipc.list_views();
    const int mode = g_mode.load();
    std::ostringstream b;
    b << "{\"ok\":" << (ipc.last_error.empty() ? "true" : "false")
      << ",\"mode\":" << mode
      << ",\"label\":\"" << rbhttp::json_escape(g_cfg.mode_labels[mode]) << "\"";
    if (!ipc.last_error.empty())
        b << ",\"error\":\"" << rbhttp::json_escape(ipc.last_error) << "\"";
    b << ",\"apps\":[";
    for (int i = 0; i < 3; ++i)
    {
        const auto &a = g_cfg.app[i];
        b << (i ? "," : "") << "{\"letter\":\"" << a.letter << "\",\"app_id\":\""
          << rbhttp::json_escape(a.app_id) << "\",\"title\":\"" << rbhttp::json_escape(a.title) << "\"}";
    }
    b << "],\"views\":[";
    for (size_t i = 0; i < views.size(); ++i)
    {
        const auto &v = views[i];
        b << (i ? "," : "") << "{\"id\":" << v.id << ",\"app_id\":\"" << rbhttp::json_escape(v.app_id)
          << "\",\"title\":\"" << rbhttp::json_escape(v.title) << "\",\"role\":\""
          << rbhttp::json_escape(v.role) << "\",\"fullscreen\":" << (v.fullscreen ? "true" : "false") << "}";
    }
    b << "]}";
    return {ipc.last_error.empty() ? 200 : 503, "application/json", b.str()};
}

static std::optional<rbhttp::Response> handle_api(const rbhttp::Request &r)
{
    if (r.path == "/api/shutdown" || r.path == "/api/stop")
        return rbhttp::run_shell_detached(g_cfg.shutdown_cmd)
                   ? rbhttp::ok_action("shutdown")
                   : rbhttp::error(500, "failed to launch shutdown command");

    if (r.path == "/api/reboot")
        return rbhttp::run_shell_detached(g_cfg.reboot_cmd)
                   ? rbhttp::ok_action("reboot")
                   : rbhttp::error(500, "failed to launch reboot command");

    if (r.path.compare(0, 10, "/api/mode/") == 0)
    {
        int mode = rbhttp::parse_index_suffix(r.path, "/api/mode/");
        if (mode < 0 || mode >= kModeCount)
            return rbhttp::error(400, "invalid mode");
        g_idle_add(request_mode_idle, GINT_TO_POINTER(mode));
        return rbhttp::Response{200, "application/json",
                                "{\"ok\":true,\"action\":\"mode\",\"mode\":" + std::to_string(mode) + "}"};
    }

    if (r.path == "/api/status")
        return status_response();

    return std::nullopt;
}

// ── main ─────────────────────────────────────────────────────────────────────

static gboolean on_unix_signal(gpointer)
{
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);

    if (!parse_args(argc, argv, g_cfg))
    {
        print_usage(argv[0]);
        return 1;
    }

    gtk_init(&argc, &argv);
    if (!gtk_layer_is_supported())
    {
        LOG("compositor does not support wlr-layer-shell (not running under Wayfire/Wayland?)\n");
        return 1;
    }

    g_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWindow *win = GTK_WINDOW(g_window);
    gtk_window_set_title(win, "wayfire-layout-switcher");
    gtk_window_set_decorated(win, FALSE);
    gtk_window_set_resizable(win, FALSE);
    gtk_window_set_default_size(win, g_cfg.button_w, g_cfg.button_h);

    gtk_layer_init_for_window(win);
    gtk_layer_set_namespace(win, "rb-layout-switcher");
    gtk_layer_set_layer(win, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_anchor(win, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(win, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_margin(win, GTK_LAYER_SHELL_EDGE_TOP, g_cfg.button_y);
    gtk_layer_set_margin(win, GTK_LAYER_SHELL_EDGE_LEFT, g_cfg.button_x);

    g_button = gtk_button_new_with_label(g_cfg.mode_labels[g_mode.load()].c_str());
    gtk_widget_set_size_request(g_button, g_cfg.button_w, g_cfg.button_h);
    g_signal_connect(g_button, "clicked", G_CALLBACK(on_button_clicked), nullptr);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
                                    "button {"
                                    " background: #000000;"
                                    " color: #ffffff;"
                                    " font-weight: bold;"
                                    " font-size: 24px;"
                                    " border: 2px solid #ffffff;"
                                    "}",
                                    -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    gtk_container_add(GTK_CONTAINER(g_window), g_button);
    g_signal_connect(g_window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    if (!g_cfg.hidden)
        gtk_widget_show_all(g_window);

    g_unix_signal_add(SIGTERM, on_unix_signal, nullptr);
    g_unix_signal_add(SIGINT, on_unix_signal, nullptr);

    rbhttp::Server server;
    if (!server.start(g_cfg.http, handle_api))
        LOG("HTTP API disabled (bind failed); the button still works\n");

    gtk_main();

    server.stop();
    return 0;
}
