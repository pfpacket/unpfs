#ifndef UNPFS_OPS_H
#define UNPFS_OPS_H

#include <unpfs/common.h>
#include <ixp.h>

struct ixp_context {
    int fd;
    const char *root;
    struct IxpServer server;
    struct IxpConn *conn;
};

extern struct ixp_context ctx;

extern char *get_real_path(const char *path);

extern void unpfs_attach(Ixp9Req *r);
extern void unpfs_clunk(Ixp9Req *r);
extern void unpfs_create(Ixp9Req *r);
extern void unpfs_flush(Ixp9Req *r);
extern void unpfs_open(Ixp9Req *r);
extern void unpfs_read(Ixp9Req *r);
extern void unpfs_remove(Ixp9Req *r);
extern void unpfs_stat(Ixp9Req *r);
extern void unpfs_walk(Ixp9Req *r);
extern void unpfs_write(Ixp9Req *r);
extern void unpfs_wstat(Ixp9Req *r);
extern void unpfs_freefid(IxpFid *r);

#endif  /* UNPFS_OPS_H */
