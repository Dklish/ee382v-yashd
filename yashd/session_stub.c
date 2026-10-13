/* session_stub.c - a placeholder shell so the server can be tested end to end
 *
 * Runs one command at a time: no pipes, redirection, & or jobs table.
 * It gets replaced by the prj1 shell, which implements the same functions
 * from session.h. Kept short on purpose; it only shows the contract:
 *   - the child's stdout/stderr go to the client socket
 *   - the child's stdin is a pipe the thread writes typed text into
 *   - session_cmd() starts the job and returns; session_poll() reaps it
 */
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "session.h"

#define MAX_ARGS 128

void session_start(session_t *s)
{
    s->fg_pgid = 0;
    s->in_fd = -1;
}

int session_cmd(session_t *s, char *line)
{
    char *argv[MAX_ARGS], *tok, *save;
    int argc = 0, inpipe[2];
    pid_t pid;

    for (tok = strtok_r(line, " \t", &save); tok && argc < MAX_ARGS - 1;
         tok = strtok_r(NULL, " \t", &save))
        argv[argc++] = tok;
    argv[argc] = NULL;
    if (argc == 0)
        return 1;                    /* empty line: just re-prompt */

    /* Close-on-exec: commands other threads fork must not inherit this pipe,
       or this job's stdin would never see EOF */
    if (pipe(inpipe) < 0)
        return 1;
    fcntl(inpipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(inpipe[1], F_SETFD, FD_CLOEXEC);

    pid = fork();
    if (pid < 0) {
        close(inpipe[0]);
        close(inpipe[1]);
        return 1;
    }
    if (pid == 0) {
        setpgid(0, 0);
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);    /* the daemon ignores it; commands should not */
        dup2(inpipe[0], STDIN_FILENO);
        dup2(s->sock, STDOUT_FILENO);
        dup2(s->sock, STDERR_FILENO);
        execvp(argv[0], argv);
        _exit(1);                    /* unknown command: print nothing */
    }

    setpgid(pid, pid);
    close(inpipe[0]);
    s->in_fd = inpipe[1];
    s->fg_pgid = pid;
    return 0;                        /* running: the thread polls until it ends */
}

int session_poll(session_t *s)
{
    int status;
    pid_t p;

    p = waitpid(-s->fg_pgid, &status, WNOHANG | WUNTRACED);
    if (p <= 0)
        return 0;                    /* still running */

    /* exited, killed or stopped: the stub just forgets it (no jobs table) */
    if (s->in_fd >= 0) {
        close(s->in_fd);
        s->in_fd = -1;
    }
    s->fg_pgid = 0;
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
        waitpid(-s->fg_pgid, NULL, 0);
    }
    if (s->in_fd >= 0)
        close(s->in_fd);
}
