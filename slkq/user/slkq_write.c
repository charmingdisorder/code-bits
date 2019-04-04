/*
 * slkq_write: simple user-space application that handles writing to
 *             /dev/slkq (SLKQ) device
 *
 * Copyright (C) 2019 Alexey Mikhailov
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "../common/slkq.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

/**
 * slkq_write simply sends message (buffer) to /dev/slkq character
 * device. Message can be defined in several ways:
 *
 *  - use file as input ('-f' option, e.g. ./slkq_write -f /tmp/msg1)
 *  - just specify string as 'msg' argument (e.g. ./slkq_write "test")
 *  - if none of above is speicified, message gets read from stdin
 *
 * '-a' option toggles nonblocking behavior for write operation.
 * If specified, O_NONBLOCK is going to be set on socket and application
 * will try to write to socket till it will receive anything but
 * EAGAIN.
 */

static unsigned int is_async = 0;
static int slkq_fd;
static char buf[SLKQ_MSG_MAX_SIZE];
static const char *fname = NULL;
static char *instr = NULL;

static void usage (const char *bin) {
        fprintf(stderr, "Usage: %s [-a] [-f filename] [msg]\n", bin);
}

int main (int argc, char **argv)
{
        int opt;
        int fd = -1;
        ssize_t r;
        unsigned int t;
        char *p;

        while ((opt = getopt(argc, argv, "af:")) != -1) {
                switch (opt) {
                case 'a':
                        is_async = 1;
                        break;
                case 'f':
                        fname = optarg;
                        break;
                default:
                        usage(argv[0]);
                        exit(EXIT_FAILURE);
                }
        }

        slkq_fd = open(SLKQ_DEV, O_WRONLY);

        if (slkq_fd < 0) {
                fprintf(stderr, "%s: failed to open %s: %s\n",
                                argv[0], SLKQ_DEV, strerror(errno));
                exit(EXIT_FAILURE);
        }

        if (is_async && (fcntl(slkq_fd, F_SETFL, O_NONBLOCK) < 0)) {
                fprintf(stderr, "%s: fcntl() failed: %s\n",
                        argv[0], strerror(errno));
                exit(EXIT_FAILURE);
        }

        if (optind >= argc) {
                /* Using stdin if no message or file specified*/
                fd = 0;
        } else if ((optind+1 == argc) && !fname) {
                /* Message specified as string */
                instr = argv[optind];
        } else {
                usage(argv[0]);
                exit(EXIT_FAILURE);
        }

        if (fname) {
                /* Reading from file and specifying message as argument are
                   mutually exclusive */
                if (instr) {
                        usage(argv[0]);
                        exit(EXIT_FAILURE);
                }

                fd = open(fname, O_RDONLY);
                if (fd < 0) {
                        fprintf(stderr, "%s: failed to open %s: %m\n",
                                argv[0], fname);
                        exit(EXIT_FAILURE);
                }
        }

        if (fd >= 0 && !instr) {
                /* Reading from file (or stdin) */
                p = buf;
                t = 0;

                do {
                        if (t >= SLKQ_MSG_MAX_SIZE - 1) {
                                fprintf(stderr, "ERROR input overflow\n");
                                exit(EXIT_FAILURE);
                        }

                        r = read(fd, p+t, SLKQ_MSG_MAX_SIZE - t);

                        if (r == 0) {
                                break;
                        } else if (r < 0) {
                                fprintf(stderr, "%s: read() failed: %s\n",
                                        argv[0], strerror(errno));
                                exit(EXIT_FAILURE);
                        }

                        t += r;

                } while (1);
        } else if (instr && fd < 0) {
                /* Just using input string as message */
                p = instr;
                t = strlen(p)+1;
        } else {
                usage(argv[0]);
                exit(EXIT_FAILURE);
        }

        do {
                r = write(slkq_fd, p, t);

                if (r == t) {
                        fprintf(stdout, "OK\n");
                        break;
                } else if (errno == EAGAIN && r == -1) {
                        fprintf(stdout, "EAGAIN\n");
                        sleep(2);
                        continue;
                } else {
                        fprintf(stdout, "ERROR %ld %s\n",
                                r, (r < 0) ? strerror(errno) : "");
                        break;
                }
        } while (1);

        if (fd > 0)
                close(fd);

        close(slkq_fd);

        exit(EXIT_SUCCESS);
}
