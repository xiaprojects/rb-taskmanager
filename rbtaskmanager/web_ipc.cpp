#include <microhttpd.h>
#include <thread>
#include <string>
#include <cstring>
#include <cstdlib>

static int remote_mode_param = 0;

static gboolean remote_set_mode_idle(gpointer data)
{
    int param = GPOINTER_TO_INT(data);
    if (param < 0 || param >= static_cast<int>(Mode::COUNT))
        return G_SOURCE_REMOVE;

    applymode(static_cast<Mode>(param));
    currentmode = static_cast<Mode>(param);
    if (buttonwidget)
        gtk_button_set_label(GTK_BUTTON(buttonwidget),
                             modelabels[static_cast<int>(currentmode)]);
    return G_SOURCE_REMOVE;
}

void remoteSetMode(int parameter)
{
    g_idle_add(remote_set_mode_idle, GINT_TO_POINTER(parameter));
}

static int send_json(struct MHD_Connection *connection, int status, const char *json)
{
    struct MHD_Response *response =
        MHD_create_response_from_buffer(std::strlen(json),
                                        (void *)json,
                                        MHD_RESPMEM_PERSISTENT);
    if (!response)
        return MHD_NO;

    MHD_add_response_header(response, "Content-Type", "application/json");
    int ret = MHD_queue_response(connection, status, response);
    MHD_destroy_response(response);
    return ret;
}

static int answer_to_connection(void *cls,
                                struct MHD_Connection *connection,
                                const char *url,
                                const char *method,
                                const char *version,
                                const char *upload_data,
                                size_t *upload_data_size,
                                void **con_cls)
{
    if (std::strcmp(method, "GET") != 0 && std::strcmp(method, "POST") != 0)
        return send_json(connection, 405, "{\"ok\":false,\"error\":\"method not allowed\"}");

    if (std::strcmp(url, "/api/shutdown") == 0 || std::strcmp(url, "/api/stop") == 0)
    {
        std::system("sudo halt");
        return send_json(connection, 200, "{\"ok\":true}");
    }

    if (std::strcmp(url, "/api/reboot") == 0)
    {
        std::system("sudo reboot");
        return send_json(connection, 200, "{\"ok\":true}");
    }

    const char *prefix = "/api/mode/";
    if (std::strncmp(url, prefix, std::strlen(prefix)) == 0)
    {
        const char *p = url + std::strlen(prefix);
        char *end = nullptr;
        long v = std::strtol(p, &end, 10);
        if (*p == '\0' || *end != '\0' || v < 0 || v >= static_cast<long>(Mode::COUNT))
            return send_json(connection, 400, "{\"ok\":false,\"error\":\"invalid mode\"}");

        remoteSetMode(static_cast<int>(v));
        return send_json(connection, 200, "{\"ok\":true}");
    }

    return send_json(connection, 404, "{\"ok\":false,\"error\":\"not found\"}");
}

static void start_rest_api()
{
    MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD,
        8080,
        nullptr, nullptr,
        &answer_to_connection, nullptr,
        MHD_OPTION_LISTENING_ADDRESS_REUSE, 1,
        MHD_OPTION_END);
}