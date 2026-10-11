#include "output.h"

#include "util.h"

#include <string.h>
#include <unistd.h>

int output_path(const char *path, int nul)
{
    size_t n = strlen(path);

    if (write_all(STDOUT_FILENO, path, n) != 0)
        return -1;
    return write_all(STDOUT_FILENO, nul ? "\0" : "\n", 1);
}
