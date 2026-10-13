/* yashd.c - the yash daemon
 *
 *   main()          becomes a daemon, listens on port 3820, starts one thread per client
 *   serve_client()  the thread: reads protocol messages and hands them to the shell side
 *
 * Protocol (client to server), one message per line:
 *   CMD <command>\n     run a command
 *   CTL <c|z|d>\n       Ctrl-C, Ctrl-Z, Ctrl-D
 *   anything else       plain text for the running command's stdin
 * The server sends back plain text, ending each command with the prompt "\n# ".
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "session.h"

#define PID_FILE  "/tmp/yashd.pid"
#define POLL_MS   50      /* how often to check a running foreground job */

/* ---------------- sending to the client ---------------- */

/* write() may send only part of the buffer, so loop until it is all out */
void session_send(session_t *s, const char *buf, size_t len)
{
    ssize_t n;

    while (len > 0) {
        n = write(s->sock, buf, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return;                 /* client is gone; the read side will notice */
        }
        buf += n;
        len -= n;
    }
}

void session_printf(session_t *s, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > (int)sizeof(buf) - 1)
        n = sizeof(buf) - 1;
    if (n > 0)
        session_send(s, buf, n);
}

static void send_prompt(session_t *s)
{
    session_send(s, "\n# ", 3);
}

/* ---------------- reading the protocol ---------------- */

/* Handles one message. msg[len] is the '\n' that ended it.
   Returns 0 when the client is leaving (CTL d at the prompt). */
static int handle_msg(session_t *s, char *msg, int len)
{
    if (len >= 4 && strncmp(msg, "CMD ", 4) == 0) {
        msg[len] = '\0';
        log_cmd(s, msg + 4);
        if (session_cmd(s, msg + 4))
            send_prompt(s);

    } else if (len >= 5 && strncmp(msg, "CTL ", 4) == 0) {
        char c = msg[4];
        if (s->fg_pgid > 0)
            session_ctl(s, c);      /* goes to the running job */
        else if (c == 'd')
            return 0;               /* Ctrl-D at the prompt: client is done */
        else
            send_prompt(s);         /* Ctrl-C / Ctrl-Z with nothing running */

    } else if (s->fg_pgid > 0) {
        session_input(s, msg, len + 1);   /* plain text, newline included */
    }
    return 1;
}

/* Handles every complete line in the buffer. A CMD that arrives while a
   foreground job is still running waits in the buffer until that job ends,
   so commands sent back to back still run one after another.
   Returns 0 when the client is leaving. */
static int handle_lines(session_t *s)
{
    char *nl;
    int len;

    while ((nl = memchr(s->inbuf, '\n', s->inlen)) != NULL) {
        len = nl - s->inbuf;
        if (s->fg_pgid > 0 && len >= 4 && strncmp(s->inbuf, "CMD ", 4) == 0)
            return 1;               /* next command: wait for this one to finish */
        if (handle_msg(s, s->inbuf, len) == 0)
            return 0;
        /* drop the line we just handled */
        s->inlen -= len + 1;
        memmove(s->inbuf, nl + 1, s->inlen);
    }
    return 1;
}

/* Reads what is waiting on the socket and handles every complete line.
   Returns 0 when the client closed the connection or sent CTL d at the prompt. */
static int read_messages(session_t *s)
{
    int n;

    n = read(s->sock, s->inbuf + s->inlen, sizeof(s->inbuf) - s->inlen - 1);
    if (n <= 0)
        return (n < 0 && errno == EINTR) ? 1 : 0;
    s->inlen += n;

    if (handle_lines(s) == 0)
        return 0;

    /* a line longer than the buffer: pass it on as plain text so it can't wedge us */
    if (s->inlen == (int)sizeof(s->inbuf) - 1) {
        if (s->fg_pgid > 0)
            session_input(s, s->inbuf, s->inlen);
        s->inlen = 0;
    }
    return 1;
}

/* ---------------- one thread per client ---------------- */

static void *serve_client(void *arg)
{
    session_t *s = arg;
    struct pollfd pfd;
    int n;

    session_start(s);
    send_prompt(s);

    for (;;) {
        /* Wait for the client to send something. While a foreground job runs,
           wake up every POLL_MS to check on it, so the prompt goes out when it ends. */
        pfd.fd = s->sock;
        pfd.events = POLLIN;
        pfd.revents = 0;
        n = poll(&pfd, 1, s->fg_pgid > 0 ? POLL_MS : -1);
        if (n < 0 && errno != EINTR)
            break;

        if (n > 0 && read_messages(s) == 0)
            break;

        if (s->fg_pgid > 0 && session_poll(s)) {
            send_prompt(s);
            if (handle_lines(s) == 0)   /* run any command that was waiting */
                break;
        }
    }

    session_end(s);
    close(s->sock);
    free(s);
    return NULL;
}

/* ---------------- daemon setup ---------------- */

/* From the class server example: lets bind() reuse the port right after a restart */
static void reusePort(int sock)
{
    int one = 1;

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) == -1) {
        log_error("setsockopt(SO_REUSEADDR) failed");
        exit(EXIT_FAILURE);
    }
}

/* Turns this process into a daemon */
static void daemon_init(void)
{
    pid_t pid;
    int fd, i, maxfd;
    char buf[32];

    /* 1. Run in the background: the parent exits, the child carries on */
    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (pid > 0)
        exit(EXIT_SUCCESS);

    /* 2. New session: no controlling terminal, so terminal signals can't reach us */
    setsid();
    signal(SIGHUP, SIG_IGN);

    /* 3. Don't keep any directory busy, and don't inherit a file-creation mask */
    if (chdir("/") < 0)
        exit(EXIT_FAILURE);
    umask(0);

    /* 4. Close everything inherited and point stdin/stdout/stderr at /dev/null */
    maxfd = sysconf(_SC_OPEN_MAX);
    if (maxfd < 0 || maxfd > 1024)
        maxfd = 1024;
    for (i = 0; i < maxfd; i++)
        close(i);
    fd = open("/dev/null", O_RDWR);
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO)
        close(fd);

    /* 5. Only one yashd at a time: lock a pid file */
    fd = open(PID_FILE, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0 || lockf(fd, F_TLOCK, 0) < 0) {
        log_error("already running (could not lock " PID_FILE ")");
        exit(EXIT_FAILURE);
    }
    if (ftruncate(fd, 0) == 0) {
        snprintf(buf, sizeof(buf), "%d\n", (int)getpid());
        if (write(fd, buf, strlen(buf)) < 0)
            log_error("could not write " PID_FILE);
    }
    /* fd stays open: the lock lasts as long as the daemon runs */
}

int main(void)
{
    int lsock, csock;
    struct sockaddr_in addr, from;
    socklen_t fromlen;
    pthread_t tid;
    pthread_attr_t attr;
    session_t *s;

    daemon_init();

    /* A client that disconnects must not kill the whole daemon with SIGPIPE */
    signal(SIGPIPE, SIG_IGN);

    lsock = socket(AF_INET, SOCK_STREAM, 0);
    if (lsock < 0) {
        log_error("socket() failed");
        exit(EXIT_FAILURE);
    }
    /* Close-on-exec on every socket: commands we fork for one client must not
       inherit the listening socket or other clients' sockets */
    fcntl(lsock, F_SETFD, FD_CLOEXEC);
    reusePort(lsock);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(YASHD_PORT);
    if (bind(lsock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("bind() to port 3820 failed");
        exit(EXIT_FAILURE);
    }
    if (listen(lsock, 5) < 0) {
        log_error("listen() failed");
        exit(EXIT_FAILURE);
    }

    /* Detached threads clean themselves up when they return */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    for (;;) {
        fromlen = sizeof(from);
        csock = accept(lsock, (struct sockaddr *)&from, &fromlen);
        if (csock < 0)
            continue;               /* interrupted, or one bad connection: keep serving */
        fcntl(csock, F_SETFD, FD_CLOEXEC);

        s = calloc(1, sizeof(*s));
        if (s == NULL) {
            close(csock);
            continue;
        }
        s->sock = csock;
        s->in_fd = -1;
        inet_ntop(AF_INET, &from.sin_addr, s->ip, sizeof(s->ip));
        s->port = ntohs(from.sin_port);

        if (pthread_create(&tid, &attr, serve_client, s) != 0) {
            close(csock);
            free(s);
        }
    }
}
