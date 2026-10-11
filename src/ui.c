#include "ui.h"

#include "term.h"
#include "util.h"

#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct Cell {
    const char *p;
    unsigned char n;
    signed char w;
} Cell;

static Cell *cells;
static size_t cell_cap;
static Buf frame;

static int use_color;
static int utf8_mode;

static const char *ansi(const char *code)
{
    return use_color ? code : "";
}

void ui_init(void)
{
    const char *loc = setlocale(LC_CTYPE, "");
    const char *term;

    if (!loc)
        loc = setlocale(LC_CTYPE, NULL);
    utf8_mode = loc && (strstr(loc, "UTF-8") || strstr(loc, "utf8"));

    term = getenv("TERM");
    use_color = getenv("NO_COLOR") == NULL &&
                !(term && strcmp(term, "dumb") == 0);
}

void ui_shutdown(void)
{
    buf_free(&frame);
    free(cells);
    cells = NULL;
    cell_cap = 0;
}

static int utf8_decode(const char *s, size_t len, uint32_t *cp, size_t *adv)
{
    const unsigned char *u = (const unsigned char *)s;

    if (len == 0)
        return 0;
    if (u[0] < 0x80) {
        *cp = u[0];
        *adv = 1;
        return 1;
    }
    if ((u[0] & 0xE0) == 0xC0) {
        uint32_t v;
        if (len < 2 || (u[1] & 0xC0) != 0x80)
            return 0;
        v = ((uint32_t)(u[0] & 0x1F) << 6) | (u[1] & 0x3F);
        if (v < 0x80)
            return 0;
        *cp = v;
        *adv = 2;
        return 1;
    }
    if ((u[0] & 0xF0) == 0xE0) {
        uint32_t v;
        if (len < 3 || (u[1] & 0xC0) != 0x80 || (u[2] & 0xC0) != 0x80)
            return 0;
        v = ((uint32_t)(u[0] & 0x0F) << 12) | ((uint32_t)(u[1] & 0x3F) << 6) |
            (u[2] & 0x3F);
        if (v < 0x800 || (v >= 0xD800 && v <= 0xDFFF))
            return 0;
        *cp = v;
        *adv = 3;
        return 1;
    }
    if ((u[0] & 0xF8) == 0xF0) {
        uint32_t v;
        if (len < 4 || (u[1] & 0xC0) != 0x80 || (u[2] & 0xC0) != 0x80 ||
            (u[3] & 0xC0) != 0x80)
            return 0;
        v = ((uint32_t)(u[0] & 0x07) << 18) | ((uint32_t)(u[1] & 0x3F) << 12) |
            ((uint32_t)(u[2] & 0x3F) << 6) | (u[3] & 0x3F);
        if (v < 0x10000 || v > 0x10FFFF)
            return 0;
        *cp = v;
        *adv = 4;
        return 1;
    }
    return 0;
}

static int is_wide(uint32_t cp)
{
    if (cp >= 0x1100 && cp <= 0x115F)
        return 1;
    if (cp >= 0x2329 && cp <= 0x232A)
        return 1;
    if (cp >= 0x2E80 && cp <= 0xA4CF)
        return 1;
    if (cp >= 0xAC00 && cp <= 0xD7A3)
        return 1;
    if (cp >= 0xF900 && cp <= 0xFAFF)
        return 1;
    if (cp >= 0xFE10 && cp <= 0xFE19)
        return 1;
    if (cp >= 0xFE30 && cp <= 0xFE6F)
        return 1;
    if (cp >= 0xFF00 && cp <= 0xFF60)
        return 1;
    if (cp >= 0xFFE0 && cp <= 0xFFE6)
        return 1;
    if (cp >= 0x1F300 && cp <= 0x1FAFF)
        return 1;
    if (cp >= 0x20000 && cp <= 0x3FFFD)
        return 1;
    return 0;
}

static int cell_width(uint32_t cp)
{
    if (cp < 32 || cp == 127)
        return -1;
    if (cp >= 0x80 && cp < 0xA0)
        return -1;
    if (cp <= 0x10FFFF) {
        wchar_t wc = (wchar_t)cp;
        if ((uint32_t)wc == cp) {
            int w = wcwidth(wc);
            if (w >= 0)
                return w;
        }
    }
    if (is_wide(cp))
        return 2;
    return 1;
}

static size_t decode_cells(const char *s)
{
    size_t n = 0;
    size_t i = 0;
    size_t len = strlen(s);

    while (i < len) {
        uint32_t cp = 0;
        size_t adv = 1;
        int ok = utf8_decode(s + i, len - i, &cp, &adv);
        int w;

        if (n == cell_cap) {
            size_t cap = cell_cap ? cell_cap * 2 : 64;
            Cell *p = realloc(cells, cap * sizeof *p);
            if (!p)
                die_oom();
            cells = p;
            cell_cap = cap;
        }
        if (!ok) {
            cells[n].p = s + i;
            cells[n].n = 1;
            cells[n].w = -1;
            n++;
            i++;
            continue;
        }
        w = cell_width(cp);
        if (w > 127)
            w = 127;
        if (w < -1)
            w = -1;
        cells[n].p = s + i;
        cells[n].n = (unsigned char)adv;
        cells[n].w = (signed char)w;
        n++;
        i += adv;
    }
    return n;
}

static int cell_cols(const Cell *c)
{
    if (c->w < 0)
        return 1;
    return c->w;
}

static void append_cell(Buf *b, const Cell *c)
{
    if (c->w < 0)
        buf_append(b, "?", 1);
    else
        buf_append(b, c->p, c->n);
}

static const char *ellipsis(void)
{
    return utf8_mode ? "\xe2\x80\xa6" : ".";
}

static int append_fit(Buf *b, const char *s, int max_cols)
{
    size_t n;
    size_t i;
    int total = 0;
    int ell;
    int budget;
    int used = 0;

    if (max_cols <= 0 || !s)
        return 0;
    n = decode_cells(s);
    for (i = 0; i < n; i++)
        total += cell_cols(&cells[i]);
    ell = total > max_cols;
    budget = max_cols - (ell ? 1 : 0);
    if (budget < 0)
        budget = 0;
    for (i = 0; i < n; i++) {
        int w = cell_cols(&cells[i]);
        if (w == 0) {
            if (used > 0)
                append_cell(b, &cells[i]);
            continue;
        }
        if (used + w > budget)
            break;
        append_cell(b, &cells[i]);
        used += w;
    }
    if (ell && used < max_cols) {
        buf_puts(b, ellipsis());
        used++;
    }
    return used;
}

static int append_fit_tail(Buf *b, const char *s, int max_cols)
{
    size_t n;
    size_t i;
    int total = 0;
    int skip;
    int skipped = 0;
    int used = 0;

    if (max_cols <= 0 || !s)
        return 0;
    n = decode_cells(s);
    for (i = 0; i < n; i++)
        total += cell_cols(&cells[i]);
    if (total <= max_cols) {
        for (i = 0; i < n; i++) {
            append_cell(b, &cells[i]);
            used += cell_cols(&cells[i]);
        }
        return used;
    }
    skip = total - (max_cols - 1);
    if (skip < 0)
        skip = 0;
    i = 0;
    while (i < n && skipped < skip) {
        int w = cell_cols(&cells[i]);
        if (w == 0) {
            i++;
            continue;
        }
        skipped += w;
        i++;
        while (i < n && cells[i].w == 0)
            i++;
    }
    if (max_cols >= 1) {
        buf_puts(b, ellipsis());
        used = 1;
    }
    for (; i < n; i++) {
        int w = cell_cols(&cells[i]);
        if (used + w > max_cols)
            break;
        append_cell(b, &cells[i]);
        used += w;
    }
    return used;
}

static char *format_display_path(const char *path)
{
    const char *home = getenv("HOME");
    const char *body = path;
    int tilde = 0;
    size_t bl;
    int add_slash = 1;
    size_t n;
    char *out;
    char *w;

    if (home && home[0] && strcmp(home, "/") != 0) {
        size_t hl = strlen(home);
        while (hl > 1 && home[hl - 1] == '/')
            hl--;
        if (strncmp(path, home, hl) == 0 &&
            (path[hl] == '\0' || path[hl] == '/')) {
            tilde = 1;
            body = path + hl;
        }
    }
    bl = strlen(body);
    if (!tilde && path[0] == '/' && path[1] == '\0')
        add_slash = 0;
    if (bl > 0 && body[bl - 1] == '/')
        add_slash = 0;
    n = (tilde ? 1 : 0) + bl + (add_slash ? 1 : 0);
    out = malloc(n + 1);
    if (!out)
        die_oom();
    w = out;
    if (tilde)
        *w++ = '~';
    memcpy(w, body, bl);
    w += bl;
    if (add_slash)
        *w++ = '/';
    *w = '\0';
    return out;
}

static int row_budget(int row, int rows, int cols)
{
    if (cols <= 0)
        return 0;
    /* The bottom-right cell scrolls some terminals even with autowrap off. */
    if (row >= rows)
        return cols - 1;
    return cols;
}

static void begin_line(Buf *b, int row)
{
    buf_printf(b, "\033[%d;1H\033[K", row);
    if (use_color)
        buf_puts(b, "\033[0m");
}

static const char *rule_glyph(void)
{
    return utf8_mode ? "\xe2\x94\x80" : "-";
}

static void draw_rule(Buf *b, int row, int budget)
{
    int i;
    const char *g;

    begin_line(b, row);
    buf_puts(b, ansi("\033[2m"));
    g = rule_glyph();
    for (i = 0; i < budget; i++)
        buf_puts(b, g);
    buf_puts(b, ansi("\033[0m"));
}

static void draw_header(Buf *b, const Browser *browser, int row, int budget)
{
    char *shown;
    const int prefix_cols = 9;

    begin_line(b, row);
    if (budget <= prefix_cols) {
        buf_puts(b, ansi("\033[1;36m"));
        append_fit(b, DIRHOP_NAME, budget);
        buf_puts(b, ansi("\033[0m"));
        return;
    }
    buf_puts(b, " ");
    buf_puts(b, ansi("\033[1;36m"));
    buf_puts(b, DIRHOP_NAME);
    buf_puts(b, ansi("\033[0m"));
    buf_puts(b, "  ");
    shown = format_display_path(browser->path);
    buf_puts(b, ansi("\033[1m"));
    append_fit_tail(b, shown, budget - prefix_cols);
    buf_puts(b, ansi("\033[0m"));
    free(shown);
}

static void draw_entry(Buf *b, const Browser *browser, int row, int budget,
                       size_t vis_index, int active)
{
    const char *name;
    int name_cols;
    int used;

    begin_line(b, row);
    if (budget <= 0)
        return;
    name = browser_name_at(browser, vis_index);
    if (active)
        buf_puts(b, ansi("\033[7m"));
    if (budget < 3) {
        append_fit(b, active ? ">" : " ", budget);
        buf_puts(b, ansi("\033[0m"));
        return;
    }
    buf_puts(b, active ? "> " : "  ");
    name_cols = append_fit(b, name, budget - 3);
    buf_puts(b, "/");
    used = 2 + name_cols + 1;
    if (active) {
        while (used < budget) {
            buf_puts(b, " ");
            used++;
        }
    }
    buf_puts(b, ansi("\033[0m"));
}

static void draw_message_row(Buf *b, int row, int budget, const char *text)
{
    begin_line(b, row);
    buf_puts(b, ansi("\033[2m"));
    append_fit(b, text, budget);
    buf_puts(b, ansi("\033[0m"));
}

static void draw_status(Buf *b, const Browser *browser, int row, int budget)
{
    char line[sizeof browser->status + 4];

    begin_line(b, row);
    snprintf(line, sizeof line, " ! %s", browser->status);
    buf_puts(b, ansi("\033[1;31m"));
    append_fit(b, line, budget);
    buf_puts(b, ansi("\033[0m"));
}

static const char *footer_move(void)
{
    if (utf8_mode)
        return " \xe2\x86\x91\xe2\x86\x93 Move   Space Open   Enter CD";
    return " UpDn Move  Space Open   Enter CD";
}

static const char *footer_back(int show_hidden)
{
    if (utf8_mode) {
        if (show_hidden)
            return " \xe2\x86\x90 Back     H Hidden*    Q Quit";
        return " \xe2\x86\x90 Back     H Hidden     Q Quit";
    }
    if (show_hidden)
        return " <- Back     H Hidden*    Q Quit";
    return " <- Back     H Hidden     Q Quit";
}

static void draw_footer(Buf *b, int row, int budget, const char *text, int strong)
{
    begin_line(b, row);
    if (strong)
        buf_puts(b, ansi("\033[1m"));
    else
        buf_puts(b, ansi("\033[2m"));
    append_fit(b, text, budget);
    buf_puts(b, ansi("\033[0m"));
}

void ui_draw(Browser *browser)
{
    int rows = 24;
    int cols = 80;
    int show_rules = 0;
    int show_f1 = 0;
    int show_f2 = 0;
    int list_rows;
    int r;
    int i;

    term_winsize(&rows, &cols);
    frame.len = 0;

    if (rows >= 6) {
        show_rules = 1;
        show_f1 = 1;
        show_f2 = 1;
        list_rows = rows - 5;
    } else if (rows == 5) {
        show_rules = 1;
        show_f1 = 1;
        list_rows = 1;
    } else if (rows >= 3) {
        show_f1 = 1;
        list_rows = rows - 2;
    } else if (rows == 2) {
        list_rows = 1;
    } else {
        list_rows = 0;
    }
    if (list_rows < 0)
        list_rows = 0;

    browser_ensure_visible(browser, list_rows > 0 ? list_rows : 1);

    r = 1;
    draw_header(&frame, browser, r, row_budget(r, rows, cols));
    r++;

    if (show_rules) {
        draw_rule(&frame, r, row_budget(r, rows, cols));
        r++;
    }

    for (i = 0; i < list_rows; i++) {
        int rr = r + i;
        int budget = row_budget(rr, rows, cols);
        size_t idx;

        if (browser->scroll < 0)
            idx = (size_t)i;
        else
            idx = (size_t)browser->scroll + (size_t)i;
        if (browser->visible_count == 0 && i == 0) {
            draw_message_row(&frame, rr, budget,
                             browser->has_status ? "  (unreadable)" : "  (empty)");
        } else if (idx < browser->visible_count) {
            draw_entry(&frame, browser, rr, budget, idx,
                       browser->selected == (int)idx);
        } else {
            begin_line(&frame, rr);
        }
    }
    r += list_rows;

    if (show_rules) {
        draw_rule(&frame, r, row_budget(r, rows, cols));
        r++;
    }

    if (show_f1 && r <= rows) {
        if (browser->has_status && !show_f2)
            draw_status(&frame, browser, r, row_budget(r, rows, cols));
        else
            draw_footer(&frame, r, row_budget(r, rows, cols), footer_move(), 0);
        r++;
    }
    if (show_f2 && r <= rows) {
        if (browser->has_status)
            draw_status(&frame, browser, r, row_budget(r, rows, cols));
        else
            draw_footer(&frame, r, row_budget(r, rows, cols),
                        footer_back(browser->show_hidden), browser->show_hidden);
        r++;
    }
    while (r <= rows) {
        begin_line(&frame, r);
        r++;
    }
    buf_puts(&frame, "\033[H");
    if (frame.len)
        term_write(frame.data, frame.len);
}
