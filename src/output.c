#include "output.h"

#include "util.h"

#include <string.h>
#include <unistd.h>

int output_path(const char *path)
{
    size_t n = strlen(path);
    int tty = isatty(STDOUT_FILENO);

    if (write_all(STDOUT_FILENO, path, n) != 0)
        return -1;
    if (tty)
        return write_all(STDOUT_FILENO, "\n", 1);
    return write_all(STDOUT_FILENO, "\0", 1);
}
