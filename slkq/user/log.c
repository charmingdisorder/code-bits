#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include "log.h"

static int logstarted = 0;
static int logstderr = 0;
static int logdebug = 0;
static char *stderr_ident = NULL;
static int show_ts = 0;

static char *get_timestamp ()
{
        char *t;

        time_t now = time (NULL);
        t = asctime (localtime (&now));
        t[strlen(t)-1] = '\0';

        return t;
}

void
logclose(void)
{
        if (!logstarted)
                return;

        logstarted = logstderr = logdebug = 0;
}

void
loginit(const char *ident, int to_stderr, int debug_flag)
{
        if (logstarted)
                logclose();

        logstarted = 1;
        logdebug = (debug_flag != 0);

        if (to_stderr) {
                logstderr = 1;
                show_ts = 1;
                stderr_ident = strdup(ident);
        } else {
                openlog(ident, LOG_PID, LOG_DAEMON);
        }
}

void
vlogit(int level, const char *fmt, va_list args)
{
        if (level == LOG_DEBUG && !logdebug)
                return;

        if (logstderr) {
                if (show_ts) {
                        fprintf(stderr, "%s ", get_timestamp());
                }
                fprintf(stderr, "%s: ", stderr_ident);
                vfprintf(stderr, fmt, args);
                fputs("\n", stderr);
        } else
                vsyslog(level, fmt, args);
}

void
logit(int level, const char *fmt, ...)
{
        va_list args;

        va_start(args, fmt);
        vlogit(level, fmt, args);
        va_end(args);
}

void
logitm(int level, const char *fmt, ...)
{
        va_list args;
        char buf[1024];

        va_start(args, fmt);
        snprintf(buf, sizeof(buf), "%s: %s", fmt, strerror(errno));
        vlogit(level, buf, args);
        va_end(args);
}

void
logerr(const char *fmt, ...)
{
        va_list args;
        char buf[1024];

        va_start(args, fmt);
        snprintf(buf, sizeof(buf), "%s: %s", fmt, strerror(errno));
        vlogit(LOG_ERR, buf, args);
        va_end(args);

        exit(1);
}

void
logerrx(const char *fmt, ...)
{
        va_list args;

        va_start(args, fmt);
        vlogit(LOG_ERR, fmt, args);
        va_end(args);

        exit(1);
}
