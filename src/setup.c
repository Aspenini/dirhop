#include "setup.h"

#include "path.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { SH_NONE, SH_BASH, SH_ZSH, SH_FISH };

typedef struct Plan {
    int shell;
    char *wrapper; /* absolute path of dirhop.bash or dirhop.fish */
    char *rc;      /* file that gets the source line */
    char *shown;   /* rc with $HOME written as ~ */
} Plan;

static void plan_free(Plan *p)
{
    free(p->wrapper);
    free(p->rc);
    free(p->shown);
}

static int shell_from_name(const char *name)
{
    const char *base = path_base(name);

    if (base[0] == '-')
        base++;
    if (strcmp(base, "bash") == 0)
        return SH_BASH;
    if (strcmp(base, "zsh") == 0)
        return SH_ZSH;
    if (strcmp(base, "fish") == 0)
        return SH_FISH;
    return SH_NONE;
}

/* The parent process is the shell that ran dho, which matters when $SHELL
   names a different login shell. */
static int detect_shell(void)
{
    char proc[64];
    char comm[64];
    const char *env;
    FILE *f;

    snprintf(proc, sizeof proc, "/proc/%ld/comm", (long)getppid());
    f = fopen(proc, "r");
    if (f) {
        int sh = SH_NONE;
        if (fgets(comm, sizeof comm, f)) {
            comm[strcspn(comm, "\n")] = '\0';
            sh = shell_from_name(comm);
        }
        fclose(f);
        if (sh != SH_NONE)
            return sh;
    }
    env = getenv("SHELL");
    return env && env[0] ? shell_from_name(env) : SH_NONE;
}

static int is_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static char *home_join(const char *rel)
{
    const char *home = getenv("HOME");

    if (!home || home[0] != '/')
        return NULL;
    return path_join(home, rel);
}

/* Looks beside the binary first (prefix/bin/dho, prefix/share/dirhop),
   then in the usual install prefixes. */
static char *find_wrapper(const char *file)
{
    static const char *const prefixes[] = {"/usr/local", "/usr"};
    char exe[PATH_MAX];
    char *dir;
    char *cand;
    size_t i;

    if (realpath("/proc/self/exe", exe)) {
        char *bin = path_parent(exe);
        if (bin) {
            char *prefix = path_parent(bin);
            if (prefix) {
                dir = path_join(prefix, "share/dirhop");
                cand = path_join(dir, file);
                free(dir);
                free(prefix);
                free(bin);
                if (is_file(cand))
                    return cand;
                free(cand);
            } else {
                free(bin);
            }
        }
    }
    dir = home_join(".local/share/dirhop");
    if (dir) {
        cand = path_join(dir, file);
        free(dir);
        if (is_file(cand))
            return cand;
        free(cand);
    }
    for (i = 0; i < sizeof prefixes / sizeof prefixes[0]; i++) {
        dir = path_join(prefixes[i], "share/dirhop");
        cand = path_join(dir, file);
        free(dir);
        if (is_file(cand))
            return cand;
        free(cand);
    }
    return NULL;
}

static char *env_dir(const char *var, const char *fallback)
{
    const char *v = getenv(var);

    if (v && v[0] == '/')
        return xstrdup(v);
    return home_join(fallback);
}

static char *tilde(const char *path)
{
    const char *home = getenv("HOME");
    size_t hl;
    char *out;

    if (!home || home[0] != '/' || strcmp(home, "/") == 0)
        return xstrdup(path);
    hl = strlen(home);
    if (strncmp(path, home, hl) != 0 || path[hl] != '/')
        return xstrdup(path);
    out = malloc(1 + strlen(path + hl) + 1);
    if (!out)
        die_oom();
    out[0] = '~';
    strcpy(out + 1, path + hl);
    return out;
}

static int make_plan(Plan *p)
{
    char *dir;

    memset(p, 0, sizeof *p);
    p->shell = detect_shell();
    switch (p->shell) {
    case SH_BASH:
        p->wrapper = find_wrapper("dirhop.bash");
        p->rc = home_join(".bashrc");
        break;
    case SH_ZSH:
        p->wrapper = find_wrapper("dirhop.bash");
        dir = getenv("ZDOTDIR") && getenv("ZDOTDIR")[0] == '/'
                  ? xstrdup(getenv("ZDOTDIR"))
                  : NULL;
        p->rc = dir ? path_join(dir, ".zshrc") : home_join(".zshrc");
        free(dir);
        break;
    case SH_FISH:
        /* conf.d is read by every fish, so no existing file is edited. */
        p->wrapper = find_wrapper("dirhop.fish");
        dir = env_dir("XDG_CONFIG_HOME", ".config");
        if (dir) {
            p->rc = path_join(dir, "fish/conf.d/dirhop.fish");
            free(dir);
        }
        break;
    default:
        return -1;
    }
    if (!p->wrapper || !p->rc || strchr(p->wrapper, '\'')) {
        plan_free(p);
        return -1;
    }
    p->shown = tilde(p->rc);
    return 0;
}

/* True when the file already names the wrapper, so it is never added twice. */
static int rc_has_line(const Plan *p)
{
    const char *needle = path_base(p->wrapper);
    char line[4096];
    int found = 0;
    FILE *f = fopen(p->rc, "r");

    if (!f)
        return 0;
    while (!found && fgets(line, sizeof line, f))
        found = strstr(line, needle) != NULL;
    fclose(f);
    return found;
}

static int mkdir_p(char *path)
{
    char *s;

    for (s = path + 1; *s; s++) {
        if (*s != '/')
            continue;
        *s = '\0';
        if (mkdir(path, 0755) != 0 && errno != EEXIST) {
            *s = '/';
            return -1;
        }
        *s = '/';
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int mkdir_parent(const char *file)
{
    char *dir = path_parent(file);
    int r = dir ? mkdir_p(dir) : 0;

    free(dir);
    return r;
}

static int append_line(const Plan *p)
{
    Buf b = {0};
    int fd;
    int r;

    struct stat st;

    /* Guarded, so uninstalling dirhop leaves a shell that still starts. */
    if (stat(p->rc, &st) == 0 && st.st_size > 0)
        buf_puts(&b, "\n");
    buf_puts(&b, "# dirhop: lets `dho` change directory\n");
    if (p->shell == SH_FISH)
        buf_printf(&b, "test -r '%s'; and source '%s'\n", p->wrapper, p->wrapper);
    else
        buf_printf(&b, "[ -r '%s' ] && . '%s'\n", p->wrapper, p->wrapper);

    if (mkdir_parent(p->rc) != 0) {
        buf_free(&b);
        return -1;
    }
    fd = open(p->rc, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        buf_free(&b);
        return -1;
    }
    r = write_all(fd, b.data, b.len);
    if (close(fd) != 0)
        r = -1;
    buf_free(&b);
    return r;
}

static char *declined_path(void)
{
    char *dir = env_dir("XDG_STATE_HOME", ".local/state");
    char *p;

    if (!dir)
        return NULL;
    p = path_join(dir, "dirhop/setup-declined");
    free(dir);
    return p;
}

static int declined(void)
{
    char *p = declined_path();
    int r = p && is_file(p);

    free(p);
    return r;
}

static void remember_declined(void)
{
    char *p = declined_path();
    int fd;

    if (!p)
        return;
    if (mkdir_parent(p) == 0) {
        fd = open(p, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
        if (fd >= 0)
            close(fd);
    }
    free(p);
}

static void reload_hint(const Plan *p)
{
    fprintf(stderr, "Open a new terminal, or run:  source %s\n", p->shown);
}

static int install(const Plan *p)
{
    if (append_line(p) != 0) {
        fprintf(stderr, DIRHOP_BIN ": cannot write %s: %s\n", p->shown,
                strerror(errno));
        return 1;
    }
    fprintf(stderr, "Added to %s. ", p->shown);
    reload_hint(p);
    return 0;
}

static int ask_yes(void)
{
    char answer[32];
    char *s;

    if (!fgets(answer, sizeof answer, stdin))
        return 0;
    for (s = answer; *s == ' ' || *s == '\t'; s++)
        ;
    return *s == '\n' || *s == '\0' || *s == 'y' || *s == 'Y';
}

int setup_offer(void)
{
    Plan p;
    int done = 0;

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) || !isatty(STDERR_FILENO))
        return 0;
    if (declined() || make_plan(&p) != 0)
        return 0;
    if (rc_has_line(&p)) {
        fprintf(stderr, DIRHOP_BIN ": the shell function is set up in %s but this "
                        "shell has not loaded it, so dho cannot change directory.\n",
                p.shown);
        reload_hint(&p);
        plan_free(&p);
        return 0;
    }
    fprintf(stderr, DIRHOP_BIN " needs a small shell function to change directory. "
                    "Without it, dho only prints the path.\n"
                    "Add it to %s? [Y/n] ",
            p.shown);
    fflush(stderr);
    if (ask_yes()) {
        done = install(&p) == 0;
    } else {
        remember_declined();
        fputs("Not asking again. Run `dho --setup` to add it later.\n", stderr);
    }
    plan_free(&p);
    return done;
}

int setup_run(void)
{
    Plan p;
    int r;

    if (make_plan(&p) != 0) {
        fputs(DIRHOP_BIN ": cannot set up this shell automatically. Source "
                         "dirhop.bash (bash, zsh) or dirhop.fish (fish) from "
                         "your shell startup file.\n",
              stderr);
        return 1;
    }
    if (rc_has_line(&p)) {
        fprintf(stderr, "Already set up in %s. ", p.shown);
        reload_hint(&p);
        plan_free(&p);
        return 0;
    }
    r = install(&p);
    plan_free(&p);
    return r;
}
