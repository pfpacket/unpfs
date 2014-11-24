
#include <unpfs/ops.h>
#include <unpfs/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>

static struct Ixp9Srv srv;
static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t signal_num = 0;

static void
usage(const char *program)
{
    printf("Usage: %s proto!addr[!port] ROOT\n"
            "Examples: %s unix!mysrv /\n"
            "          %s tcp!localhost!564 /var/www/\n",
            program, program, program);
}

static void
fatal(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    exit(EXIT_FAILURE);
}

static char *
remove_terminal_slash(char *path)
{
    size_t length = strlen(path);

    /* Never remove root, "/" */
    if (length > 1 && path[length - 1] == '/')
        path[length - 1] = '\0';

    return path;
}

static void
register_signal_handler(int signum, void (*handler)(int))
{
    struct sigaction sig;

    sig.sa_handler = handler;
    sigemptyset(&sig.sa_mask);
    sig.sa_flags = SA_NODEFER;

    if (sigaction(signum, &sig, NULL) < 0)
        fatal("sigaction: %s\n", strerror(errno));
}

static void
signal_handler(int signum)
{
    switch (signum) {
    case SIGHUP:
    case SIGINT:
    case SIGTERM:
        running = 0;
        signal_num = signum;
        break;
    }
}

static void
unpfs_preselect(IxpServer *server)
{
    server->running = running;
}

int
main(int argc, char **argv)
{
    int ret;

    if (argc < 3) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    srv.attach  = unpfs_attach;
    srv.clunk   = unpfs_clunk;
    srv.create  = unpfs_create;
    srv.flush   = unpfs_flush;
    srv.open    = unpfs_open;
    srv.read    = unpfs_read;
    srv.remove  = unpfs_remove;
    srv.stat    = unpfs_stat;
    srv.version = unpfs_version;
    srv.walk    = unpfs_walk;
    srv.write   = unpfs_write;
    srv.wstat   = unpfs_wstat;
    srv.freefid = unpfs_freefid;

    ctx.fd = ixp_announce(argv[1]);
    if (ctx.fd < 0)
        fatal("ixp_announce: %s\n", ixp_errbuf());

    ctx.root = remove_terminal_slash(argv[2]);
    ctx.conn = ixp_listen(&ctx.server, ctx.fd, &srv, ixp_serve9conn, NULL);
    if (!ctx.conn)
        fatal("ixp_listen: %s\n", ixp_errbuf());

    register_signal_handler(SIGHUP, signal_handler);
    register_signal_handler(SIGINT, signal_handler);
    register_signal_handler(SIGTERM, signal_handler);

    ctx.server.preselect = unpfs_preselect;

    /*unpfs_log_level(LOG_NOTICE);*/
    unpfs_log(LOG_NOTICE,
        "Ready to accept 9P clients: trans=%s root=%s",
        __func__, argv[0], ctx.root);

    /* Server main loop */
    ret = ixp_serverloop(&ctx.server);

    ixp_server_close(&ctx.server);
    unpfs_log(LOG_NOTICE, "[*] Server caught signal: %d", signal_num);

    return ret;
}
