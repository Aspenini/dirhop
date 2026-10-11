#include "util.h"

#include "term.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void die_oom(void)
{
    term_leave();
    fprintf(stderr, DIRHOP_BIN ": out of memory\n");
    fflush(stderr);
    _exit(1);
}

int write_all(int fd, const void *buf, size_t n)
{
    const char *p = buf;

    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

char *xstrndup(const char *s, size_t n)
{
    char *p = malloc(n + 1);
    if (!p)
        die_oom();
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *xstrdup(const char *s)
{
    return xstrndup(s, strlen(s));
}

void buf_clear(Buf *b)
{
    b->len = 0;
    if (b->data)
        b->data[0] = '\0';
}

void buf_free(Buf *b)
{
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void buf_reserve(Buf *b, size_t n)
{
    size_t need;
    size_t cap;
    char *p;

    if (n > (size_t)-1 - b->len - 1)
        die_oom();
    need = b->len + n + 1;
    if (need <= b->cap)
        return;
    cap = b->cap ? b->cap : 256;
    while (cap < need) {
        if (cap > (size_t)-1 / 2)
            die_oom();
        cap *= 2;
    }
    p = realloc(b->data, cap);
    if (!p)
        die_oom();
    b->data = p;
    b->cap = cap;
}

void buf_append(Buf *b, const char *s, size_t n)
{
    buf_reserve(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void buf_puts(Buf *b, const char *s)
{
    buf_append(b, s, strlen(s));
}

void buf_printf(Buf *b, const char *fmt, ...)
{
    va_list ap;
    char tmp[128];
    int n;

    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n < sizeof tmp) {
        buf_append(b, tmp, (size_t)n);
        return;
    }

    {
        char *big = malloc((size_t)n + 1);
        if (!big)
            die_oom();
        va_start(ap, fmt);
        vsnprintf(big, (size_t)n + 1, fmt, ap);
        va_end(ap);
        buf_append(b, big, (size_t)n);
        free(big);
    }
}
