#include "config.h"

#include "nav.h"
#include "output.h"
#include "path.h"
#include "term.h"
#include "ui.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* dirhop — minimal TUI directory navigator.
   The interface and keyboard stay on /dev/tty. Enter writes the highlighted
   directory's absolute path to stdout and exits 0. Quit writes nothing and
   exits non-zero. Paths are logical: directory symlinks are not resolved. */

static Browser app;
static int want_quit;

static void cleanup(void)
{
    term_leave();
    browser_clear(&app);
    ui_shutdown();
}

static void confirm_selected(void)
{
    char *path;
    struct stat st;

    path = browser_selected_path(&app);
    if (!path) {
        browser_status(&app, "empty directory");
        return;
    }
    if (stat(path, &st) != 0) {
        browser_status_errno(&app, errno);
        free(path);
        return;
    }
    if (!S_ISDIR(st.st_mode)) {
        browser_status(&app, "not a directory");
        free(path);
        return;
    }
    /* Leave the alternate screen before writing the path. Otherwise the
       path is wiped when the main screen is restored. */
    term_leave();
    if (output_path(path) != 0) {
        free(path);
        exit(1);
    }
    free(path);
    exit(0);
}

static void dispatch(int key, int ch)
{
    switch (key) {
    case KEY_UP:
        browser_move(&app, -1);
        break;
    case KEY_DOWN:
        browser_move(&app, 1);
        break;
    case KEY_LEFT:
    case KEY_BACKSPACE:
        browser_ascend(&app);
        break;
    case KEY_RIGHT:
        browser_descend(&app);
        break;
    case KEY_ENTER:
        confirm_selected();
        break;
    case KEY_ESC:
        want_quit = 1;
        break;
    case KEY_CHAR:
        if (ch == 'q' || ch == 'Q')
            want_quit = 1;
        else if (ch == 'j' || ch == 'J')
            browser_move(&app, 1);
        else if (ch == 'k' || ch == 'K')
            browser_move(&app, -1);
        else if (ch == 'h' || ch == 'H')
            browser_toggle_hidden(&app);
        else if (ch == ' ')
            browser_descend(&app);
        break;
    default:
        break;
    }
}

int main(int argc, char **argv)
{
    const char *arg;
    struct stat st;

    if (argc > 2) {
        fprintf(stderr, "usage: " DIRHOP_BIN " [directory]\n");
        return 2;
    }

    browser_init(&app);
    atexit(cleanup);
    ui_init();

    arg = (argc == 2) ? argv[1] : NULL;
    if (arg)
        app.path = path_normalize(arg);
    else
        app.path = path_getcwd();
    if (!app.path) {
        fprintf(stderr, DIRHOP_BIN ": %s\n", strerror(errno));
        return 1;
    }

    if (stat(app.path, &st) != 0) {
        fprintf(stderr, DIRHOP_BIN ": %s: %s\n", app.path, strerror(errno));
        return 1;
    }
    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, DIRHOP_BIN ": not a directory: %s\n", app.path);
        return 1;
    }

    if (browser_rescan(&app) != 0)
        browser_status_errno(&app, errno);

    if (term_open() != 0)
        return 1;
    term_enter();

    while (!term_interrupted() && !want_quit) {
        int ch = 0;
        int key;

        ui_draw(&app);
        key = term_read_key(&ch);
        if (term_interrupted())
            break;
        if (key == KEY_NONE)
            continue;
        browser_clear_status(&app);
        dispatch(key, ch);
    }

    if (term_fatal_signal()) {
        int sig = term_fatal_signal();
        cleanup();
        signal(sig, SIG_DFL);
        raise(sig);
        return 128 + sig;
    }
    return 1;
}
