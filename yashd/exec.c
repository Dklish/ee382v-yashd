/* exec.c - start the processes for a command line (Project 1's pipe.c)
 *
 * What changed from Project 1:
 *   - a command's stdout/stderr go to the client's socket, not the terminal
 *   - a foreground command's stdin is a pipe that typed text is written into;
 *     a background command's stdin is /dev/null
 *   - no tcsetpgrp(): the daemon has no terminal
 *   - no waiting here: a foreground job is recorded in the session and
 *     Diego's thread checks on it with session_poll()
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "shell.h"

/* The daemon ignores SIGPIPE and SIGHUP, and an ignored signal stays ignored
   after exec(), so put back the defaults a command expects */
static void reset_child_signals(void)
{
    signal(SIGINT,  SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGPIPE, SIG_DFL);
    signal(SIGHUP,  SIG_DFL);
}

/* Close-on-exec: all clients share one process, so a command forked for one
   client must not inherit another client's pipes (its reader would never see EOF) */
static void cloexec(int fd)
{
    fcntl(fd, F_SETFD, FD_CLOEXEC);
}

/* Everything a child does between fork() and exec(), and never returns.
   pgid 0 starts a new process group, else the child joins pgid.
   in_fd becomes stdin and out_fd stdout; stderr always goes to the client.
   The extra fds are close-on-exec, so exec() closes them for us. */
static void child_exec(session_t *s, cmd_t *c, pid_t pgid, int in_fd, int out_fd)
{
    setpgid(0, pgid);
    reset_child_signals();

    dup2(in_fd, STDIN_FILENO);
    dup2(out_fd, STDOUT_FILENO);
    dup2(s->sock, STDERR_FILENO);

    /* Redirections run after the pipes are connected so
       an explicit file takes priority over the pipe on that side */
    if (apply_redirections(c) < 0)
        _exit(1);                    /* input file missing */

    execvp(c->argv[0], c->argv);     /* searches PATH and returns only on error */
    _exit(1);                        /* unknown command: print nothing */
}

/* Records a started job as the foreground job and returns 0 ("running").
   in_fd is the write end of its stdin pipe. */
static int start_foreground(session_t *s, pid_t pgid, int in_fd, const char *cmd)
{
    s->in_fd = in_fd;
    snprintf(s->fg_cmd, sizeof(s->fg_cmd), "%s", cmd);
    s->fg_pgid = pgid;
    return 0;
}

static int launch_single(session_t *s, pipeline_t *pl)
{
    int in[2] = { -1, -1 };
    pid_t pid;

    /* A foreground command gets a pipe for its stdin */
    if (!pl->bg) {
        if (pipe(in) < 0)
            return 1;
        cloexec(in[0]);
        cloexec(in[1]);
    }

    pid = fork();
    if (pid < 0) {
        if (!pl->bg) {
            close(in[0]);
            close(in[1]);
        }
        return 1;
    }
    if (pid == 0) {
        int child_in = pl->bg ? open("/dev/null", O_RDONLY | O_CLOEXEC) : in[0];
        child_exec(s, &pl->cmds[0], 0, child_in, s->sock);
    }

    /* Parent sets the child's group too */
    setpgid(pid, pid);

    if (pl->bg) {
        job_add(s, pid, JOB_RUNNING, pl->raw);   /* if background then don't wait */
        return 1;
    }
    close(in[0]);                                /* only the child reads it */
    return start_foreground(s, pid, in[1], pl->raw);
}

/* For piping, the left command's stdout goes into the pipe and the right
   command's stdin reads from it, with pipefd[1] the write end and pipefd[0]
   the read end. */
static int launch_pipeline(session_t *s, pipeline_t *pl)
{
    int in[2], pipefd[2];
    pid_t left, right = -1;

    if (pipe(in) < 0)
        return 1;
    if (pipe(pipefd) < 0) {
        close(in[0]);
        close(in[1]);
        return 1;
    }
    cloexec(in[0]);
    cloexec(in[1]);
    cloexec(pipefd[0]);
    cloexec(pipefd[1]);

    left = fork();
    if (left == 0)
        child_exec(s, &pl->cmds[0], 0, in[0], pipefd[1]);     /* new group, pgid == left */
    if (left > 0) {
        setpgid(left, left);
        right = fork();
        if (right == 0)
            child_exec(s, &pl->cmds[1], left, pipefd[0], s->sock);  /* join the left's group */
    }

    /* The shell closes both ends of the pipe between the commands, and the read
       end of the stdin pipe, so EOF works */
    close(pipefd[0]);
    close(pipefd[1]);
    close(in[0]);

    if (left < 0 || right < 0) {                 /* a fork failed: clean up what started */
        if (left > 0) {
            kill(-left, SIGKILL);
            while (waitpid(-left, NULL, 0) > 0)
                ;
        }
        close(in[1]);
        return 1;
    }
    setpgid(right, left);
    return start_foreground(s, left, in[1], pl->raw);
}

int launch(session_t *s, pipeline_t *pl)
{
    if (pl->ncmds == 1)
        return launch_single(s, pl);
    return launch_pipeline(s, pl);
}
