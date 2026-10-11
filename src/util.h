#ifndef DIRHOP_UTIL_H
#define DIRHOP_UTIL_H

#include "config.h"

#include <stddef.h>

typedef struct Buf {
    char *data;
    size_t len;
    size_t cap;
} Buf;

void die_oom(void);
int write_all(int fd, const void *buf, size_t n);

char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

void buf_clear(Buf *b);
void buf_free(Buf *b);
void buf_append(Buf *b, const char *s, size_t n);
void buf_puts(Buf *b, const char *s);
void buf_printf(Buf *b, const char *fmt, ...);

#endif
