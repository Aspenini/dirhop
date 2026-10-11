#include "term.h"

#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* Autowrap is off so a full-width line cannot scroll the screen. */
static const char ENTER_SCREEN[] = "\033[?1049h\033[?7l\033[?25l\033[H\033[2J";
static const char LEAVE_SCREEN[] = "\033[0m\033[?25h\033[?7h\033[?1049l";

/* How long the bytes after Esc may trail it and still form one key. Arrow
   keys arrive as Esc [ A; over ssh the tail can lag the Esc slightly, and
   reading it as a bare Esc would quit. */
#define ESC_WAIT_MS 50

static int tty_fd = -1;
static int wake_fd[2] = {-1, -1};
static struct termios orig_tios;
static int raw_on;
static int saw_eof;
static volatile sig_atomic_t exit_sig;
static volatile sig_atomic_t got_cont;

static unsigned char inbuf[64];
static size_t in_pos;
static size_t in_len;

/* Signal handlers only set a flag and write one byte to a pipe. poll()
   watches the pipe beside the terminal, so a resize that lands between two
   poll() calls is never lost. */
static void wake(void)
{
    int saved = errno;
    ssize_t r;

    if (wake_fd[1] >= 0) {
        r = write(wake_fd[1], "", 1);
        (void)r;
    }
    errno = saved;
}

static void on_winch(int sig)
{
    (void)sig;
    wake();
}

static void on_cont(int sig)
{
    (void)sig;
    got_cont = 1;
    wake();
}

static void on_fatal(int sig)
{
    exit_sig = sig;
    wake();
}

static int set_flags(int fd)
{
    int fl = fcntl(fd, F_GETFL);
    int fdfl = fcntl(fd, F_GETFD);

    if (fl < 0 || fdfl < 0)
        return -1;
    if (fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0)
        return -1;
    return fcntl(fd, F_SETFD, fdfl | FD_CLOEXEC);
}

static int install_signals(void)
{
    struct sigaction sa;

    if (pipe(wake_fd) != 0)
        return -1;
    if (set_flags(wake_fd[0]) != 0 || set_flags(wake_fd[1]) != 0)
        return -1;

    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sa.sa_handler = on_winch;
    sigaction(SIGWINCH, &sa, NULL);
    sa.sa_handler = on_cont;
    sigaction(SIGCONT, &sa, NULL);

    sa.sa_handler = on_fatal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);

    /* The shell waits on this process while it captures stdout. A stopped
       dho could not be resumed from that shell, so suspend is refused. Raw
       mode turns off ISIG, so Ctrl-C and Ctrl-Z arrive as bytes anyway;
       these cover signals sent from elsewhere. */
    sa.sa_handler = SIG_IGN;
    sigaction(SIGTSTP, &sa, NULL);
    sigaction(SIGTTIN, &sa, NULL);
    sigaction(SIGTTOU, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);
    return 0;
}

int term_open(void)
{
    pid_t fg;

    tty_fd = open("/dev/tty", O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (tty_fd < 0) {
        fprintf(stderr, DIRHOP_BIN ": cannot open /dev/tty: %s\n", strerror(errno));
        return -1;
    }
    if (tcgetattr(tty_fd, &orig_tios) != 0) {
        fprintf(stderr, DIRHOP_BIN ": cannot read terminal settings: %s\n",
                strerror(errno));
        goto fail;
    }
    /* A background process cannot read the terminal. Its reads fail or stop
       it, which looks like dho quitting at once or hanging. Say why. */
    fg = tcgetpgrp(tty_fd);
    if (fg != -1 && fg != getpgrp()) {
        fprintf(stderr, DIRHOP_BIN ": not in the terminal's foreground job; "
                        "run it directly or through the shell function\n");
        goto fail;
    }
    if (install_signals() != 0) {
        fprintf(stderr, DIRHOP_BIN ": %s\n", strerror(errno));
        goto fail;
    }
    return 0;

fail:
    close(tty_fd);
    tty_fd = -1;
    return -1;
}

static void apply_raw(void)
{
    struct termios raw = orig_tios;

    raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(tcflag_t)OPOST;
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(tty_fd, TCSANOW, &raw);
    write_all(tty_fd, ENTER_SCREEN, sizeof ENTER_SCREEN - 1);
}

void term_enter(void)
{
    if (tty_fd < 0 || raw_on)
        return;
    apply_raw();
    raw_on = 1;
}

void term_leave(void)
{
    if (tty_fd < 0 || !raw_on)
        return;
    write_all(tty_fd, LEAVE_SCREEN, sizeof LEAVE_SCREEN - 1);
    tcsetattr(tty_fd, TCSADRAIN, &orig_tios);
    raw_on = 0;
}

void term_winsize(int *rows, int *cols)
{
    struct winsize ws;

    *rows = 24;
    *cols = 80;
    if (tty_fd >= 0 && ioctl(tty_fd, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_row > 0)
            *rows = ws.ws_row;
        if (ws.ws_col > 0)
            *cols = ws.ws_col;
    }
}

int term_write(const void *buf, size_t n)
{
    if (tty_fd < 0 || n == 0)
        return 0;
    return write_all(tty_fd, buf, n);
}

int term_interrupted(void)
{
    return exit_sig || saw_eof;
}

int term_fatal_signal(void)
{
    return (int)exit_sig;
}

static void drain_wake(void)
{
    char sink[32];

    while (read(wake_fd[0], sink, sizeof sink) > 0)
        ;
    /* After SIGSTOP and SIGCONT the shell may have reset the terminal. */
    if (got_cont) {
        got_cont = 0;
        if (raw_on)
            apply_raw();
    }
}

/* Returns 1 when the terminal has input, 0 on timeout, and -1 when a
   signal or a hangup should end the wait. An untimed wait also returns -1
   on a resize so the screen is redrawn. A timed wait is gathering the rest
   of an escape sequence, so it keeps going and the redraw follows the key. */
static int wait_input(int timeout_ms)
{
    for (;;) {
        struct pollfd p[2];
        int woke = 0;
        int r;

        if (exit_sig || saw_eof)
            return -1;
        p[0].fd = tty_fd;
        p[0].events = POLLIN;
        p[0].revents = 0;
        p[1].fd = wake_fd[0];
        p[1].events = POLLIN;
        p[1].revents = 0;
        r = poll(p, 2, timeout_ms);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            saw_eof = 1;
            return -1;
        }
        if (r == 0)
            return 0;
        if (p[1].revents) {
            drain_wake();
            woke = 1;
        }
        if (exit_sig)
            return -1;
        if (p[0].revents & POLLIN)
            return 1;
        if (p[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            saw_eof = 1;
            return -1;
        }
        if (woke && timeout_ms < 0)
            return -1;
    }
}

/* Returns 1 with a byte, 0 on timeout, or -1 as wait_input() does. */
static int next_byte(int timeout_ms, unsigned char *out)
{
    if (in_pos == in_len) {
        ssize_t n;
        int r = wait_input(timeout_ms);

        if (r <= 0)
            return r;
        do
            n = read(tty_fd, inbuf, sizeof inbuf);
        while (n < 0 && errno == EINTR);
        if (n <= 0) {
            saw_eof = 1;
            return -1;
        }
        in_pos = 0;
        in_len = (size_t)n;
    }
    *out = inbuf[in_pos++];
    return 1;
}

static int map_final(unsigned char f, int param)
{
    switch (f) {
    case 'A':
        return KEY_UP;
    case 'B':
        return KEY_DOWN;
    case 'C':
        return KEY_RIGHT;
    case 'D':
        return KEY_LEFT;
    case 'H':
        return KEY_HOME;
    case 'F':
        return KEY_END;
    case '~':
        switch (param) {
        case 1:
        case 7:
            return KEY_HOME;
        case 4:
        case 8:
            return KEY_END;
        case 5:
            return KEY_PGUP;
        case 6:
            return KEY_PGDN;
        }
        break;
    }
    return KEY_NONE;
}

/* Called after Esc. Recognises CSI (Esc [) and SS3 (Esc O) keys, with or
   without modifier parameters. Anything else after Esc is an Alt chord and
   is ignored. */
static int read_escape(void)
{
    unsigned char c;
    int param = 0;
    int in_first = 1;
    int i;
    int r;

    r = next_byte(ESC_WAIT_MS, &c);
    if (r == 0)
        return KEY_ESC;
    if (r < 0)
        return KEY_NONE;
    if (c == '\033')
        return KEY_ESC;
    if (c == 'O') {
        if (next_byte(ESC_WAIT_MS, &c) <= 0)
            return KEY_NONE;
        return map_final(c, 0);
    }
    if (c != '[')
        return KEY_NONE;

    for (i = 0; i < 32; i++) {
        if (next_byte(ESC_WAIT_MS, &c) <= 0)
            return KEY_NONE;
        if (c >= '0' && c <= '9') {
            if (in_first && param < 1000)
                param = param * 10 + (c - '0');
        } else if (c == ';') {
            in_first = 0;
        } else if (c >= 0x40 && c <= 0x7e) {
            return map_final(c, param);
        } else if (c < 0x20 || c > 0x3f) {
            return KEY_NONE;
        }
    }
    return KEY_NONE;
}

int term_read_key(int *ch)
{
    unsigned char c;

    *ch = 0;
    if (next_byte(-1, &c) <= 0)
        return KEY_NONE;
    switch (c) {
    case '\033':
        return read_escape();
    case '\r':
    case '\n':
        return KEY_ENTER;
    case 0x7f:
    case 0x08:
        return KEY_BACKSPACE;
    case 0x03:
        return KEY_CTRL_C;
    case 0x0c:
        return KEY_CTRL_L;
    case 0x0e:
        return KEY_DOWN;
    case 0x10:
        return KEY_UP;
    }
    if (c < 0x20 || c >= 0x7f)
        return KEY_NONE;
    *ch = c;
    return KEY_CHAR;
}
