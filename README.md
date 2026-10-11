# Dirhop

A tiny directory navigator for the terminal. It lists folders, you move with the keyboard, and Enter hands the chosen path back to your shell.

```
 dirhop  ~/Development/
 ──────────────────────────────────
   Wormhole/
 > PyC64/
   Rutted/
   Aspen-Browser/
   juice-arch64/
 ──────────────────────────────────
 ↑↓ Move   Space Open   Enter CD   . CD here
 ← Back     H Hidden     Q Quit
```

Only directories are shown. There is no configuration and nothing is saved between launches.

## Build

Dirhop is built with [xmake](https://xmake.io). The release flags are `-std=c11 -Os -Wall -Wextra` with LTO and no unwind tables, and the binary is stripped.

```sh
xmake
```

The executable is under `build/`. Install the binary and the shell wrappers:

```sh
xmake install -o ~/.local
```

That places `dho` on `~/.local/bin` and the wrappers in `~/.local/share/dirhop/`. Add `~/.local/bin` to `PATH` if it is not already there.

The stripped dynamic binary is meant to stay under 30 KB. It is about 23 KB on x86-64 Linux.

## Shell integration

A child process cannot change its parent's directory, so `dho` is a shell function. It runs the binary, reads the chosen path, and `cd`s only after you confirm. Arguments pass through, so `dho ~/src` starts there.

Bash or zsh, in `~/.bashrc` or `~/.zshrc`:

```sh
source ~/.local/share/dirhop/dirhop.bash
```

Fish, in `~/.config/fish/config.fish`:

```fish
source ~/.local/share/dirhop/dirhop.fish
```

The function calls `command dho`, which is the binary on `PATH`. The function and the binary share a name; `command` selects the binary. The package and the name drawn in the header stay `dirhop`.

Open a terminal, run `dho`, move with Space, and press Enter. The shell prompt returns inside the directory you highlighted, or press `.` to land in the directory on screen. `q`, Esc, or Ctrl-C leaves the shell where it was.

The bash and zsh function runs the binary in a command substitution, which keeps it in the terminal's foreground job. A process substitution (`< <(...)`) does not in zsh, and a background process cannot read the keyboard. If dho finds itself in the background anyway, it says so on stderr instead of quitting silently.

## Keys

| Key | Action |
| --- | --- |
| `↑` / `↓`, `k` / `j`, Ctrl-P / Ctrl-N | Move the selection |
| `PgUp` / `PgDn` | Move by a screen |
| `Home` / `End`, `g` / `G` | Jump to the first or last entry |
| `Space` / `→` | Open the selected directory |
| `Enter` | Print the selected directory and exit so the shell can `cd` |
| `.` | Print the directory on screen and exit |
| `Backspace` / `←` | Go to the parent directory |
| `h` | Show or hide hidden directories |
| Ctrl-L | Reread the directory and redraw |
| `q` / `Esc` / Ctrl-C | Quit and keep the current directory |

`j` moves down and `k` moves up. Letter keys also work in uppercase, except `g` and `G`, which are first and last.

Hidden directories start hidden. `h` toggles them immediately and keeps the current selection when that name is still visible. The choice is not stored. The next launch hides them again. While they are visible, the footer shows `Hidden*`.

Enter acts on the highlighted directory. From the parent, highlight a folder and press Enter to land inside it. Space is how you look further down before deciding; once inside the folder you want, press `.`. An empty folder shows `(empty)`, and Enter there picks that folder. A folder you cannot read shows the error and its name on the footer and stays on the current list.

Directories sort case-insensitively in natural order, so `v2` comes before `v10`.

The list scrolls when it is taller than the terminal. The screen is redrawn in one write, wrapped in synchronized output where the terminal supports it, and the layout shrinks on short or narrow terminals. Resizes are picked up immediately.

Ctrl-Z does nothing. The shell is waiting on dho to print a path, and a stopped dho could not be resumed from it.

## Path returned to the shell

Interface output goes to `/dev/tty`. Standard output is used only after Enter.

- The path is followed by a newline, so `cd "$(dho)"` works in a script.
- With `-0` (`--print0`) it is followed by a NUL byte instead. The fish function uses this.

Either way, newlines inside a directory name are preserved by the shell functions.

The process exits 0 after a confirmed path. `q` and Esc exit 1 and Ctrl-C exits 130, with the terminal restored and no path written. The shell function then returns without calling `cd`.

```
usage: dho [-0] [directory]
  -0, --print0   end the path with NUL instead of a newline
  -h, --help     show this help
  -V, --version  show the version
```

Paths stay logical. A directory symlink is listed and chosen under its own name, and `..` walks back along that path. Without an argument, dho starts from `$PWD` when it names the current directory, so a shell sitting in a symlinked path keeps that path.

Names with spaces are ordinary arguments. The shell function passes the path as one quoted word.

## Scope

Version 0.2.0 is keyboard navigation (including paging and jumps), directory listing in natural order, the hidden toggle, scrolling, shell integration, and basic errors (empty folders, permissions, names with spaces or newlines).

It does not manage files, search, bookmark, preview, use the mouse, read a config file, keep history, or load plugins.

## Layout

```
dirhop/
├── src/
│   ├── main.c       startup and keys
│   ├── term.c       raw mode, signals, keyboard
│   ├── ui.c         screen
│   ├── nav.c        selection and movement
│   ├── scan.c       directory listing
│   ├── path.c       logical paths
│   ├── output.c     path written on Enter
│   └── util.c
├── shell/
│   ├── dirhop.bash
│   └── dirhop.fish
├── xmake.lua
├── LICENSE
└── README.md
```

Headers sit next to the sources. The program is C11 and links only libc.
