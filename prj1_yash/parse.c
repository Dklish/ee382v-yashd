/* parse.c - split an input line into a pipeline_t */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "yash.h"

/* Fills *pl from line and line is modified in place with strtok_r() writes '\0'
   between tokens, and the argv/filename pointers point into it. */
int parse_line(char *line, pipeline_t *pl)
{
    char *tok, *saveptr, *first = line;
    cmd_t *cur;             /* the command tokens are currently added to */
    int i;

    memset(pl, 0, sizeof(*pl));
    /* Keep a copy of the line before it is split up, for the jobs table. */
    snprintf(pl->raw, sizeof(pl->raw), "%s", line);
    pl->ncmds = 1;
    cur = &pl->cmds[0];

    /* strtok_r takes the string on the first call and NULL after that. */
    for (tok = strtok_r(first, " \t", &saveptr); tok != NULL; tok = strtok_r(NULL, " \t", &saveptr)) {

        if (strcmp(tok, "|") == 0) {
            /* Only one | is allowed, and must follow a command. */
            if (pl->ncmds == MAX_CMDS || cur->argc == 0)
                return -1;
            pl->ncmds = 2;
            cur = &pl->cmds[1];

        } else if (strcmp(tok, "<") == 0 || strcmp(tok, ">") == 0 || strcmp(tok, "2>") == 0) {
            /* The next token is the file name. */
            char *sym = tok;
            char *target = strtok_r(NULL, " \t", &saveptr);
            if (target == NULL || cur->argc == 0)
                return -1;               /* no file name, or no command yet */
            if (sym[0] == '<')
                cur->in  = target;
            else if (sym[0] == '>') 
                cur->out = target;
            else
                cur->err = target;

        } else if (strcmp(tok, "&") == 0) {
            /* & must be last, must follow a command, and can't be combined with a pipe. */
            if (pl->ncmds != 1 || cur->argc == 0)
                return -1;
            pl->bg = 1;
            if (strtok_r(NULL, " \t", &saveptr) != NULL)
                return -1;               /* something followed & */
            break;

        } else {
            /* The program name or one of its arguments and one slot is kept free for the NULL that ends argv. */
            if (cur->argc >= MAX_ARGS - 1)
                return -1;
            cur->argv[cur->argc++] = tok;
        }
    }

    /* Every command needs a program name, and execvp() needs argv to end with NULL */
    for (i = 0; i < pl->ncmds; i++) {
        if (pl->cmds[i].argc == 0)
            return -1;
        pl->cmds[i].argv[pl->cmds[i].argc] = NULL;
    }

    /* Store command without its '&' and 
    the jobs table adds " &" back only while the job is running. */
    if (pl->bg) {
        char *end = strrchr(pl->raw, '&');   /* & is always the last token */
        *end = '\0';
        while (end > pl->raw && (end[-1] == ' ' || end[-1] == '\t'))
            *--end = '\0';
    }
    return 0;
}
