/* session.c - the shell side of session.h
 */
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "shell.h"

#define MAX_ARGS 128

static int is_builtin(pipeline_t *pl, const char *name)
{
    return pl->ncmds == 1 && !pl->bg && strcmp(pl->cmds[0].argv[0], name) == 0;
}

void session_start(session_t *s)
{
    jobs_init(s);
    s->fg_pgid = 0;
    s->in_fd = -1;
}

int session_cmd(session_t *s, char *line)
{
    pipeline_t pl;
    int done = 1;

    if (parse_line(line, &pl) == 0) {           /* empty or invalid line: just reprompt */
        if (is_builtin(&pl, "jobs"))
            jobs_builtin(s);
        else if (is_builtin(&pl, "fg"))
            done = fg_builtin(s);
        else if (is_builtin(&pl, "bg"))
            bg_builtin(s);
        else
            done = launch(s, &pl);
    }

    /* Report background jobs that finished, right before prompt */
    if (done)
        jobs_report_done(s);
    return done;
}

int session_poll(session_t *s)
{
    if (foreground_poll(s) == 0)
        return 0;
    jobs_report_done(s);
    return 1;
}

void session_ctl(session_t *s, char c)
{
    if (c == 'c')
        kill(-s->fg_pgid, SIGINT);
    else if (c == 'z')
        kill(-s->fg_pgid, SIGTSTP);
    else if (c == 'd' && s->in_fd >= 0) {
        close(s->in_fd);             /* EOF for the job's stdin */
        s->in_fd = -1;
    }
}

void session_input(session_t *s, const char *buf, size_t len)
{
    if (s->in_fd >= 0 && write(s->in_fd, buf, len) < 0) {
        close(s->in_fd);             /* job stopped reading */
        s->in_fd = -1;
    }
}

void session_end(session_t *s)
{
    if (s->fg_pgid > 0) {
        kill(-s->fg_pgid, SIGKILL);
        while (waitpid(-s->fg_pgid, NULL, 0) > 0)
            ;
        s->fg_pgid = 0;
    }
    jobs_kill_all(s);
    if (s->in_fd >= 0) {
        close(s->in_fd);
        s->in_fd = -1;
    }
}
