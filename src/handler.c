
#include <unpfs/fid.h>
#include <unpfs/posix.h>
#include <unpfs/log.h>
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
    FILE *fp;
};

static void
mode_fopen_from_sys_open(char *buf, int len, int flags, mode_t mode)
{
    if (len <= 1)
        return;
    buf[0] = '\0';

    if (flags == O_RDONLY)
        strncat(buf, "r", len - strlen(buf) - 1);
    if (flags & O_WRONLY)
        strncat(buf, "w", len - strlen(buf) - 1);
    if (flags & O_RDWR)
        strncat(buf, flags & O_CREAT ? "w+" : "r+", len - strlen(buf) - 1);
    if (flags & O_APPEND)
        strncat(buf, flags & O_WRONLY ? "a" : "a+", len - strlen(buf) - 1);
    strncat(buf, "b", len - strlen(buf) - 1);
}

static int
file_open(struct unpfs_fid *fid, const char *path, int flags, mode_t mode)
{
    int fd = -1;
    char fopen_mode[255];
    struct file_handle *fh = zalloc(sizeof *fh);

    fid->priv = fh;

    mode_fopen_from_sys_open(fopen_mode, sizeof fopen_mode, flags, mode);

    fd = open(fid->real_path, flags, mode);
    if (fd < 0)
        return -1;

    fh->fp = fdopen(fd, fopen_mode);

    return fh->fp ? 0 : -1;
}

static ssize_t
file_read(struct unpfs_fid *fid, char **buf, size_t count, uint64_t offset)
{
    size_t n = - 1;
    struct file_handle *fh = fid->priv;

    *buf = ixp_emallocz(count);
    if (!*buf) {
        errno = ENOMEM;
        return -1;
    }

    if (fseek(fh->fp, offset, SEEK_SET) < 0)
        return -1;

    n = fread(*buf, 1, count, fh->fp);

    return (n != 0 ? (int)n : (feof(fh->fp) ? 0 : (ferror(fh->fp) ? -1 : 0)));
}

static ssize_t
file_write(struct unpfs_fid *fid, const void *buf, size_t count, uint64_t offset)
{
    struct file_handle *fh = fid->priv;

    if (fseek(fh->fp, offset, SEEK_SET) < 0)
        return -1;

    return fwrite(buf, 1, count, fh->fp);
}

static int
file_close(struct unpfs_fid *fid)
{
    FILE *fp;
    struct file_handle *fh = fid->priv;

    if (!fh)
        return 0;

    fp = fh->fp;

    zfree((char **)&fh);

    return fp ? fclose(fp) : 0;
}

static int
file_remove(struct unpfs_fid *fid)
{
    return remove(fid->real_path);
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
