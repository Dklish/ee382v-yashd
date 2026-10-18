/* client.c - yash, the client: a thin terminal for yashd
 *
 *   $ yash <IP_Address_of_Server>
 * Ctrl-D or "quit" at the prompt exits.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT    3820
#define BUFSIZE 4096

static int sock = -1;

/* Handlers only call write(), which is safe inside a signal handler */
static void sig_int(int signo)
{
    (void)signo;
    if (write(sock, "CTL c\n", 6) < 0) { /* nothing to do */ }
}

static void sig_tstp(int signo)
{
    (void)signo;
    if (write(sock, "CTL z\n", 6) < 0) { /* nothing to do */ }
}

static void send_all(const char *buf, size_t len)
{
    ssize_t n;

    while (len > 0) {
        n = write(sock, buf, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        buf += n;
        len -= n;
    }
}

int main(int argc, char *argv[])
{
    struct sockaddr_in server;
    struct sigaction sa;
    struct pollfd pfd[2];
    char buf[BUFSIZE], msg[BUFSIZE + 8];
    char tail[3] = {0, 0, 0};       /* last 3 bytes received, to spot "\n# " */
    int running = 0;                /* 1 between sending CMD and the next prompt */
    int stdin_open = 1;
    int ready = 0;                  /* 1 once the server's first prompt arrives */
    int n, i;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <IP_Address_of_Server>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    if (inet_pton(AF_INET, argv[1], &server.sin_addr) != 1) {
        fprintf(stderr, "yash: bad address %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    /* SA_RESTART so a Ctrl-C doesn't break poll()/read() with an error */
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = sig_int;
    sigaction(SIGINT, &sa, NULL);
    sa.sa_handler = sig_tstp;
    sigaction(SIGTSTP, &sa, NULL);

    for (;;) {
        pfd[0].fd = stdin_open && ready ? STDIN_FILENO : -1;   /* -1 = skip it */
        pfd[0].events = POLLIN;
        pfd[1].fd = sock;
        pfd[1].events = POLLIN;
        pfd[0].revents = pfd[1].revents = 0;

        if (poll(pfd, 2, -1) < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        /* Server output: print it, and watch for the prompt */
        if (pfd[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            n = read(sock, buf, sizeof(buf));
            if (n <= 0)
                break;                              /* server closed the connection */
            if (write(STDOUT_FILENO, buf, n) < 0)
                break;
            for (i = 0; i < n; i++) {
                tail[0] = tail[1];
                tail[1] = tail[2];
                tail[2] = buf[i];
            }
            if (tail[0] == '\n' && tail[1] == '#' && tail[2] == ' ') {
                running = 0;
                ready = 1;
                if (!stdin_open)
                    break;                          /* input file ended while it ran */
            }
        }

        /* User input */
        if (pfd[0].revents & (POLLIN | POLLHUP)) {
            n = read(STDIN_FILENO, buf, sizeof(buf) - 1);
            if (n < 0 && errno == EINTR)
                continue;

            if (n <= 0) {                           /* Ctrl-D */
                send_all("CTL d\n", 6);
                if (!running)
                    break;                          /* at the prompt: leave */
                if (!isatty(STDIN_FILENO))
                    stdin_open = 0;                 /* a file's EOF repeats forever */
                continue;
            }

            if (running) {
                send_all(buf, n);                   /* plain text for the command */
                continue;
            }

            buf[n] = '\0';
            buf[strcspn(buf, "\n")] = '\0';
            if (strcmp(buf, "quit") == 0) {
                send_all("CTL d\n", 6);
                break;
            }
            n = snprintf(msg, sizeof(msg), "CMD %s\n", buf);
            running = 1;
            send_all(msg, n);
        }
    }

    close(sock);
    return 0;
}
