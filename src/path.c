#include "path.h"

#include "util.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char *path_getcwd(void)
{
    size_t n = 256;

    for (;;) {
        char *buf = malloc(n);
        int err;

        if (!buf)
            die_oom();
        if (getcwd(buf, n))
            return buf;
        err = errno;
        free(buf);
        if (err != ERANGE) {
            errno = err;
            return NULL;
        }
        if (n > (size_t)-1 / 2) {
            errno = ENOMEM;
            return NULL;
        }
        n *= 2;
    }
}

char *path_join(const char *dir, const char *name)
{
    size_t ld = strlen(dir);
    size_t ln = strlen(name);
    int root = (ld == 1 && dir[0] == '/');
    size_t n = root ? (1 + ln) : (ld + 1 + ln);
    char *p = malloc(n + 1);

    if (!p)
        die_oom();
    if (root) {
        p[0] = '/';
        memcpy(p + 1, name, ln + 1);
    } else {
        memcpy(p, dir, ld);
        p[ld] = '/';
        memcpy(p + ld + 1, name, ln + 1);
    }
    return p;
}

char *path_parent(const char *path)
{
    const char *slash;
    size_t n;
    char *p;

    if (strcmp(path, "/") == 0)
        return NULL;
    slash = strrchr(path, '/');
    if (!slash || slash == path)
        return xstrdup("/");
    n = (size_t)(slash - path);
    p = malloc(n + 1);
    if (!p)
        die_oom();
    memcpy(p, path, n);
    p[n] = '\0';
    return p;
}

const char *path_base(const char *path)
{
    const char *s = strrchr(path, '/');
    if (!s || s[1] == '\0')
        return path;
    return s + 1;
}

char *path_normalize(const char *input)
{
    char *raw;
    char **parts;
    size_t cap = 16;
    size_t n = 0;
    char *s;
    char *out;

    if (!input || !input[0]) {
        errno = EINVAL;
        return NULL;
    }
    if (input[0] == '/') {
        raw = xstrdup(input);
    } else {
        char *cwd = path_getcwd();
        if (!cwd)
            return NULL;
        raw = path_join(cwd, input);
        free(cwd);
    }

    parts = malloc(cap * sizeof *parts);
    if (!parts)
        die_oom();

    s = raw;
    if (*s == '/')
        s++;
    while (*s) {
        char *slash = strchr(s, '/');
        size_t len = slash ? (size_t)(slash - s) : strlen(s);

        if (len == 0 || (len == 1 && s[0] == '.')) {
            /* duplicate slash or "." */
        } else if (len == 2 && s[0] == '.' && s[1] == '.') {
            if (n > 0)
                free(parts[--n]);
        } else {
            char **grown;
            if (n == cap) {
                cap *= 2;
                grown = realloc(parts, cap * sizeof *parts);
                if (!grown)
                    die_oom();
                parts = grown;
            }
            parts[n++] = xstrndup(s, len);
        }
        if (!slash)
            break;
        s = slash + 1;
    }

    if (n == 0) {
        out = xstrdup("/");
    } else {
        size_t total = 0;
        size_t i;
        char *w;

        for (i = 0; i < n; i++)
            total += 1 + strlen(parts[i]);
        out = malloc(total + 1);
        if (!out)
            die_oom();
        w = out;
        for (i = 0; i < n; i++) {
            size_t len = strlen(parts[i]);
            *w++ = '/';
            memcpy(w, parts[i], len);
            w += len;
            free(parts[i]);
        }
        *w = '\0';
    }

    free(parts);
    free(raw);
    return out;
}
