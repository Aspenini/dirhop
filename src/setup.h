#ifndef DIRHOP_SETUP_H
#define DIRHOP_SETUP_H

#include "config.h"

/* The shell function is what lets dho change directory. When dho runs bare
   on a terminal, the function is not loaded. setup_offer() then asks once
   whether to add it to the shell's startup file. Returns 1 when the line was
   added and dho should exit, or 0 to carry on. */
int setup_offer(void);

/* dho --setup: adds the line without asking. Returns an exit status. */
int setup_run(void);

#endif
