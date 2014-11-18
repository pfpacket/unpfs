#ifndef UNPFS_LOG_H
#define UNPFS_LOG_H

#include <unpfs/common.h>
#include <syslog.h>

extern int unpfs_log_level(int level);

#ifdef NDEBUG
#   define unpfs_syslog unpfs_log_dummy
#   define unpfs_log unpfs_log_dummy
    extern void unpfs_log_dummy(int pri, const char *fmt, ...);
#else
#   define unpfs_syslog syslog
    extern void unpfs_log(int pri, const char *fmt, ...);
#endif  /* NDEBUG */


#endif  /* UNPFS_LOG_H */
