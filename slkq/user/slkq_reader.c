#include "../common/slkq.h"
#include "atomic_io.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include <linux/limits.h>
#include <sys/stat.h>

#define T_WIN (1 * 60)

static unsigned int is_daemon = 1;
static unsigned int stopping = 0;
static int slkq_fd;
static int out_fd;

static void usage (const char *bin) {
        fprintf(stderr, "Usage: %s [-f]\n", bin);
        exit(EXIT_FAILURE);
}

static void daemonize ()
{
        int i;
        pid_t pid;

        if ((pid = fork()) > 0)
                exit(EXIT_SUCCESS);

        if (setsid() < 0) {
                logit(LOG_ERR, "setsid() failed");
                exit(EXIT_FAILURE);
        }

        signal(SIGHUP, SIG_IGN);
        signal(SIGCHLD, SIG_IGN);

        if ((pid = fork()) > 0)
                exit(EXIT_SUCCESS);

        chdir("/");
        umask(0);

        for (i = 0; i < 64; i++)
                close(i);
}

static void _mkdir(const char *dir)
{
        char tmp[256];
        char *p = NULL;
        size_t len;

        snprintf(tmp, sizeof(tmp),"%s",dir);
        len = strlen(tmp);

        if(tmp[len - 1] == '/')
                tmp[len - 1] = 0;

        for(p = tmp + 1; *p; p++) {
                if(*p == '/') {
                        *p = 0;
                        mkdir(tmp, S_IRWXU);
                        *p = '/';
                }
        }

        mkdir(tmp, S_IRWXU);
}

static int open_new_export_file (const char *dir, struct tm *t)
{
        int fd;
        char fname[PATH_MAX];

        _mkdir(dir);
        snprintf(fname, PATH_MAX, "%s/%d%02d%02d_%02d%02d.bin", dir,
                 (t->tm_year+1900), (t->tm_mon+1), t->tm_mday,
                 (t->tm_hour), (t->tm_min));

        if ((fd = open(fname, O_WRONLY | O_CREAT | O_APPEND, 0666)) < 0) {
                syslog(LOG_ERR, "can't open export file %s: %m", fname);
        }

        return fd;
}

static int lock_file (int fd)
{
        struct flock fl;

        fl.l_type = F_WRLCK;
        fl.l_start = 0;
        fl.l_whence = SEEK_SET;
        fl.l_len = 0;

        return (fcntl(fd, F_SETLK, &fl));
}

static int already_running (void)
{
        int fd;
        char buf[16];

        fd = open(SLKQ_READER_LOCK, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
        if (fd < 0) {
                fprintf(stderr, "can't open %s: %m", SLKQ_READER_LOCK);
                return 1;
        }

        if (lock_file(fd) < 0) {
                fprintf(stderr, "can't lock %s: %m", SLKQ_READER_LOCK);
                close(fd);
                return 1;
        }

        ftruncate(fd, 0);
        sprintf(buf, "%ld", (long)getpid());
        write(fd, buf, strlen(buf)+1);
        return(0);
}

static time_t t_start = 0, t_now = 0;

static int handle_input (int fd)
{
        char buf[SLKQ_MSG_MAX_SIZE];
        ssize_t r;

        if ((r = read(fd, buf, sizeof(buf))) <= 0) {
                logit(LOG_ERR, "%s: read(): %m", __func__);
                return r;
        }

        /* XXX: handle signals */

        t_now = time(NULL);

        if (!t_start) {
                t_start = t_now - (t_now % T_WIN);
                out_fd = open_new_export_file(SLKQ_READER_OUTPUT_DIR,
                                              localtime(&t_start));
                if (out_fd < 0) {
                        logit(LOG_ERR, "%s: can't open new export file: %m",
                                __func__);
                        return -1;
                }
        }

        if ((t_now - t_start) >= T_WIN) {
                close(out_fd);
                t_start = t_now - (t_now % T_WIN);

                out_fd = open_new_export_file(SLKQ_READER_OUTPUT_DIR,
                                              localtime(&t_start));
                if (out_fd < 0) {
                        logit(LOG_ERR, "%s: can't open new export file: %m",
                                __func__);
                        return -1;
                }
        }

        if (atomicio(vwrite, out_fd, (u_int16_t *)&r, 2) != 2) {
                logit(LOG_ERR, "%s: write: %m", __func__);
                return -1;
        }

        if (atomicio(vwrite, out_fd, buf, r) != r) {
                logit(LOG_ERR, "%s: write: %m", __func__);
                return -1;
        }

        return r;
}

int main (int argc, char **argv)
{
        int opt, rc = -1;

        while ((opt = getopt(argc, argv, "f")) != -1) {
                switch (opt) {
                case 'f':
                        is_daemon = 0;
                        break;
                default:
                        usage(argv[0]);
                }
        }

        if (already_running()) {
                fprintf(stderr, "%s: already running\n", argv[0]);
                exit(EXIT_FAILURE);
        }

        if (is_daemon) {
                daemonize();
        }

        loginit("slkq_reader", (1-is_daemon), 0);

        slkq_fd = open(SLKQ_DEV, O_RDONLY);

        if (slkq_fd < 0) {
                logit(LOG_ERR, "%s: failed to open %s: %s\n",
                      argv[0], SLKQ_DEV, strerror(errno));
                exit(EXIT_FAILURE);
        }

        do {
                if (stopping) {
                        break;
                }

                rc = handle_input(slkq_fd);
        } while (rc >= 0);

        close(slkq_fd);
        logclose();

        exit(EXIT_SUCCESS);
}
