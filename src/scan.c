#include "scan.h"

#include "path.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int entry_is_dir(int dfd, const char *path, const char *name)
{
    struct stat st;

    if (dfd >= 0) {
        if (fstatat(dfd, name, &st, 0) != 0)
            return 0;
        return S_ISDIR(st.st_mode);
    }

    {
        char *full = path_join(path, name);
        int ok = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        free(full);
        return ok;
    }
}

static int should_list(int dfd, const char *path, const struct dirent *de)
{
#ifdef DT_UNKNOWN
    unsigned char t = de->d_type;
    if (t == DT_DIR)
        return 1;
    /* Symlinks and filesystems without d_type need stat(). stat() follows
       links, so a symlink to a directory is kept and a symlink to a file
       is not. */
    if (t != DT_UNKNOWN && t != DT_LNK)
        return 0;
#else
    (void)de;
#endif
    return entry_is_dir(dfd, path, de->d_name);
}

static int is_dot_or_dotdot(const char *name)
{
    return name[0] == '.' &&
           (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

static unsigned char fold_ascii(unsigned char c)
{
    if (c >= 'A' && c <= 'Z')
        return (unsigned char)(c + 32);
    return c;
}

static int cmp_names(const char *a, const char *b)
{
    const unsigned char *ua = (const unsigned char *)a;
    const unsigned char *ub = (const unsigned char *)b;

    while (*ua && *ub) {
        unsigned char ca = fold_ascii(*ua);
        unsigned char cb = fold_ascii(*ub);
        if (ca != cb)
            return ca < cb ? -1 : 1;
        ua++;
        ub++;
    }
    if (*ua)
        return 1;
    if (*ub)
        return -1;
    return strcmp(a, b);
}

static int cmp_entry(const void *a, const void *b)
{
    const Entry *ea = a;
    const Entry *eb = b;
    return cmp_names(ea->name, eb->name);
}

static int push_entry(Entry **arr, size_t *n, size_t *cap, const char *name)
{
    size_t len;
    char *copy;

    if (*n == *cap) {
        size_t c = *cap ? *cap * 2 : 32;
        Entry *p = realloc(*arr, c * sizeof *p);
        if (!p)
            return -1;
        *arr = p;
        *cap = c;
    }
    len = strlen(name);
    copy = malloc(len + 1);
    if (!copy)
        return -1;
    memcpy(copy, name, len + 1);
    (*arr)[*n].name = copy;
    (*arr)[*n].hidden = name[0] == '.';
    (*n)++;
    return 0;
}

void scan_free(DirList *list)
{
    size_t i;

    if (!list)
        return;
    if (list->items) {
        for (i = 0; i < list->count; i++)
            free(list->items[i].name);
        free(list->items);
    }
    list->items = NULL;
    list->count = 0;
}

int scan_directories(const char *path, DirList *out)
{
    DIR *dir;
    int dfd;
    Entry *arr = NULL;
    size_t n = 0;
    size_t cap = 0;
    int err;

    dir = opendir(path);
    if (!dir)
        return -1;
    dfd = dirfd(dir);
    for (;;) {
        struct dirent *de;

        errno = 0;
        de = readdir(dir);
        if (!de)
            break;
        if (is_dot_or_dotdot(de->d_name))
            continue;
        if (!should_list(dfd, path, de))
            continue;
        if (push_entry(&arr, &n, &cap, de->d_name) != 0) {
            closedir(dir);
            {
                DirList partial = {arr, n};
                scan_free(&partial);
            }
            die_oom();
        }
    }
    err = errno;
    closedir(dir);
    if (err != 0) {
        DirList partial = {arr, n};
        scan_free(&partial);
        errno = err;
        return -1;
    }
    if (n > 1)
        qsort(arr, n, sizeof *arr, cmp_entry);
    out->items = arr;
    out->count = n;
    return 0;
}
