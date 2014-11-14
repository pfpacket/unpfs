
#include <unpfs/fid.h>
#include <unpfs/ops.h>
#include <ixp.h>

struct unpfs_fid *
unpfs_fid_new(const char *path, uint8_t type)
{
    struct unpfs_fid *fid = zalloc(sizeof *fid);

    fid->path = strdup(path);
    fid->real_path = get_real_path(path);
    fid->type = type;
    fid->priv = NULL;

    fid->handler =
        (type & P9_QTDIR ?
            &dir_handler :
            &file_handler);

    return fid;
}

struct unpfs_fid *
unpfs_fid_clone(struct unpfs_fid *fid)
{
    struct unpfs_fid *newfid = zalloc(sizeof *fid);

    newfid->path = strdup(fid->path);
    newfid->real_path = strdup(fid->real_path);
    newfid->type = fid->type;
    newfid->handler = fid->handler;
    newfid->priv = fid->priv;

    return newfid;
}

void
unpfs_fid_destroy(struct unpfs_fid *fid)
{
    zfree(&fid->path);
    zfree(&fid->real_path);
    zfree((char **)&fid);
}
