/* session.h - one connected client, and the calls between the server and the shell
 *
 *   yashd.c   accepts the client, runs one thread per client, reads the protocol
 *   log.c     writes /tmp/yashd.log
 *   shell side (session_stub.c for now, the prj1 shell later) runs the commands
 *
 * The thread never blocks inside the shell: session_cmd() starts a command and
 * returns, and the thread keeps reading the socket (so CTL c / CTL z / typed
 * input still arrive) while it calls session_poll() to see if the job is done.
 */
#ifndef SESSION_H
#define SESSION_H

#include <sys/types.h>
#include <stddef.h>
#include <netinet/in.h>

#define YASHD_PORT  3820
#define INBUF_SIZE  4096   /* protocol lines are at most ~200 characters */

typedef struct {
    /* filled in by yashd.c */
    int   sock;                      /* connected client socket */
    char  ip[INET_ADDRSTRLEN];       /* client address, for the log */
    int   port;                      /* client port, for the log */
    char  inbuf[INBUF_SIZE];         /* bytes read but not yet split into lines */
    int   inlen;

    /* owned by the shell side */
    volatile pid_t fg_pgid;          /* group of the foreground job, 0 at the prompt */
    int   in_fd;                     /* write end of the foreground job's stdin pipe, -1 if none */
    /* TODO (shell side): this client's job table goes here, e.g. job_t jobs[MAX_JOBS]; */
} session_t;

/* ---- implemented by the shell side ---- */

/* Called once when the client connects. */
void session_start(session_t *s);

/* Runs one command line (without "CMD ").
   Returns 1 if it is finished (builtin, background job, bad line) and the prompt
   should be sent now, or 0 if a foreground job is running (fg_pgid is set). */
int  session_cmd(session_t *s, char *line);

/* Called about every 50ms while fg_pgid > 0. Reaps without blocking.
   Returns 1 once the foreground job has exited or stopped (fg_pgid is cleared). */
int  session_poll(session_t *s);

/* CTL message while a foreground job runs: 'c' = SIGINT, 'z' = SIGTSTP, 'd' = EOF on its stdin. */
void session_ctl(session_t *s, char c);

/* Plain text typed by the user, for the foreground job's stdin. */
void session_input(session_t *s, const char *buf, size_t len);

/* The client disconnected: kill every job it still has. */
void session_end(session_t *s);

/* ---- implemented by the server side, for the shell to use ---- */

/* Send text to the client (jobs output, fg/bg messages). */
void session_send(session_t *s, const char *buf, size_t len);
void session_printf(session_t *s, const char *fmt, ...);

/* log.c */
void log_cmd(const session_t *s, const char *cmd);
void log_error(const char *msg);

#endif
