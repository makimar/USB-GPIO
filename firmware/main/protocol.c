#include "protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int protocol_tokenize(char *line, char *argv[], int max_argv)
{
    int argc = 0;
    char *save = NULL;
    char *tok = strtok_r(line, " \t\r\n", &save);
    while (tok != NULL && argc < max_argv) {
        argv[argc++] = tok;
        tok = strtok_r(NULL, " \t\r\n", &save);
    }
    return argc;
}

void protocol_reply_ok(const char *data)
{
    if (data == NULL) {
        fputs("OK\n", stdout);
    } else {
        printf("OK %s\n", data);
    }
    fflush(stdout);
}

void protocol_reply_ok_fmt(const char *fmt, ...)
{
    fputs("OK ", stdout);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fputs("\n", stdout);
    fflush(stdout);
}

void protocol_reply_err(int code, const char *message)
{
    printf("ERR %d %s\n", code, message);
    fflush(stdout);
}

bool protocol_parse_int(const char *s, long *out)
{
    if (s == NULL || *s == '\0') {
        return false;
    }
    char *end = NULL;
    long val = strtol(s, &end, 10);
    if (end == s || *end != '\0') {
        return false;
    }
    *out = val;
    return true;
}
