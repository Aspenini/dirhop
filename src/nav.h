#ifndef DIRHOP_NAV_H
#define DIRHOP_NAV_H

#include "scan.h"

typedef struct Browser {
    char *path;
    DirList dirs;
    size_t *visible;
    size_t visible_count;
    size_t visible_cap;
    int selected;
    int scroll;
    int show_hidden;
    char status[192];
    int has_status;
} Browser;

void browser_init(Browser *b);
void browser_clear(Browser *b);

/* Rescan b->path. On failure the previous listing is kept and errno is set. */
int browser_rescan(Browser *b);

int browser_descend(Browser *b);
int browser_ascend(Browser *b);
void browser_move(Browser *b, int delta);
void browser_toggle_hidden(Browser *b);
void browser_ensure_visible(Browser *b, int list_rows);

void browser_status(Browser *b, const char *msg);
void browser_status_errno(Browser *b, int err);
void browser_clear_status(Browser *b);

const char *browser_name_at(const Browser *b, size_t visible_index);
char *browser_selected_path(const Browser *b);

#endif
