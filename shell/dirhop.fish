# dirhop — fish integration.
# Source this file from config.fish. The `dho` binary must be on PATH.
#
# `dho -0` ends the path with NUL and `read -z` keeps it whole, embedded
# newlines included. Quitting writes nothing, `read` fails, and the function
# returns without changing directory.

function dho --description "Browse directories and cd into the chosen one"
    switch "$argv[1]"
        case -h --help -V --version --setup
            command dho $argv
            return
    end
    set -l target ""
    command dho -0 $argv | read -z target
    or return
    if test -n "$target"; and test "$target" != "$PWD"
        builtin cd -- "$target"
    end
end
