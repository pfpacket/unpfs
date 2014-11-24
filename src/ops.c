
#include <unpfs/ops.h>
#include <unpfs/fid.h>
#include <unpfs/log.h>
#include <unpfs/posix.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/time.h>
#include <fcntl.h>
#include <libgen.h> /* For POSIX basename(3) */

struct ixp_context ctx;
static uint32_t g_msize = 0;
#define unpfs_min(a, b) ((a) < (b) ? (a) : (b))

enum {
    /*
     * ample room for Twrite/Rread header
     * size[4] Tread/Twrite tag[2] fid[4] offset[8] count[4]
     */
    P9_IOHDRSZ = 24,

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
        1 +(path_is_root ? 0 : strlen(path) + 1);
    char *real_path = zalloc(length);

    n = snprintf(real_path, length, "%s%s%s",
        (root_is_root ? "" : ctx.root),
        (*path == '/' ? "" : "/"),
        (path_is_root ? "" : path));
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

        unpfs_log(LOG_ERR, "respond: type=%u tag=%u fid=%u: %s",
            r->ifcall.hdr.type,
            r->ifcall.hdr.tag,
            r->ifcall.hdr.fid, msg);
    }

    ixp_respond(r, msg);
}

static uint32_t
compute_iounit(Ixp9Req *r, const char *name)
{
    uint32_t iounit = g_msize - P9_IOHDRSZ;

    /*
    struct statfs stbuf;

    if (!statfs(name, &stbuf)) {
        iounit = stbuf.f_bsize;
        iounit *= (g_msize - P9_IOHDRSZ) / stbuf.f_bsize;
    }
    */

    return iounit;
}

/*
 *
 * 9P2000 operations
 *
 */

/*
 * Session Management
 */
void
unpfs_version(Ixp9Req *r)
{
    g_msize = unpfs_min(r->ifcall.version.msize, IXP_MAX_MSG);
    r->ifcall.version.msize = g_msize;

    unpfs_log(LOG_INFO, "%s: msize=%u", __func__, g_msize);

    respond(r, 0);
}

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

        unpfs_log(LOG_NOTICE, "%s: New 9P client: uname=%s aname=%s",
            __func__, r->ifcall.tattach.uname, r->ifcall.tattach.aname);
    }

    respond(r, ret);
}

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

    if (r->fid->fid == r->newfid->fid) {
        unpfs_log(LOG_INFO, "%s: fid and newfid equals: fid=%u newfid=%u",
            __func__, r->fid->fid, r->newfid->fid);
    }

    r->newfid->aux = r->ifcall.twalk.nwname ?
        unpfs_fid_new(path, r->ofcall.rwalk.wqid[i - 1].type) :
        unpfs_fid_clone(fid);

    unpfs_log(LOG_INFO, "%s: newfid: fid=%u fid->path=%s fid->real_path=%s",
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
unpfs_flush(Ixp9Req *r)
{
    struct unpfs_fid *fid = r->fid->aux;

    unpfs_log(LOG_INFO, "%s: fid=%u real_path=%s",
        __func__, r->fid->fid, fid->real_path);

    respond(r, 0);
}


/*
 * File I/O
 */
void
unpfs_open(Ixp9Req *r)
{
    int ret = 0;
    int flags = 0;
    struct unpfs_fid *fid = r->fid->aux;

    unpfs_log(LOG_NOTICE, "%s: fid=%u real_path=%s",
        __func__, r->fid->fid, fid->real_path);

    flags = open_mode_9p_to_posix(r->ifcall.topen.mode);

    ret = fid->handler->open(fid, NULL, flags, 0);
    if (ret < 0)
        ret = errno;
    else {
        r->ofcall.ropen.iounit = compute_iounit(r, fid->real_path);
        unpfs_log(LOG_NOTICE, "%s: iounit=%u",
            __func__, r->ofcall.ropen.iounit);
    }

    respond(r, ret);
}

void
unpfs_create(Ixp9Req *r)
{
    int ret = 0;
    struct stat stbuf;
    struct unpfs_fid *fid = r->fid->aux;
    char *new_real_path = zalloc(PATH_MAX);
    mode_t mode = perm_9p_to_posix(r->ifcall.tcreate.perm);
    int flags = open_mode_9p_to_posix(r->ifcall.topen.mode) | O_CREAT;

    unpfs_log(LOG_NOTICE, "%s: fid=%u real_path=%s",
        __func__, r->fid->fid, fid->real_path);

    snprintf(new_real_path, PATH_MAX, "%s/%s",
        (!strcmp(fid->real_path, "/") ? "" : fid->real_path),
        r->ifcall.tcreate.name);

    zfree(&fid->path);
    zfree(&fid->real_path);
    fid->path = strdup(new_real_path + strlen(ctx.root));
    fid->real_path = strdup(new_real_path);
    fid->type = (r->ifcall.tcreate.perm & P9_DMDIR ? P9_QTDIR : P9_QTFILE);
    fid->handler =
        (fid->type & P9_QTDIR ?
            &dir_handler :
            &file_handler);

    ret = fid->handler->open(fid, new_real_path, flags, mode);
    if (ret < 0) {
        ret = errno;
        goto out;
    }

    if (lstat(new_real_path, &stbuf) < 0) {
        ret = errno;
    } else {
        r->fid->qid.type = fid->type;
        r->fid->qid.version = 0;
        r->fid->qid.path = stbuf.st_ino;
        r->ofcall.rcreate.iounit = compute_iounit(r, fid->real_path);
        unpfs_log(LOG_NOTICE, "%s: iounit=%u",
            __func__, r->ofcall.rcreate.iounit);
    }


out:
    zfree(&new_real_path);
    respond(r, ret);
}

void
unpfs_read(Ixp9Req *r)
{
    int ret = 0;
    ssize_t count;
    struct unpfs_fid *fid = r->fid->aux;

    unpfs_log(LOG_INFO, "%s: fid=%u real_path=%s count=%d offset=%lu",
        __func__, r->fid->fid,
        fid->real_path,
        r->ifcall.twrite.count,
        r->ifcall.twrite.offset);

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

    unpfs_log(LOG_INFO, "%s: fid=%u real_path=%s count=%d offset=%lu",
        __func__, r->fid->fid,
        fid->real_path,
        r->ifcall.twrite.count,
        r->ifcall.twrite.offset);

    count = fid->handler->write(
        fid,
        r->ifcall.twrite.data,
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
unpfs_remove(Ixp9Req *r)
{
    int ret = 0;
    struct unpfs_fid *fid = r->fid->aux;

    unpfs_log(LOG_NOTICE, "%s: fid=%u real_path=%s",
        __func__, r->fid->fid, fid->real_path);

    ret = fid->handler->remove(fid);
    if (ret < 0)
        ret = errno;

    respond(r, ret);
}

void
unpfs_clunk(Ixp9Req *r)
{
    struct unpfs_fid *fid = r->fid->aux;

    if (fid && fid->handler) {
        unpfs_log(LOG_NOTICE, "%s: fid=%u real_path=%s",
            __func__, r->fid->fid, fid->real_path);

        fid->handler->close(fid);
    }

    respond(r, 0);
}


/*
 * Metadata Management
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

    unpfs_log(LOG_NOTICE, "%s: fid=%u real_path=%s",
        __func__, r->fid->fid, fid->real_path);

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

static int
unpfs_rename(struct unpfs_fid *fid, IxpStat *stat)
{
    int ret = 0, count;
    char new_real_path[PATH_MAX];
    char *real_parent = NULL;

    /* No need to name */
    if (!strlen(stat->name))
        goto out;

    if (!(real_parent = strdup(fid->real_path))) {
        ret = -1;
        goto out;
    }

    count = snprintf(new_real_path, sizeof new_real_path,
        "%s/%s", dirname(real_parent), stat->name);
    if (count < 0) {
        ret = -1;
        goto out;
    }

    unpfs_log(LOG_INFO, "%s: renaming %s to %s",
        __func__, fid->real_path, new_real_path);

    /* No need to rename */
    if (!strcmp(fid->real_path, new_real_path))
        goto out;

    if (rename(fid->real_path, new_real_path) < 0) {
        ret = -1;
        goto out;
    }

    fid->real_path = strdup(new_real_path);

out:
    zfree(&real_parent);
    return ret;
}

static int 
unpfs_utimes(struct unpfs_fid *fid, IxpStat *stat)
{
    struct timeval times[2];

    if (stat->atime == UINT32_MAX || stat->mtime == UINT32_MAX)
        return 0;

    memset(times, 0, sizeof times);

    times[0].tv_sec = stat->atime;
    times[1].tv_sec = stat->mtime;

    return utimes(fid->real_path, times);
}

static int
unpfs_truncate(struct unpfs_fid *fid, IxpStat *stat)
{
    return stat->length == UINT64_MAX ?
        0 : truncate(fid->real_path, stat->length);
}

static int
stat_is_sync_request(const struct IxpStat *stat)
{
    int i = 0;
    int len = sizeof (*stat) - (sizeof (char *) * 4);

    for (; i < len; ++i) {
        if (((uint8_t *)stat)[i] != UINT8_MAX)
            return 0;
    }

    return 1;
}

void
unpfs_wstat(Ixp9Req *r)
{
    int ret = 0;
    struct stat stbuf;
    struct unpfs_fid *fid = r->fid->aux;
    IxpStat *stat = &r->ifcall.twstat.stat;

    unpfs_log(LOG_NOTICE, "%s: fid=%u real_path=%s stat->name=%s",
        __func__, r->fid->fid, fid->real_path, stat->name);

    if (stat_is_sync_request(stat)) {
        unpfs_log(LOG_INFO, "%s: syncing...", __func__);
        sync();
        goto out;
    }

    if (unpfs_rename(fid, stat) < 0) {
        ret = errno;
        goto out;
    }

    if (lstat(fid->real_path, &stbuf) < 0) {
        ret = errno;
        goto out;
    }

    if (unpfs_utimes(fid, stat) < 0) {
        ret = errno;
        goto out;
    }

    if (unpfs_truncate(fid, stat) < 0) {
        ret = errno;
        goto out;
    }

out:
    respond(r, ret);
}

void
unpfs_freefid(IxpFid *f)
{
    struct unpfs_fid *fid = f->aux;

    if (fid) {
        unpfs_log(LOG_INFO, "%s: fid=%u fid->path=%s%s",
            __func__, f->fid, fid->path,
            !f->fid ? ": client detached" : "");
        unpfs_fid_destroy(fid);
    }
}
