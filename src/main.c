#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include <microhttpd.h>

#include "api.h"

#define DEFAULT_PORT 8080

static volatile int g_running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

int main(int argc, char *argv[])
{
    int port = DEFAULT_PORT;
    if (argc > 1)
        port = atoi(argv[1]);

    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    struct MHD_Daemon *daemon =
        MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD,
                         (uint16_t)port,
                         NULL, NULL,
                         &handle_request, NULL,
                         MHD_OPTION_NOTIFY_COMPLETED,
                             &cleanup_request, NULL,
                         MHD_OPTION_END);

    if (!daemon) {
        fprintf(stderr, "Failed to start HTTP server on port %d\n", port);
        return 1;
    }

    printf("Cubiomes seed-search API listening on port %d\n", port);
    printf("POST http://localhost:%d/search\n", port);
    printf("Press Ctrl-C to stop.\n");

    while (g_running)
        sleep(1);

    MHD_stop_daemon(daemon);
    printf("\nServer stopped.\n");
    return 0;
}
