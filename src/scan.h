#ifndef DIRHOP_SCAN_H
#define DIRHOP_SCAN_H

#include "config.h"

#include <stddef.h>

typedef struct Entry {
    char *name;
    int hidden;
} Entry;

typedef struct DirList {
    Entry *items;
    size_t count;
} DirList;

/* Fills list with directories and directory symlinks, sorted
   alphabetically. Hidden names are included and flagged. Returns 0,
   or -1 with errno set. */
int scan_directories(const char *path, DirList *out);
void scan_free(DirList *list);

#endif
