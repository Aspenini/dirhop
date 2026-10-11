#include "config.h"

#include "nav.h"
#include "output.h"
#include "path.h"
#include "term.h"
#include "ui.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* dirhop — minimal TUI directory navigator.
   The interface and keyboard stay on /dev/tty. Enter writes a directory's
   absolute path to stdout and exits 0. Quit writes nothing and exits
   non-zero. Paths are logical: directory symlinks are not resolved. */

static Browser app;
static int want_quit;
static int quit_code = 1;
static int print0;

static void cleanup(void)
{
    term_leave();
    browser_clear(&app);
    ui_shutdown();
}

static void usage(FILE *f)
{
    fprintf(f,
            "usage: " DIRHOP_BIN " [-0] [directory]\n"
            "\n"
            "Browse directories on the terminal and print the chosen one.\n"
            "Source the shell function so that Enter changes directory.\n"
            "\n"
            "  -0, --print0   end the path with NUL instead of a newline\n"
            "  -h, --help     show this help\n"
            "  -V, --version  show the version\n");
}

static void confirm_path(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0) {
        browser_status_errno(&app, errno, path_base(path));
        return;
    }
    if (!S_ISDIR(st.st_mode)) {
        browser_status(&app, "not a directory");
        return;
    }
    /* Leave the alternate screen before writing the path, in case stdout is
       the terminal: restoring the main screen would wipe it. */
    term_leave();
    exit(output_path(path, print0) == 0 ? 0 : 1);
}

/* Enter takes the highlighted directory. With nothing to highlight it takes
   the directory on screen, so an empty folder can still be chosen. */
static void confirm_selected(void)
{
    char *path = browser_selected_path(&app);

    if (!path) {
        confirm_path(app.path);
        return;
    }
    confirm_path(path);
    free(path);
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
    case KEY_PGUP:
        browser_move(&app, -app.page);
        break;
    case KEY_PGDN:
        browser_move(&app, app.page);
        break;
    case KEY_HOME:
        browser_move(&app, INT_MIN);
        break;
    case KEY_END:
        browser_move(&app, INT_MAX);
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
    case KEY_CTRL_C:
        quit_code = 130;
        want_quit = 1;
        break;
    case KEY_CTRL_L:
        browser_refresh(&app);
        break;
    case KEY_CHAR:
        switch (ch) {
        case 'q':
        case 'Q':
            want_quit = 1;
            break;
        case 'j':
        case 'J':
            browser_move(&app, 1);
            break;
        case 'k':
        case 'K':
            browser_move(&app, -1);
            break;
        case 'g':
            browser_move(&app, INT_MIN);
            break;
        case 'G':
            browser_move(&app, INT_MAX);
            break;
        case 'h':
        case 'H':
            browser_toggle_hidden(&app);
            break;
        case ' ':
            browser_descend(&app);
            break;
        case '.':
            confirm_path(app.path);
            break;
        }
        break;
    default:
        break;
    }
}

int main(int argc, char **argv)
{
    const char *arg = NULL;
    int opts_done = 0;
    struct stat st;
    int i;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (!opts_done && a[0] == '-' && a[1] != '\0') {
            if (strcmp(a, "--") == 0) {
                opts_done = 1;
            } else if (strcmp(a, "-0") == 0 || strcmp(a, "--print0") == 0) {
                print0 = 1;
            } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
                usage(stdout);
                return 0;
            } else if (strcmp(a, "-V") == 0 || strcmp(a, "--version") == 0) {
                puts(DIRHOP_NAME " " DIRHOP_VERSION);
                return 0;
            } else {
                fprintf(stderr, DIRHOP_BIN ": unknown option: %s\n", a);
                usage(stderr);
                return 2;
            }
            continue;
        }
        if (arg) {
            usage(stderr);
            return 2;
        }
        arg = a;
    }

    browser_init(&app);
    atexit(cleanup);
    ui_init();

    app.path = arg ? path_normalize(arg) : path_getcwd();
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
        browser_status_errno(&app, errno, NULL);

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
    return quit_code;
}
