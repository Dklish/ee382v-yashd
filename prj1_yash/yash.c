/* yash.c - the main read/parse/execute loop
 *
 * Each command line is handled like this:
 *     cmd             fork -> exec -> wait
 *     cmd > file      fork -> redirect -> exec -> wait
 *     cmd1 | cmd2     pipe -> fork x2 -> connect the pipe -> exec x2 -> wait
 *     cmd &           fork -> exec, no wait
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "yash.h"

volatile pid_t fg_pgid = 0;

/* Runs jobs, fg or bg inside the shell and returns 1*/
static int run_builtin(pipeline_t *pl)
{
    const char *c = pl->cmds[0].argv[0];

    /* Builtins are supported on their own */
    if (pl->ncmds != 1 || pl->bg)
        return 0;

    if (strcmp(c, "jobs") == 0) { 
        jobs_builtin(); 
        return 1; 
    }

    if (strcmp(c, "fg")   == 0) { 
        fg_builtin();   
        return 1; 
    }
    if (strcmp(c, "bg")   == 0) { 
        bg_builtin();   
        return 1; 
    }
    return 0;
}

int main(void)
{
    char line[MAX_LINE];
    pipeline_t pl;

    jobs_init();
    install_shell_handlers();

    for (;;) {
        /* Report background jobs that finished since the last prompt */
        jobs_report_done();

        /*The prompt has no newline, so flush it to make it appear now */
        fputs("# ", stdout);
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            if (ferror(stdin) && errno == EINTR) {
                /* A signal interrupted the read so clear the error and show a new prompt*/
                clearerr(stdin);
                putchar('\n');
                continue;
            }
            break;  /* Ctrl-D- exit the shell */
        }

        /* Remove the trailing newline or execvp() would look for "ls\n" */
        line[strcspn(line, "\n")] = '\0';

        if (parse_line(line, &pl) < 0)
            continue;                   /* when empty or invalid line then reprompt */

        if (run_builtin(&pl))
            continue;

        launch(&pl);
    }

    /* Make sure no child process outlives the shell */
    jobs_kill_all();
    putchar('\n');
    return 0;
}
