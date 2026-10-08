/* yash.h - shared types and declarations for yash
 *
 * How a line flows through the shell:
 *   input line -> parse_line() -> pipeline_t (one or two cmd_t's)
 *              -> launch()     -> fork/exec the command(s)
 *              -> job_t        (only for jobs run with & or stopped with ^Z)
 */
#ifndef YASH_H
#define YASH_H

#include <sys/types.h>

#define MAX_LINE   256   /* input lines are at most 200 characters */
#define MAX_ARGS   128   /* enough tokens for a 200-char line */
#define MAX_JOBS   20   /* at most 20 jobs at a time */
#define MAX_CMDS   2     /* at most one '|' per line so two commands */

/* One command- its argument list plus any redirections*/
typedef struct {
    char *argv[MAX_ARGS];
    int   argc;
    char *in;    /* < target or NULL */
    char *out;   /* > target or NULL */
    char *err;   /* 2> target or NULL */
} cmd_t;

/* A whole input line after parsing */
typedef struct {
    cmd_t cmds[MAX_CMDS];
    int   ncmds;  /* 1, or 2 when a pipe is present */
    int   bg;  /* 1 if the line ended with & */
    char  raw[MAX_LINE];   /* the line as typed, shown by jobs */
} pipeline_t;

typedef enum { JOB_RUNNING, JOB_STOPPED, JOB_DONE } jstate_t;

typedef struct {
    int      num;
    pid_t    pgid;
    jstate_t state;
    int      used;   /* 1 if this table slot is in use */
    char     cmd[MAX_LINE];   /* command string to display */
} job_t;

/* parse.c  */
/* Splits a line into a pipeline_t and returns 0 if it is a runnable command,
   -1 if the line is empty or invalid */
int parse_line(char *line, pipeline_t *pl);

/* redirect.c */
/* Applies <, > and 2> in the child, after fork() and before exec() and
   returns -1 if a file can't be opened like a missing input file. */
int apply_redirections(cmd_t *c);

/*  pipe.c  */
/* Starts the commands for one line, then waits for them or records a background job. */
void launch(pipeline_t *pl);

/* signals.c  */
/* Sets up the shell's signal handlers which are called once at startup */
void install_shell_handlers(void);

/* job.c */
void  jobs_init(void);
int   job_add(pid_t pgid, jstate_t st, const char *cmd);
void  jobs_update(void);   /* reap background jobs (SIGCHLD handler) */
void  foreground(pid_t pgid, const char *cmd, int cont); /* run + wait */
void  jobs_builtin(void);
void  fg_builtin(void);
void  bg_builtin(void);
void  jobs_report_done(void); /* print "Done" entries before the prompt */
void  jobs_kill_all(void); /* kill every job on exit */

/* process group of the job running in the foreground, or 0 at the prompt. */
extern volatile pid_t fg_pgid;

#endif
