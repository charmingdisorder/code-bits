#include "../common/slkq.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

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
                fd = 0;
        } else if ((optind+1 == argc) && !fname) {
                instr = argv[optind];
        } else {
                usage(argv[0]);
                exit(EXIT_FAILURE);
        }

        if (fname) {
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
                        fprintf(stdout, "ERROR write() returned %ld %s\n",
                                r, (r < 0) ? strerror(errno) : "");
                        break;
                }
        } while (1);

        if (fd > 0)
                close(fd);

        close(slkq_fd);

        exit(EXIT_SUCCESS);
}
