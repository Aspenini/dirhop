#include "nav.h"

#include "path.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void browser_init(Browser *b)
{
    memset(b, 0, sizeof *b);
    b->selected = -1;
}

void browser_clear(Browser *b)
{
    if (!b)
        return;
    scan_free(&b->dirs);
    free(b->visible);
    b->visible = NULL;
    b->visible_count = 0;
    b->visible_cap = 0;
    free(b->path);
    b->path = NULL;
    b->selected = -1;
    b->scroll = 0;
    b->page = 0;
    b->show_hidden = 0;
    b->unreadable = 0;
    b->has_status = 0;
    b->status[0] = '\0';
}

void browser_status(Browser *b, const char *msg)
{
    if (!msg || !msg[0])
        msg = "error";
    snprintf(b->status, sizeof b->status, "%s", msg);
    b->has_status = 1;
}

/* The error comes first so a long name cannot push it off a narrow line. */
void browser_status_errno(Browser *b, int err, const char *name)
{
    const char *s = strerror(err);

    if (!s || !s[0])
        s = "error";
    if (name && name[0])
        snprintf(b->status, sizeof b->status, "%s: %s", s, name);
    else
        snprintf(b->status, sizeof b->status, "%s", s);
    b->has_status = 1;
}

void browser_clear_status(Browser *b)
{
    b->has_status = 0;
    b->status[0] = '\0';
}

static void browser_rebuild(Browser *b)
{
    size_t i;

    if (b->dirs.count > b->visible_cap) {
        size_t *p = realloc(b->visible, b->dirs.count * sizeof *p);
        if (!p)
            die_oom();
        b->visible = p;
        b->visible_cap = b->dirs.count;
    }
    b->visible_count = 0;
    for (i = 0; i < b->dirs.count; i++) {
        if (b->dirs.items[i].hidden && !b->show_hidden)
            continue;
        b->visible[b->visible_count++] = i;
    }
    if (b->visible_count == 0)
        b->selected = -1;
    else if (b->selected >= (int)b->visible_count)
        b->selected = (int)b->visible_count - 1;
}

static void browser_reselect(Browser *b, const char *name, int fallback)
{
    size_t i;

    browser_rebuild(b);
    b->scroll = 0;
    if (b->visible_count == 0) {
        b->selected = -1;
        return;
    }
    if (name) {
        for (i = 0; i < b->visible_count; i++) {
            if (strcmp(browser_name_at(b, i), name) == 0) {
                b->selected = (int)i;
                return;
            }
        }
    }
    if (fallback < 0)
        fallback = 0;
    if ((size_t)fallback >= b->visible_count)
        fallback = (int)b->visible_count - 1;
    b->selected = fallback;
}

static int browser_load(Browser *b, const char *path)
{
    DirList fresh;

    memset(&fresh, 0, sizeof fresh);
    if (scan_directories(path, &fresh) != 0)
        return -1;
    scan_free(&b->dirs);
    b->dirs = fresh;
    b->unreadable = 0;
    browser_rebuild(b);
    return 0;
}

int browser_rescan(Browser *b)
{
    if (browser_load(b, b->path) != 0) {
        if (b->dirs.count == 0)
            b->unreadable = 1;
        return -1;
    }
    /* A non-empty listing always has a selection. */
    if (b->selected < 0)
        b->selected = b->visible_count ? 0 : -1;
    return 0;
}

int browser_refresh(Browser *b)
{
    char *saved = NULL;
    int fallback = b->selected;
    int err;

    if (b->selected >= 0 && (size_t)b->selected < b->visible_count)
        saved = xstrdup(browser_name_at(b, (size_t)b->selected));
    if (browser_load(b, b->path) != 0) {
        err = errno;
        free(saved);
        browser_status_errno(b, err, NULL);
        return -1;
    }
    browser_reselect(b, saved, fallback);
    free(saved);
    return 0;
}

const char *browser_name_at(const Browser *b, size_t visible_index)
{
    return b->dirs.items[b->visible[visible_index]].name;
}

char *browser_selected_path(const Browser *b)
{
    if (b->selected < 0 || (size_t)b->selected >= b->visible_count)
        return NULL;
    return path_join(b->path, browser_name_at(b, (size_t)b->selected));
}

void browser_ensure_visible(Browser *b, int list_rows)
{
    int max_scroll = 0;

    if (list_rows < 1)
        list_rows = 1;
    b->page = list_rows;
    if (b->selected < 0 || b->visible_count == 0) {
        b->scroll = 0;
        return;
    }
    if (b->scroll > b->selected)
        b->scroll = b->selected;
    if (b->selected >= b->scroll + list_rows)
        b->scroll = b->selected - list_rows + 1;
    if (b->visible_count > (size_t)list_rows)
        max_scroll = (int)b->visible_count - list_rows;
    if (b->scroll > max_scroll)
        b->scroll = max_scroll;
    if (b->scroll < 0)
        b->scroll = 0;
}

void browser_move(Browser *b, int delta)
{
    long long next;

    if (b->visible_count == 0 || b->selected < 0)
        return;
    next = (long long)b->selected + delta;
    if (next < 0)
        next = 0;
    if ((unsigned long long)next >= b->visible_count)
        next = (long long)b->visible_count - 1;
    b->selected = (int)next;
}

int browser_descend(Browser *b)
{
    char *next;

    if (b->selected < 0 || (size_t)b->selected >= b->visible_count)
        return 0;
    next = browser_selected_path(b);
    if (browser_load(b, next) != 0) {
        browser_status_errno(b, errno, path_base(next));
        free(next);
        return -1;
    }
    free(b->path);
    b->path = next;
    b->selected = b->visible_count ? 0 : -1;
    b->scroll = 0;
    return 0;
}

int browser_ascend(Browser *b)
{
    char *saved;
    char *parent;

    if (!b->path || strcmp(b->path, "/") == 0)
        return 1;
    saved = xstrdup(path_base(b->path));
    parent = path_parent(b->path);
    if (!parent) {
        free(saved);
        return 1;
    }
    if (browser_load(b, parent) != 0) {
        browser_status_errno(b, errno, parent);
        free(parent);
        free(saved);
        return -1;
    }
    free(b->path);
    b->path = parent;
    browser_reselect(b, saved, 0);
    free(saved);
    return 0;
}

void browser_toggle_hidden(Browser *b)
{
    char *saved = NULL;
    int fallback = b->selected;

    if (b->selected >= 0 && (size_t)b->selected < b->visible_count)
        saved = xstrdup(browser_name_at(b, (size_t)b->selected));
    b->show_hidden = !b->show_hidden;
    browser_reselect(b, saved, fallback);
    free(saved);
}
