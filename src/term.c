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

static const char ENTER_SCREEN[] = "\033[?1049h\033[?7l\033[?25l\033[H";
static const char LEAVE_SCREEN[] = "\033[0m\033[?25h\033[?7h\033[?1049l";

static int tty_fd = -1;
static struct termios orig_tios;
static int have_tios;
static volatile sig_atomic_t raw_on;
static volatile sig_atomic_t exit_sig;
static int saw_eof;

static void on_winch(int sig)
{
    (void)sig;
}

static void on_fatal(int sig)
{
    exit_sig = sig;
}

/* Stop restores the terminal before the process actually suspends, then
   rebuilds the screen after SIGCONT. SIGTSTP is blocked inside the handler,
   so it has to be unblocked or raise() only queues the signal. */
static void on_tstp(int sig)
{
    int saved = errno;
    struct sigaction sa;
    struct sigaction old;
    sigset_t set;
    sigset_t oldset;

    (void)sig;
    term_leave();

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTSTP, &sa, &old);

    sigemptyset(&set);
    sigaddset(&set, SIGTSTP);
    sigprocmask(SIG_UNBLOCK, &set, &oldset);
    raise(SIGTSTP);
    sigprocmask(SIG_SETMASK, &oldset, NULL);

    sigaction(SIGTSTP, &old, NULL);
    term_enter();
    errno = saved;
}

static void install_signals(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);

    sa.sa_handler = on_winch;
    sigaction(SIGWINCH, &sa, NULL);

    sa.sa_handler = on_fatal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);

    sa.sa_handler = on_tstp;
    sigaction(SIGTSTP, &sa, NULL);

    /* A shell function captures stdout, so this process is not always the
       foreground job. Ignoring these lets /dev/tty reads and writes proceed
       instead of stopping the process. */
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
}

int term_open(void)
{
    tty_fd = open("/dev/tty", O_RDWR | O_CLOEXEC);
    if (tty_fd < 0) {
        fprintf(stderr, DIRHOP_BIN ": cannot open /dev/tty: %s\n", strerror(errno));
        return -1;
    }
    if (tcgetattr(tty_fd, &orig_tios) != 0) {
        fprintf(stderr, DIRHOP_BIN ": cannot read terminal settings: %s\n",
                strerror(errno));
        close(tty_fd);
        tty_fd = -1;
        return -1;
    }
    have_tios = 1;
    install_signals();
    return 0;
}

void term_enter(void)
{
    struct termios raw;

    if (tty_fd < 0 || !have_tios || raw_on)
        return;
    raw = orig_tios;
    raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(tcflag_t)OPOST;
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(tty_fd, TCSAFLUSH, &raw);
    /* Autowrap is off so a full-width line cannot scroll the screen. */
    write_all(tty_fd, ENTER_SCREEN, sizeof ENTER_SCREEN - 1);
    raw_on = 1;
}

void term_leave(void)
{
    if (tty_fd < 0 || !raw_on)
        return;
    write_all(tty_fd, LEAVE_SCREEN, sizeof LEAVE_SCREEN - 1);
    if (have_tios)
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

static int wait_input(int timeout_ms)
{
    for (;;) {
        struct pollfd pfd;
        int r;

        if (exit_sig || saw_eof)
            return 0;
        pfd.fd = tty_fd;
        pfd.events = POLLIN;
        r = poll(&pfd, 1, timeout_ms);
        if (r < 0) {
            if (errno == EINTR) {
                if (exit_sig)
                    return 0;
                /* A short wait is gathering an escape sequence. Restart it
                   so a resize does not turn an arrow key into Esc. An infinite
                   wait returns so the screen can be drawn at the new size. */
                if (timeout_ms < 0)
                    return 0;
                continue;
            }
            saw_eof = 1;
            return 0;
        }
        if (r == 0)
            return 0;
        if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) &&
            !(pfd.revents & POLLIN)) {
            saw_eof = 1;
            return 0;
        }
        return 1;
    }
}

static int read_byte(char *out)
{
    for (;;) {
        ssize_t n = read(tty_fd, out, 1);
        if (n == 1)
            return 1;
        if (n == 0) {
            saw_eof = 1;
            return 0;
        }
        if (errno == EINTR) {
            if (exit_sig)
                return 0;
            continue;
        }
        saw_eof = 1;
        return 0;
    }
}

static int read_escape(void)
{
    char seq[16];
    int n = 0;

    if (!wait_input(30))
        return KEY_ESC;
    if (!read_byte(&seq[0]))
        return KEY_ESC;
    n = 1;
    if (seq[0] != '[' && seq[0] != 'O')
        return KEY_NONE;

    while (n < (int)sizeof seq) {
        char c;
        if (!wait_input(15))
            break;
        if (!read_byte(&seq[n]))
            break;
        c = seq[n++];
        if (seq[0] == 'O')
            break;
        if ((unsigned char)c >= 0x40)
            break;
    }
    if (n >= 2) {
        char f = seq[n - 1];
        if (f == 'A')
            return KEY_UP;
        if (f == 'B')
            return KEY_DOWN;
        if (f == 'C')
            return KEY_RIGHT;
        if (f == 'D')
            return KEY_LEFT;
    }
    return KEY_NONE;
}

int term_read_key(int *ch)
{
    char c;

    *ch = 0;
    if (exit_sig || saw_eof)
        return KEY_NONE;
    if (!wait_input(-1))
        return KEY_NONE;
    if (!read_byte(&c))
        return KEY_NONE;
    if (c == '\033')
        return read_escape();
    if (c == '\r' || c == '\n')
        return KEY_ENTER;
    if (c == 0x7f || c == 0x08)
        return KEY_BACKSPACE;
    if ((unsigned char)c < 0x20 || (unsigned char)c >= 0x80)
        return KEY_NONE;
    *ch = (unsigned char)c;
    return KEY_CHAR;
}
