// Line protocol helpers: tokenizing, and the `OK [data]` / `ERR <code>
// <message>` reply framing described in CLAUDE.md and docs/protocol.md.
#pragma once

#include <stdarg.h>
#include <stdbool.h>

// Error codes. 1 and 2 are fixed by the protocol spec (docs/protocol.md);
// the rest are firmware-specific but must stay documented there.
#define PROTO_ERR_UNKNOWN_COMMAND 1
#define PROTO_ERR_PIN_RESERVED 2
#define PROTO_ERR_BAD_ARGS 3
#define PROTO_ERR_INVALID_PIN 4
#define PROTO_ERR_INVALID_MODE 5
#define PROTO_ERR_NO_PWM_CHANNEL 6
#define PROTO_ERR_PWM_NOT_ACTIVE 7
#define PROTO_ERR_ADC_UNAVAILABLE 8

#define PROTO_MAX_ARGS 8

// Splits `line` in place on whitespace. Returns the number of tokens found
// (0 for a blank line), capped at PROTO_MAX_ARGS. `argv[i]` point into `line`.
int protocol_tokenize(char *line, char *argv[], int max_argv);

// Writes "OK\n" (data == NULL) or "OK <data>\n".
void protocol_reply_ok(const char *data);

// Writes "OK <fmt-expansion>\n".
void protocol_reply_ok_fmt(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Writes "ERR <code> <message>\n".
void protocol_reply_err(int code, const char *message);

// Parses `s` as a base-10 integer. Rejects empty strings, trailing garbage,
// and leading/trailing whitespace (strtok already stripped whitespace
// around tokens, so this mainly guards against e.g. "4x"). Returns false on
// any parse failure; `*out` is left untouched.
bool protocol_parse_int(const char *s, long *out);
