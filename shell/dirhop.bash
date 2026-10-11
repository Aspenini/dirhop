# dirhop — bash and zsh integration.
# Source this file from ~/.bashrc or ~/.zshrc. The `dho` binary must be on PATH.
#
# The binary runs in a command substitution, which keeps it in the
# terminal's foreground job in both shells. A process substitution does not:
# zsh starts one in the background, where it cannot read the keyboard.
#
# Quitting writes nothing and exits non-zero, so cd is skipped. The trailing
# "x" stops the substitution from eating a newline at the end of a name.

dho() {
    [ -n "${ZSH_VERSION-}" ] && emulate -L zsh
    local target
    case ${1-} in
        -h | --help | -V | --version | --setup)
            command dho "$@"
            return
            ;;
    esac
    target=$(command dho "$@" && printf x) || return
    target=${target%x}
    target=${target%$'\n'}
    if [ -n "$target" ] && [ "$target" != "$PWD" ]; then
        builtin cd -- "$target"
    fi
}
