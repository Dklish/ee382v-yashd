/* job.c - the job table and the jobs / fg / bg builtins (from Project 1)
 *
 * What changed from Project 1:
 *   - the table lives in the client's session_t (s->jobs), not a global,
 *     so every client has its own jobs
 *   - output goes to the client with session_printf() instead of printf()
 *   - foreground() is split: launch()/fg_builtin() start the job and return,
 *     and foreground_poll() checks on it without blocking (called by
 *     session_poll() about every 50ms from thread)
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "shell.h"

void jobs_init(session_t *s)
{
    memset(s->jobs, 0, sizeof(s->jobs));
}

/* table helpers */

static job_t *job_find(session_t *s, pid_t pgid)
{
    int i;
    for (i = 0; i < MAX_JOBS; i++)
        if (s->jobs[i].used && s->jobs[i].pgid == pgid)
            return &s->jobs[i];
    return NULL;
}

static job_t *job_by_num(session_t *s, int num)
{
    int i;
    for (i = 0; i < MAX_JOBS; i++)
        if (s->jobs[i].used && s->jobs[i].num == num)
            return &s->jobs[i];
    return NULL;
}

/* The current job is the highest numbered one and marked
   with '+' in the jobs list and is the job that fg acts on. */
static job_t *job_current(session_t *s)
{
    int i;
    job_t *best = NULL;
    for (i = 0; i < MAX_JOBS; i++)
        if (s->jobs[i].used && (best == NULL || s->jobs[i].num > best->num))
            best = &s->jobs[i];
    return best;
}

/* Adds a job and returns its number, or -1 if table is full
   and new job gets 1 + the highest number in use*/
int job_add(session_t *s, pid_t pgid, jstate_t st, const char *cmd)
{
    int i;
    job_t *cur = job_current(s);
    for (i = 0; i < MAX_JOBS; i++) {
        if (!s->jobs[i].used) {
            s->jobs[i].used  = 1;
            s->jobs[i].num   = cur ? cur->num + 1 : 1;
            s->jobs[i].pgid  = pgid;
            s->jobs[i].state = st;
            s->jobs[i].in_fd = -1;
            snprintf(s->jobs[i].cmd, sizeof(s->jobs[i].cmd), "%s", cmd);
            return s->jobs[i].num;
        }
    }
    return -1;
}

/* Prints one line of the jobs list (Ex: [2]+  Running   sleep 5 &) and
   the current job gets '+', every other job gets '-' and
   running jobs show a trailing '&'. */
static void job_print(session_t *s, job_t *j)
{
    const char *state = j->state == JOB_RUNNING ? "Running" :
                        j->state == JOB_STOPPED ? "Stopped" : "Done";
    session_printf(s, "[%d]%c  %-8s  %s%s\n", j->num, j == job_current(s) ? '+' : '-',
                   state, j->cmd, j->state == JOB_RUNNING ? " &" : "");
}

/* Prints the jobs in number order (all of them, or only the Done ones),
   then removes the Done ones: a finished job is reported once. */
static void print_jobs(session_t *s, int only_done)
{
    int n, i;
    job_t *j, *cur = job_current(s);

    for (n = 1; cur && n <= cur->num; n++) {
        j = job_by_num(s, n);
        if (j && (!only_done || j->state == JOB_DONE))
            job_print(s, j);
    }
    for (i = 0; i < MAX_JOBS; i++)
        if (s->jobs[i].used && s->jobs[i].state == JOB_DONE) {
            if (s->jobs[i].in_fd >= 0)
                close(s->jobs[i].in_fd);
            s->jobs[i].used = 0;
        }
}

/* reaping */

/* Checks each background job for state changes without blocking and
   a negative pid makes waitpid() look at every process in that group.
   Only this client's own groups are checked, never waitpid(-1), so one
   client never reaps another client's children. */
void jobs_update(session_t *s)
{
    int i, status;
    job_t *j;

    for (i = 0; i < MAX_JOBS; i++) {
        j = &s->jobs[i];
        if (!j->used || j->state == JOB_DONE || j->pgid == s->fg_pgid)
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

/* Project 1's foreground() wait loop, without blocking.
   Returns 0 while the foreground job is still running, 1 once every process
   in its group has exited (waitpid() returns -1, ECHILD) or it stopped. */
int foreground_poll(session_t *s)
{
    int status = 0;
    int stopped = 0;
    pid_t p, pgid = s->fg_pgid;
    job_t *j;

    for (;;) {
        p = waitpid(-pgid, &status, WNOHANG | WUNTRACED);
        if (p == 0)
            return 0;                       /* still running: check again later */
        if (p < 0) {
            if (errno == EINTR)
                continue;                   /* interrupted by a signal so retry */
            break;                          /* no processes left: the job is done */
        }
        if (WIFSTOPPED(status)) {           /* Ctrl-Z */
            stopped = 1;
            break;
        }
        /* one process exited; loop, since a pipeline has two */
    }

    /* A stopped job is kept in or added to the table and
       a job resumed with fg that has now finished is then removed. */
    j = job_find(s, pgid);
    if (stopped) {
        if (j == NULL && job_add(s, pgid, JOB_STOPPED, s->fg_cmd) > 0)
            j = job_find(s, pgid);
        if (j) {
            /* keep its stdin pipe open so typing still reaches it after fg */
            j->state = JOB_STOPPED;
            j->in_fd = s->in_fd;
            s->in_fd = -1;
        }
    } else if (j) {
        j->used = 0;
    }

    if (s->in_fd >= 0) {                    /* nobody will type into it any more */
        close(s->in_fd);
        s->in_fd = -1;
    }
    s->fg_pgid = 0;
    return 1;
}

/* builtins */

void jobs_builtin(session_t *s)
{
    jobs_update(s);
    print_jobs(s, 0);
}

void jobs_report_done(session_t *s)
{
    jobs_update(s);
    print_jobs(s, 1);
}

/* fg- print the most recent job's command and resume it in the foreground.
   Returns 0 when a job was resumed (thread then polls it), 1 if there's none. */
int fg_builtin(session_t *s)
{
    job_t *j = job_current(s);
    if (j == NULL)
        return 1;

    session_printf(s, "%s\n", j->cmd);
    j->state = JOB_RUNNING;
    snprintf(s->fg_cmd, sizeof(s->fg_cmd), "%s", j->cmd);
    s->in_fd = j->in_fd;                    /* typed text goes to it again */
    j->in_fd = -1;
    s->fg_pgid = j->pgid;
    kill(-j->pgid, SIGCONT);                /* resume every process in the job */
    return 0;
}

/* bg- resume the most recent *stopped* job in the background and print it
   with a trailing '&'*/
void bg_builtin(session_t *s)
{
    int i;
    job_t *j = NULL;

    for (i = 0; i < MAX_JOBS; i++)
        if (s->jobs[i].used && s->jobs[i].state == JOB_STOPPED &&
            (j == NULL || s->jobs[i].num > j->num))
            j = &s->jobs[i];
    if (j == NULL)
        return;

    j->state = JOB_RUNNING;
    kill(-j->pgid, SIGCONT);
    session_printf(s, "[%d]%c %s &\n", j->num, j == job_current(s) ? '+' : '-', j->cmd);
}

/* When the client leaves, kill every remaining job and reap it,
   since a daemon runs for a long time and zombies would pile up. */
void jobs_kill_all(session_t *s)
{
    int i;
    for (i = 0; i < MAX_JOBS; i++)
        if (s->jobs[i].used && s->jobs[i].state != JOB_DONE) {
            kill(-s->jobs[i].pgid, SIGKILL);
            while (waitpid(-s->jobs[i].pgid, NULL, 0) > 0)
                ;
            if (s->jobs[i].in_fd >= 0)
                close(s->jobs[i].in_fd);
        }
}
