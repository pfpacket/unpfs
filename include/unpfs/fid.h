#ifndef UNPFS_FID_H
#define UNPFS_FID_H

#include <unpfs/common.h>

struct unpfs_fid;

struct fid_handler {
    int (*open)(struct unpfs_fid *, const char *, int, mode_t);
    ssize_t (*read)(struct unpfs_fid *, char **buf, size_t, uint64_t);
    ssize_t (*write)(struct unpfs_fid *, const void *buf, size_t, uint64_t);
    int (*close)(struct unpfs_fid *);
    int (*remove)(struct unpfs_fid *);
};

extern const struct fid_handler file_handler;
extern const struct fid_handler dir_handler;

struct unpfs_fid {
    char *path;
    char *real_path;
    uint8_t type;
    const struct fid_handler *handler;
    void *priv;
};

extern struct unpfs_fid *unpfs_fid_new(const char *path, uint8_t type);
extern struct unpfs_fid *unpfs_fid_clone(struct unpfs_fid *fid);
extern void unpfs_fid_destroy(struct unpfs_fid *fid);

#endif  /* UNPFS_FID_H */
