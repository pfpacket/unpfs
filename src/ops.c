
#include <unpfs/ops.h>
#include <unpfs/fid.h>
#include <unpfs/posix.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h> /* For POSIX basename(3) */
#include <syslog.h>

struct ixp_context ctx;

enum {
    ERRNO_MSG_BUF_LENGTH = 1024
};

char *
get_real_path(const char *path)
{
    int n;
    int root_is_root = !strcmp(ctx.root, "/");
    int path_is_root = !strcmp(path, "/");
    size_t length =
        (root_is_root ? 0 : strlen(ctx.root)) +
        (path_is_root ? 0 : strlen(path)) + 1;
    char *real_path = zalloc(length);

    n = snprintf(real_path, length, "%s%s",
        (root_is_root ? "" : ctx.root), (path_is_root ? "" : path));
    if (n < 0)
        zfree(&real_path);

    return real_path;
}

static void
respond(Ixp9Req *r, int err)
{
    const char *msg = NULL;
    static char buf[ERRNO_MSG_BUF_LENGTH];

    if (err) {
        if (strerror_r(err, buf, sizeof buf) < 0)
            msg = "Input/output error";
        else
            msg = buf;
    }

    ixp_respond(r, msg);
}

/*
 *
 * 9P2000 operations
 *
 */

/*
 * session class
 */
void
unpfs_attach(Ixp9Req *r)
{
    int ret = 0;
    struct stat stbuf;

    if (lstat(ctx.root, &stbuf) < 0) {
        ret = errno;
    } else {
        struct unpfs_fid *fid;

        r->fid->qid.type = P9_QTDIR;
        r->fid->qid.version = 0;
        r->fid->qid.path = stbuf.st_ino;
        r->ofcall.rattach.qid = r->fid->qid;

        fid = unpfs_fid_new("/", r->fid->qid.type);
        r->fid->aux = fid;
        fprintf(stderr, "[*] %s: Accepted a new 9P client: fid->path=%s fid->real_path=%s\n",
            __func__, fid->path, fid->real_path);
    }

    respond(r, ret);
}

void
unpfs_flush(Ixp9Req *r)
{
    respond(r, 0);
}


/*
 * file class
 */
void
unpfs_walk(Ixp9Req *r)
{
    off_t offset;
    int ret = 0, i = 0;
    char *path = zalloc(PATH_MAX), *real_path = NULL;
    struct unpfs_fid *fid = r->fid->aux;

    snprintf(path, PATH_MAX, "%s", (!strcmp(fid->path, "/") ? "" : fid->path));

    for (offset = strlen(path); i < r->ifcall.twalk.nwname; ++i) {
        struct stat stbuf;
        int count =
            snprintf(path + offset, PATH_MAX - offset,
                "/%s", r->ifcall.twalk.wname[i]);

        if (count < 0) {
            ret = errno;
            goto out;
        }

        offset += count;
        real_path = get_real_path(path);

        if (lstat(real_path, &stbuf) < 0) {
            ret = errno;
            goto out;
        } else {
            r->ofcall.rwalk.wqid[i].type = stbuf.st_mode & S_IFMT;
            if (S_ISDIR(stbuf.st_mode))
                r->ofcall.rwalk.wqid[i].type |= P9_QTDIR;
            r->ofcall.rwalk.wqid[i].version = 0;
            r->ofcall.rwalk.wqid[i].path = stbuf.st_ino;
            zfree(&real_path);
        }
    }

    r->newfid->aux = r->ifcall.twalk.nwname ?
        unpfs_fid_new(path, r->ofcall.rwalk.wqid[i - 1].type) :
        unpfs_fid_clone(fid);

    fprintf(stderr, "%s: newfid: fid=%u fid->path=%s fid->real_path=%s\n",
        __func__,
        r->newfid->fid,
        ((struct unpfs_fid *)(r->newfid->aux))->path,
        ((struct unpfs_fid *)(r->newfid->aux))->real_path);

    r->ofcall.rwalk.nwqid = i;

out:
    zfree(&path);
    zfree(&real_path);
    respond(r, ret);
}

void
unpfs_create(Ixp9Req *r)
{
    int ret = 0;
    struct stat stbuf;
    char *new_real_path = zalloc(PATH_MAX);
    struct unpfs_fid *fid = r->fid->aux;
    mode_t mode = perm_9p_to_posix(r->ifcall.tcreate.perm);

    snprintf(new_real_path, PATH_MAX, "%s%s/%s",
        (!strcmp(ctx.root, "/") ? "" : ctx.root),
        (!strcmp(fid->path, "/") ? "" : fid->path),
        r->ifcall.tcreate.name);

    fprintf(stderr, "%s: fid->path=%s new_real_path=%s\n",
        __func__, fid->path, new_real_path);


    zfree(&fid->path);
    zfree(&fid->real_path);
    fid->path = strdup(new_real_path + strlen(ctx.root));
    fid->real_path = strdup(new_real_path);
    fid->type = (r->ifcall.tcreate.perm & P9_DMDIR ? P9_QTDIR : P9_QTFILE);
    fid->handler =
        (fid->type & P9_QTDIR ?
            &dir_handler :
            &file_handler);

    ret = fid->handler->open(fid, new_real_path, O_CREAT, mode);
    if (ret < 0) {
        ret = errno;
        goto out;
    }

    if (lstat(new_real_path, &stbuf) < 0) {
        ret = errno;
    } else {
        zfree(&fid->path);
        r->fid->qid.path = stbuf.st_ino;
        r->fid->qid.type = stbuf.st_mode & S_IFMT;
    }

out:
    zfree(&new_real_path);
    respond(r, ret);
}

void
unpfs_remove(Ixp9Req *r)
{
    int ret = 0;
    struct unpfs_fid *fid = r->fid->aux;

    ret = fid->handler->remove(fid);
    if (ret < 0)
        ret = errno;

    respond(r, ret);
}


void
unpfs_open(Ixp9Req *r)
{
    int ret = 0;
    int flags = 0;
    struct unpfs_fid *fid = r->fid->aux;

    flags = open_mode_9p_to_posix(r->ifcall.topen.mode);

    ret = fid->handler->open(fid, NULL, flags, 0);
    if (ret < 0)
        ret = errno;

    respond(r, ret);
}

void
unpfs_read(Ixp9Req *r)
{
    int ret = 0;
    ssize_t count;
    struct unpfs_fid *fid = r->fid->aux;

    count = fid->handler->read(
        fid,
        &r->ofcall.rread.data,
        r->ifcall.tread.count,
        r->ifcall.tread.offset
    );

    if (count < 0)
        ret = errno;
    else
        r->ofcall.rread.count = count;

    respond(r, ret);
}

void
unpfs_write(Ixp9Req *r)
{
    int ret = 0;
    ssize_t count;
    struct unpfs_fid *fid = r->fid->aux;

    count = fid->handler->write(
        fid,
        r->ofcall.rwrite.data,
        r->ifcall.twrite.count,
        r->ifcall.twrite.offset
    );

    if (count < 0)
        ret = errno;
    else
        r->ofcall.rwrite.count = count;

    respond(r, ret);
}

void
unpfs_clunk(Ixp9Req *r)
{
    struct unpfs_fid *fid = r->fid->aux;

    if (fid && fid->handler)
        fid->handler->close(fid);

    respond(r, 0);
}


/*
 * metadata class
 */
void
unpfs_stat(Ixp9Req *r)
{
    int ret = 0;
    struct stat stbuf;
    struct unpfs_fid *fid = r->fid->aux;
    int size;
    IxpMsg m;
    struct IxpStat s;
    char *buf, *name = strdup(fid->path);

    if (!name || lstat(fid->real_path, &stbuf) < 0) {
        ret = errno;
        goto out;
    }

    stat_posix_to_9p(&s, basename(name), &stbuf);

    /* Pack the stat to the binary */
    size = ixp_sizeof_stat(&s);
    buf = ixp_emallocz(size);
    m = ixp_message(buf, size, MsgPack);
    ixp_pstat(&m, &s);
    zfree(&name);

    r->fid->qid = s.qid;
    r->ofcall.rstat.nstat = size;
    r->ofcall.rstat.stat = (uint8_t *)m.data;

out:
    respond(r, ret);
}

void
unpfs_wstat(Ixp9Req *r)
{
    respond(r, 0);
}

void
unpfs_freefid(IxpFid *f)
{
    struct unpfs_fid *fid = f->aux;
    if (fid) {
        fprintf(stderr, "%s: fid=%u fid->path=%s\n",
            __func__, f->fid, fid->path);
        unpfs_fid_destroy(fid);
    }
}
