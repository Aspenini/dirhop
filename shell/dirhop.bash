# dirhop — bash and zsh integration.
# Source this file. The `dho` binary must be on PATH.
#
# The binary writes a NUL-terminated absolute path. That keeps directory
# names which contain newlines intact. Quitting writes nothing; the read
# fails and the function returns non-zero without calling cd.

if [ -n "${BASH_VERSION-}" ] && [ "${BASH_SOURCE[0]-}" = "$0" ]; then
    echo "dho: source this file from bash or zsh" >&2
    exit 1
fi

if [ -n "${ZSH_VERSION-}" ]; then
    case ${ZSH_EVAL_CONTEXT-} in
        toplevel)
            echo "dho: source this file from bash or zsh" >&2
            exit 1
            ;;
    esac
fi

if [ -n "${ZSH_VERSION-}" ]; then
    dho() {
        emulate -L zsh
        local target
        if ! IFS= read -r -d '' target < <(command dho "$PWD" </dev/tty); then
            return 1
        fi
        if [ -n "$target" ]; then
            builtin cd -- "$target" || return
        fi
    }
elif [ -n "${BASH_VERSION-}" ]; then
    dho() {
        local target
        if ! IFS= read -r -d '' target < <(command dho "$PWD" </dev/tty); then
            return 1
        fi
        if [ -n "$target" ]; then
            builtin cd -- "$target" || return
        fi
    }
else
    echo "dho: source this file from bash or zsh" >&2
    return 1
fi
