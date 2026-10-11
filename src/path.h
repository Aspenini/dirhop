#ifndef DIRHOP_PATH_H
#define DIRHOP_PATH_H

#include "config.h"

/* Logical paths. ".." drops a component and does not resolve symlinks. */

char *path_getcwd(void);
char *path_join(const char *dir, const char *name);
char *path_parent(const char *path);
const char *path_base(const char *path);
char *path_normalize(const char *input);

#endif
