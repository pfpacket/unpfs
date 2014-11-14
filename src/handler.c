
#include <unpfs/fid.h>
#include <unpfs/posix.h>
#include <unpfs/ops.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>

/*
 * File operations
 */

struct file_handle {
    int fd;
};

static int
file_open(struct unpfs_fid *fid, const char *path, int flags, mode_t mode)
{
    struct file_handle *fh = zalloc(sizeof *fh);

    fid->priv = fh;
    fh->fd = open(fid->real_path, flags, mode);

    return fh->fd >= 0 ? 0 : -1;
}

static ssize_t
file_read(struct unpfs_fid *fid, char **buf, size_t count, uint64_t offset)
{
    struct file_handle *fh = fid->priv;

    *buf = ixp_emallocz(count);
    if (!*buf) {
        errno = ENOMEM;
        return -1;
    }

    return read(fh->fd, *buf, count);
}

static ssize_t
file_write(struct unpfs_fid *fid, const void *buf, size_t count, uint64_t offset)
{
    struct file_handle *fh = fid->priv;

    return write(fh->fd, buf, count);
}

static int
file_close(struct unpfs_fid *fid)
{
    int fd;
    struct file_handle *fh = fid->priv;

    if (!fh)
        return 0;

    fd = fh->fd;

    zfree((char **)&fh);

    return close(fd);
}

static int
file_remove(struct unpfs_fid *fid)
{
    return unlink(fid->real_path);
}

const struct fid_handler file_handler = {
    file_open,
    file_read,
    file_write,
    file_close,
    file_remove
};


/*
 * Directory operations
 */
static int
dir_open(struct unpfs_fid *fid, const char *path, int flags, mode_t mode)
{
    if (flags & O_CREAT) {
        int ret = mkdir(path, mode);
        if (ret < 0)
            return ret;
    }

    fid->priv = opendir(fid->real_path);

    return fid->priv ? 0 : -1;
}

static ssize_t
dir_read(struct unpfs_fid *fid, char **buf, size_t count, uint64_t offset)
{
    ssize_t n = 0;
    DIR *dirp = fid->priv;
    char path[PATH_MAX];
    char *statbuf = ixp_emallocz(count);
    IxpMsg m = ixp_message(statbuf, count, MsgPack);
    struct dirent entry, *result;

    for (; readdir_r(dirp, &entry, &result) == 0 && result;) {
        IxpStat s;
        struct stat stbuf;

        /* 9P doesn't need ../ */
        if (!strcmp(entry.d_name, ".."))
            continue;

        snprintf(path, sizeof path, "%s%s%s",
            fid->real_path,
            (!strcmp(fid->real_path, "/") ? "" : "/"),
            entry.d_name);

        if (lstat(path, &stbuf) < 0)
            continue;

        stat_posix_to_9p(&s, entry.d_name, &stbuf);

        if (n + ixp_sizeof_stat(&s) > (ssize_t)count)
            break;
        n += ixp_sizeof_stat(&s);

        /* Pack the stat to the binary */
        ixp_pstat(&m, &s);
    }

    *buf = m.data;

    return n;
}

static ssize_t
dir_write(struct unpfs_fid *fid, const void *buf, size_t count, uint64_t offset)
{
    errno = ENOTSUP;
    return -1;
}

static int
dir_close(struct unpfs_fid *fid)
{
    DIR *dirp = fid->priv;

    return closedir(dirp);
}

static int
dir_remove(struct unpfs_fid *fid)
{
    return rmdir(fid->real_path);
}


const struct fid_handler dir_handler = {
    dir_open,
    dir_read,
    dir_write,
    dir_close,
    dir_remove
};
