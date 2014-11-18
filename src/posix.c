
#include <unpfs/posix.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

void
stat_posix_to_9p(IxpStat *stat, char *name, struct stat *buf)
{
    stat->type = 0;
    stat->dev = 0;
    stat->qid.type = buf->st_mode & S_IFMT;
    stat->qid.path = buf->st_ino;
    stat->qid.version = 0;
    stat->mode = buf->st_mode & 0777;
    if (S_ISDIR(buf->st_mode)) {
        stat->mode |= P9_DMDIR;
        stat->qid.type |= P9_QTDIR;
    }
    stat->atime = buf->st_atime;
    stat->mtime = buf->st_mtime;
    stat->length = buf->st_size;
    stat->name = name;
    stat->uid  = getenv("USER");
    stat->gid  = getenv("USER");
    stat->muid = getenv("USER");
}

mode_t
perm_9p_to_posix(uint32_t perm)
{
    mode_t mode = 0;

    if (perm & P9_DMDIR)
        mode = 0777777777;
    else
        mode = 0777777777;

    return mode;
}

int
open_mode_9p_to_posix(uint8_t mode_9p)
{
    unsigned int i = 0;
    int mode_posix = 0;
    static int mode_map[][2] = {
        {P9_OREAD, O_RDONLY}, {P9_OWRITE, O_WRONLY},
        {P9_ORDWR, O_RDWR}, {P9_OTRUNC, O_TRUNC},
        /*{P9_ODIRECT, O_DIRECT},*/ {P9_ONONBLOCK, O_NONBLOCK},
        {P9_OEXEC, O_EXCL}, {P9_OAPPEND, O_APPEND}
    };
    size_t map_size = (sizeof mode_map) / (sizeof (int) * 2);

    for (; i < map_size; ++i) {
        if (mode_9p & mode_map[i][0])
            mode_posix |= mode_map[i][1];
    }

    return mode_posix;
}
