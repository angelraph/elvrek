/*
 * elv-ctl: crew commands and status.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Usage: elv-ctl start | stop | shock | epi | mode 30:2 | mode cont |
 *                transport on | transport off | status [key]
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "elv_ipc.h"

static int status(const char *key)
{
    char buf[1200];
    int fd = open(ELV_DEV_STATUS, O_RDONLY);
    if (fd == -1) { perror(ELV_DEV_STATUS); return 1; }
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) { fprintf(stderr, "elv-ctl: no status\n"); return 1; }
    buf[n] = '\0';
    if (!key) { fputs(buf, stdout); return 0; }
    /* print one value: elv-ctl status state */
    size_t kl = strlen(key);
    for (char *p = buf; (p = strstr(p, key)); p++) {
        if ((p == buf || p[-1] == ' ') && p[kl] == '=') {
            p += kl + 1;
            size_t l = strcspn(p, " \n");
            printf("%.*s\n", (int)l, p);
            return 0;
        }
    }
    fprintf(stderr, "elv-ctl: no key %s\n", key);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: elv-ctl start|stop|shock|epi|mode 30:2|mode cont|"
                        "transport on|transport off|status [key]\n");
        return 2;
    }
    if (!strcmp(argv[1], "status")) return status(argc > 2 ? argv[2] : NULL);

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "%s%s%s", argv[1], argc > 2 ? " " : "", argc > 2 ? argv[2] : "");
    int fd = open(ELV_DEV_OPERATOR, O_WRONLY);
    if (fd == -1) { perror(ELV_DEV_OPERATOR); return 1; }
    ssize_t w = write(fd, cmd, strlen(cmd));
    close(fd);
    if (w != (ssize_t)strlen(cmd)) { perror("write"); return 1; }
    return 0;
}
