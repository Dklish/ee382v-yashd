/* redirect.c - apply < , > and 2> to a child process */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "yash.h"

/* Permissions for newly created files like owner and group read/write, others read */
#define OUT_PERMS (S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH)

/* Open path and make stdfd (0, 1 or 2) refer to it */
static int redirect_one(const char *path, int flags, int stdfd)
{
    int fd = open(path, flags, OUT_PERMS);
    if (fd < 0)
        return -1;
    /* dup2 makes stdfd a copy of fd, closing whatever stdfd was before */
    if (dup2(fd, stdfd) < 0) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

int apply_redirections(cmd_t *c)
{
    /* The file must already exist else the command fails */
    if (c->in && redirect_one(c->in, O_RDONLY, STDIN_FILENO) < 0)
        return -1;

    /* Create the file if it doesn't exist, and empty it if it does */
    if (c->out && redirect_one(c->out, O_WRONLY|O_CREAT|O_TRUNC, STDOUT_FILENO) < 0)
        return -1;

    if (c->err && redirect_one(c->err, O_WRONLY|O_CREAT|O_TRUNC, STDERR_FILENO) < 0)
        return -1;

    return 0;
}
