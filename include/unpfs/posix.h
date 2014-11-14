#ifndef UNPFS_POSIX_H
#define UNPFS_POSIX_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ixp.h>

extern void stat_posix_to_9p(IxpStat *stat, char *name, struct stat *buf);
extern mode_t perm_9p_to_posix(uint32_t perm);
extern int open_mode_9p_to_posix(uint8_t mode_9p);

#endif  /* UNPFS_POSIX_H */
