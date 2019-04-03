#ifndef _LOG_H_
#define _LOG_H_

#include <syslog.h>

#if defined(__GNUC__)
# ifndef __dead
#  define __dead                __attribute__((__noreturn__))
# endif
# ifndef __packed
#  define __packed              __attribute__((__packed__))
# endif
#endif

void logclose(void);
void loginit(const char *ident, int to_stderr, int debug_flag);
void logit(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void logitm(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void logerr(const char *fmt, ...) __dead __attribute__((format(printf, 1, 2)));
void logerrx(const char *fmt, ...) __dead __attribute__((format(printf, 1, 2)));

#endif
