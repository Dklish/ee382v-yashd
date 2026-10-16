/* shell.h - the shell side's own functions (exec.c, job.c)
 *
 * Every function takes the client's session_t, which holds that client's
 * job table, foreground job and socket. 
 */
#ifndef SHELL_H
#define SHELL_H

#include "session.h" 

/* exec.c */
/* Starts the command for one line. Returns 1 if there's nothing to wait
   for (background job, or the launch failed), 0 if a foreground job is running. */
int  launch(session_t *s, pipeline_t *pl);

/* job.c */
void jobs_init(session_t *s);
int  job_add(session_t *s, pid_t pgid, jstate_t st, const char *cmd);
void jobs_update(session_t *s);       /* reap background jobs without blocking */
int  foreground_poll(session_t *s);   /* 1 once the foreground job ended or stopped */
void jobs_builtin(session_t *s);
int  fg_builtin(session_t *s);        /* 0 if a job was resumed (now in the foreground) */
void bg_builtin(session_t *s);
void jobs_report_done(session_t *s);  /* print "Done" entries before the prompt */
void jobs_kill_all(session_t *s);     /* kill and reap every job this client has */

#endif
