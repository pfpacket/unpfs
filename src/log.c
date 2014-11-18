
#include <unpfs/common.h>
#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <syslog.h>

static volatile sig_atomic_t unpfs_log_pri = 0;

int
unpfs_log_level(int level)
{
    int old_level = unpfs_log_pri;

    unpfs_log_pri = level;

    return old_level;
}

void
unpfs_log(int pri, const char *fmt, ...)
{
    if (unpfs_log_pri <= pri) {
        va_list ap;

        va_start(ap, fmt);

        fprintf(stderr, "[%d] ", pri);
        vfprintf(stderr, fmt, ap);

        va_end(ap);
    }
}

void
unpfs_log_dummy(int pri, const char *fmt, ...)
{
}
