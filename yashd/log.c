/* log.c - /tmp/yashd.log in syslog-like format
 *
 *   Oct 12 21:07:55 yashd[127.0.0.1:7813]: ls -l
 *
 * Every client thread logs, so one mutex keeps their lines from interleaving.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include "session.h"

#define LOG_FILE "/tmp/yashd.log"

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

/* Appends one line. O_CLOEXEC so a command forked by another thread
   at the same moment can't inherit the log file. */
static void log_line(const char *who, const char *text)
{
    char when[32];
    time_t now = time(NULL);
    struct tm tm;
    int fd;

    localtime_r(&now, &tm);
    strftime(when, sizeof(when), "%b %d %H:%M:%S", &tm);

    pthread_mutex_lock(&log_lock);
    fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd >= 0) {
        dprintf(fd, "%s yashd[%s]: %s\n", when, who, text);
        close(fd);
    }
    pthread_mutex_unlock(&log_lock);
}

void log_cmd(const session_t *s, const char *cmd)
{
    char who[INET_ADDRSTRLEN + 8];

    snprintf(who, sizeof(who), "%s:%d", s->ip, s->port);
    log_line(who, cmd);
}

void log_error(const char *msg)
{
    log_line("error", msg);
}
