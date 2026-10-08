/* signals.c - the shell's signal handling */
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "yash.h"

/* Ctrl-C- pass it to the foreground job, if there is one */
static void sig_int(int signo)
{
    (void)signo;
    if (fg_pgid > 0)
        kill(-fg_pgid, SIGINT);
}

/* Ctrl-Z- pass it to the foreground job, which then stops and shell is never stopped */
static void sig_tstp(int signo)
{
    (void)signo;
    if (fg_pgid > 0)
        kill(-fg_pgid, SIGTSTP);
}

/* When a child stopped, continued or exited then update the job table and
   waitpid() inside jobs_update() can change errno while the main program 
   is in the middle of checking it, so errno is saved and restored. */
static void sig_chld(int signo)
{
    (void)signo;
    int saved_errno = errno;
    jobs_update();
    errno = saved_errno;
}

void install_shell_handlers(void)
{
    if (signal(SIGINT,  sig_int)  == SIG_ERR)
        perror("signal(SIGINT)");
    if (signal(SIGTSTP, sig_tstp) == SIG_ERR)
        perror("signal(SIGTSTP)");
    if (signal(SIGCHLD, sig_chld) == SIG_ERR)
        perror("signal(SIGCHLD)");

    /* When a fg job ends, the shell takes the terminal back with tcsetpgrp() 
    so the call raises SIGTTOU which would stop the shell so ignore it. */
    signal(SIGTTOU, SIG_IGN);
}
