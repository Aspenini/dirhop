#ifndef DIRHOP_OUTPUT_H
#define DIRHOP_OUTPUT_H

#include "config.h"

/* Writes the confirmed path to stdout. A terminal gets a trailing newline.
   A pipe gets a trailing NUL so a name can contain newlines. Returns 0, or -1. */
int output_path(const char *path);

#endif
