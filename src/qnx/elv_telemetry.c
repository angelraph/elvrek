/*
 * elv-telemetry: streams /dev/elvrek/status to the crew console.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unprivileged, low priority. Reads the status device at 20 Hz and sends
 * each line to every connected TCP client (default port 7700). It has no
 * write path into the safety process.
 *
 * Usage: elv-telemetry [-p port]
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include "elv_ipc.h"
#include "elv_rt.h"

#define MAXC 8

int main(int argc, char **argv)
{
    int port = 7700, opt;
    while ((opt = getopt(argc, argv, "p:")) != -1)
        if (opt == 'p') port = atoi(optarg);
    signal(SIGPIPE, SIG_IGN);
    elv_rt_set_prio(ELV_PRIO_TELEM);

    int ls = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port),
                             .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (bind(ls, (struct sockaddr *)&a, sizeof(a)) == -1 || listen(ls, 4) == -1) {
        perror("elv-telemetry: listen");
        return 1;
    }
    int cl[MAXC];
    for (int i = 0; i < MAXC; i++) cl[i] = -1;
    fprintf(stderr, "elv-telemetry: port %d\n", port);

    for (;;) {
        fd_set rf; FD_ZERO(&rf); FD_SET(ls, &rf);
        struct timeval tv = { 0, 50000 };
        if (select(ls + 1, &rf, NULL, NULL, &tv) > 0) {
            int c = accept(ls, NULL, NULL);
            int placed = 0;
            for (int i = 0; i < MAXC && c != -1 && !placed; i++)
                if (cl[i] == -1) { cl[i] = c; placed = 1; }
            if (!placed && c != -1) close(c);
        }
        char buf[1200];
        int fd = open(ELV_DEV_STATUS, O_RDONLY);
        if (fd == -1) continue;
        ssize_t n = read(fd, buf, sizeof(buf));
        close(fd);
        if (n <= 0) continue;
        for (int i = 0; i < MAXC; i++)
            if (cl[i] != -1 && send(cl[i], buf, (size_t)n, MSG_NOSIGNAL) != n) {
                close(cl[i]);
                cl[i] = -1;
            }
    }
}
