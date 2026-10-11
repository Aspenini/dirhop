# dirhop — fish integration.
# Source this file from config.fish. The `dho` binary must be on PATH.
#
# `read -z` keeps the NUL-terminated path, including embedded newlines.
# Quitting produces no output, `read` fails, and the function returns
# without changing directory.

function dho --description "Interactive directory change"
    set -l target ""
    command dho "$PWD" </dev/tty | read -z target
    or return
    if test -n "$target"
        builtin cd -- "$target"
    end
end
