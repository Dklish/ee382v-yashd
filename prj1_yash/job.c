/* job.c - the job table and the jobs / fg / bg builtins */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "yash.h"

static job_t jobs[MAX_JOBS];

void jobs_init(void)
{
    memset(jobs, 0, sizeof(jobs));
}

/* table helpers */

static job_t *job_find(pid_t pgid)
{
    int i;
    for (i = 0; i < MAX_JOBS; i++)
        if (jobs[i].used && jobs[i].pgid == pgid)
            return &jobs[i];
    return NULL;
}

static job_t *job_by_num(int num)
{
    int i;
    for (i = 0; i < MAX_JOBS; i++)
        if (jobs[i].used && jobs[i].num == num)
            return &jobs[i];
    return NULL;
}

/* The current job is the highest numbered one and marked 
with '+' in the jobs list and is the job that fg acts on. */
static job_t *job_current(void)
{
    int i;
    job_t *best = NULL;
    for (i = 0; i < MAX_JOBS; i++)
        if (jobs[i].used && (best == NULL || jobs[i].num > best->num))
            best = &jobs[i];
    return best;
}

/* Adds a job and returns its number, or -1 if table is full 
and new job gets 1 + the highest number in use, so gaps are not reused */
int job_add(pid_t pgid, jstate_t st, const char *cmd)
{
    int i;
    job_t *cur = job_current();
    for (i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].used) {
            jobs[i].used  = 1;
            jobs[i].num   = cur ? cur->num + 1 : 1;
            jobs[i].pgid  = pgid;
            jobs[i].state = st;
            snprintf(jobs[i].cmd, sizeof(jobs[i].cmd), "%s", cmd);
            return jobs[i].num;
        }
    }
    return -1;
}

/* Prints one line of the jobs list (Ex: [2]+  Running   sleep 5 &) and
   the current job gets '+', every other job gets '-' and 
   running jobs show a trailing '&'. */
static void job_print(job_t *j)
{
    const char *state = j->state == JOB_RUNNING ? "Running" :
                        j->state == JOB_STOPPED ? "Stopped" : "Done";
    printf("[%d]%c  %-8s  %s%s\n", j->num, j == job_current() ? '+' : '-',
           state, j->cmd, j->state == JOB_RUNNING ? " &" : "");
}

/* Prints the jobs in number order (all of them, or only the Done ones),
   then removes the Done ones: a finished job is reported once. */
static void print_jobs(int only_done)
{
    int n, i;
    job_t *j, *cur = job_current();

    for (n = 1; cur && n <= cur->num; n++) {
        j = job_by_num(n);
        if (j && (!only_done || j->state == JOB_DONE))
            job_print(j);
    }
    for (i = 0; i < MAX_JOBS; i++)
        if (jobs[i].used && jobs[i].state == JOB_DONE)
            jobs[i].used = 0;
}

/* reaping  */

/* Checks each background job for state changes without blocking and
   a negative pid makes waitpid() look at every process in that group and
   several children can change state before the handler runs,
   but only one SIGCHLD is delivered for all of them. */
void jobs_update(void)
{
    int i, status;
    job_t *j;

    for (i = 0; i < MAX_JOBS; i++) {
        j = &jobs[i];
        if (!j->used || j->state == JOB_DONE || j->pgid == fg_pgid)
            continue;
        while (waitpid(-j->pgid, &status, WNOHANG | WUNTRACED | WCONTINUED) > 0) {
            if (WIFEXITED(status) || WIFSIGNALED(status))
                j->state = JOB_DONE;
            else if (WIFSTOPPED(status))
                j->state = JOB_STOPPED;
            else if (WIFCONTINUED(status))
                j->state = JOB_RUNNING;
        }
    }
}

/* Runs a job in the foreground and waits until it finishes or stops */
void foreground(pid_t pgid, const char *cmd, int cont)
{
    int status = 0;
    int stopped = 0;
    pid_t p;
    job_t *j;

    fg_pgid = pgid;

    /* Give the job the terminal so it can read the keyboard and receive Ctrl-C / Ctrl-Z */
    tcsetpgrp(STDIN_FILENO, pgid);
    if (cont)
        kill(-pgid, SIGCONT);               /* resume every process in the job */

    /* Wait on the whole process group and waitpid() returns -1 (ECHILD)
     once no processes are left in the group. */
    for (;;) {
        p = waitpid(-pgid, &status, WUNTRACED);
        if (p < 0) {
            if (errno == EINTR)
                continue;                   /* interrupted by a signal so retry */
            break;
        }
        if (WIFSTOPPED(status)) {           /* Ctrl-Z to stop waiting */
            stopped = 1;
            break;
        }
    }

    tcsetpgrp(STDIN_FILENO, getpgrp());     /* shell takes the terminal back */
    fg_pgid = 0;

    /* The terminal echoes ^C / ^Z without a newline then move the prompt down. */
    if (stopped || (WIFSIGNALED(status) && WTERMSIG(status) == SIGINT))
        putchar('\n');

    /* A stopped job is kept in or added to the table and 
    a job resumed with fg that has now finished is then removed. */
    j = job_find(pgid);
    if (stopped) {
        if (j)
            j->state = JOB_STOPPED;
        else
            job_add(pgid, JOB_STOPPED, cmd);
    } else if (j) {
        j->used = 0;
    }
}

/* builtins */

void jobs_builtin(void)
{
    jobs_update();
    print_jobs(0);
}

void jobs_report_done(void)
{
    jobs_update();
    print_jobs(1);
}

/* fg- print the most recent job's command, resume it, and wait for it. */
void fg_builtin(void)
{
    job_t *j = job_current();
    if (j == NULL)
        return;

    printf("%s\n", j->cmd);
    fflush(stdout);                         /* print before the job's output */
    j->state = JOB_RUNNING;
    foreground(j->pgid, j->cmd, 1);
}

/* bg- resume the most recent *stopped* job in the background and print it
   with a trailing '&'*/
void bg_builtin(void)
{
    int i;
    job_t *j = NULL;

    for (i = 0; i < MAX_JOBS; i++)
        if (jobs[i].used && jobs[i].state == JOB_STOPPED &&
            (j == NULL || jobs[i].num > j->num))
            j = &jobs[i];
    if (j == NULL)
        return;

    j->state = JOB_RUNNING;
    kill(-j->pgid, SIGCONT);
    printf("[%d]%c %s &\n", j->num, j == job_current() ? '+' : '-', j->cmd);
}

/* On exit, kill every remaining job. */
void jobs_kill_all(void)
{
    int i;
    for (i = 0; i < MAX_JOBS; i++)
        if (jobs[i].used && jobs[i].state != JOB_DONE)
            kill(-jobs[i].pgid, SIGKILL);
}
