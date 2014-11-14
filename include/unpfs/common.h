#ifndef UNPFS_COMMON_H
#define UNPFS_COMMON_H

#ifndef _FILE_OFFSET_BITS
#   define _FILE_OFFSET_BITS 64
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

extern void *zalloc(size_t size);
extern void zfree(char **m);

#endif  /* UNPFS_COMMON_H */
