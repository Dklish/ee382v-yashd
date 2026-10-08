/* pipe.c - start the process for a command line */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "yash.h"

/* Children start with a copy of the shell's signal settings */
static void reset_child_signals(void)
{
    signal(SIGINT,  SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
}

/* Everything a child does between fork() and exec() won't return and
   pgid 0 starts a new process group else the child joins pgid and
   fg is 1 for a foreground job */
static void child_exec(cmd_t *c, pid_t pgid, int fg)
{
    setpgid(0, pgid);

    /* A foreground job must own the terminal or it gets stopped as soon
       as it reads from the keyboard so it has to happen before
       SIGTTOU is reset below, or the call could stop the child */
    if (fg)
        tcsetpgrp(STDIN_FILENO, getpgrp());

    reset_child_signals();

    /* Redirections run after any pipe has been connected so
     an explicit file takes priority over the pipe on that side */
    if (apply_redirections(c) < 0)
        _exit(1);                    /* input file missing */

    execvp(c->argv[0], c->argv);     /* searches PATH and returns only on error */

    /* _exit() is used instead of exit() for unknown commands 
    so the copy of the shell's output buffer isn't flushed a second time*/
    _exit(1);
}

static void launch_single(pipeline_t *pl)
{
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }
    if (pid == 0)
        child_exec(&pl->cmds[0], 0, !pl->bg);

    /* Parent sets the child's group too*/
    setpgid(pid, pid);

    if (pl->bg)
        job_add(pid, JOB_RUNNING, pl->raw);   /* if background then don't wait */
    else
        foreground(pid, pl->raw, 0);
}

/* For piping, the left command's stdout goes into the pipe and the right/next
   command's stdin reads from it with pipefd[1] being the write end and pipefd[0] the
   read end */
static void launch_pipeline(pipeline_t *pl)
{
    int pipefd[2];
    pid_t left, right;

    /* Create the pipe before forking so both children inherit it */
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return;
    }

    left = fork();
    if (left < 0) {
        perror("fork");
        return;
    }
    if (left == 0) {
        close(pipefd[0]);                  /* left side never reads */
        dup2(pipefd[1], STDOUT_FILENO);    /* stdout goes into the pipe */
        close(pipefd[1]);
        child_exec(&pl->cmds[0], 0, 1);    /* new group, pgid == left */
    }
    setpgid(left, left);

    right = fork();
    if (right < 0) {
        perror("fork");
        return;
    }
    if (right == 0) {
        close(pipefd[1]);                  /* right side never writes */
        dup2(pipefd[0], STDIN_FILENO);     /* stdin comes from the pipe */
        close(pipefd[0]);
        child_exec(&pl->cmds[1], left, 1); /* join the left child's group */
    }
    setpgid(right, left);

    /* The shell closes both ends*/
    close(pipefd[0]);
    close(pipefd[1]);

    /* Wait for both children */
    foreground(left, pl->raw, 0);
}

void launch(pipeline_t *pl)
{
    /* Flush before fork() so the child doesn't inherit unwritten output
       and print it a second time. */
    fflush(stdout);

    if (pl->ncmds == 1)
        launch_single(pl);
    else
        launch_pipeline(pl);
}
