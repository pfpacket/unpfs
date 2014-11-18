
#include <unpfs/common.h>
#include <stdio.h>

void *
zalloc(size_t size)
{
    void *m = malloc(PATH_MAX);
    if (!m) {
        perror("Fatal error: malloc");
        exit(EXIT_FAILURE);
    }
    return m;
}

void
zfree(char **m)
{
    int err = errno;

    if (m && *m) {
        free(*m);
        *m = NULL;
    }

    errno = err;
}
