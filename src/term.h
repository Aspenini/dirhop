#ifndef DIRHOP_TERM_H
#define DIRHOP_TERM_H

#include "config.h"

#include <stddef.h>

enum {
    KEY_NONE = 0,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_PGUP,
    KEY_PGDN,
    KEY_ENTER,
    KEY_BACKSPACE,
    KEY_ESC,
    KEY_CTRL_C,
    KEY_CTRL_L,
    KEY_CHAR
};

/* Opens /dev/tty and installs the signal handlers. Returns 0, or -1 after
   printing the reason to stderr. */
int term_open(void);
void term_enter(void);
void term_leave(void);

void term_winsize(int *rows, int *cols);
int term_write(const void *buf, size_t n);

/* Blocks until a key, a resize, a resume, or a fatal signal. KEY_NONE means
   redraw and leaves any status line alone. */
int term_read_key(int *ch);

int term_interrupted(void);
int term_fatal_signal(void);

#endif
