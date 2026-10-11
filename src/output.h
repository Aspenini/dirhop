#ifndef DIRHOP_OUTPUT_H
#define DIRHOP_OUTPUT_H

#include "config.h"

/* Writes the confirmed path to stdout, followed by a newline, or by a NUL
   when nul is set. Returns 0, or -1. */
int output_path(const char *path, int nul);

#endif
