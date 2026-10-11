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
 ↑↓ Move   Space Open   Enter CD
 ← Back     H Hidden     Q Quit
```

Only directories are shown. There is no configuration and nothing is saved between launches.

## Build

Dirhop is built with [xmake](https://xmake.io). The release flags are `-std=c11 -Os -Wall -Wextra`, and the binary is stripped.

```sh
xmake
```

The executable is under `build/`. Install the binary and the shell wrappers:

```sh
xmake install -o ~/.local
```

That places `dho` on `~/.local/bin` and the wrappers in `~/.local/share/dirhop/`. Add `~/.local/bin` to `PATH` if it is not already there.

The stripped dynamic binary is meant to stay under 30 KB.

## Shell integration

A child process cannot change its parent's directory, so `dho` is a shell function. It runs the binary, reads the chosen path, and `cd`s only after you confirm.

Bash or zsh, in `~/.bashrc` or `~/.zshrc`:

```sh
source ~/.local/share/dirhop/dirhop.bash
```

Fish, in `~/.config/fish/config.fish`:

```fish
source ~/.local/share/dirhop/dirhop.fish
```

The function calls `command dho`, which is the binary on `PATH`. The function and the binary share a name; `command` selects the binary. The package and the name drawn in the header stay `dirhop`.

Open a terminal, run `dho`, move with Space, and press Enter. The shell prompt returns inside the directory you highlighted. `q` or Esc leaves the shell where it was.

## Keys

| Key | Action |
| --- | --- |
| `↑` / `↓`, `k` / `j` | Move the selection |
| `Space` / `→` | Open the selected directory |
| `Enter` | Print that directory and exit so the shell can `cd` |
| `Backspace` / `←` | Go to the parent directory |
| `h` | Show or hide hidden directories |
| `q` / `Esc` | Quit and keep the current directory |

`j` moves down and `k` moves up. Letter keys also work in uppercase.

Hidden directories start hidden. `h` toggles them immediately and keeps the current selection when that name is still visible. The choice is not stored. The next launch hides them again. While they are visible, the footer shows `Hidden*`.

Enter acts on the highlighted directory. From the parent, highlight a folder and press Enter to land inside it. Space is how you look further down before deciding. An empty folder shows `(empty)`. A folder you cannot read shows the error on the footer and stays on the current list.

The list scrolls when it is taller than the terminal. The screen is redrawn in one write, and the layout shrinks on short or narrow terminals.

## Path returned to the shell

Interface output goes to `/dev/tty`. Standard output is used only after Enter.

- When stdout is a pipe, the path is followed by a NUL byte. Newlines inside a directory name are preserved.
- When stdout is a terminal, the path is followed by a newline so a direct run is readable.

The process exits 0 after a confirmed path. Quit, Esc, and Ctrl-C restore the terminal and exit non-zero, with no path written. The shell function then returns without calling `cd`.

Paths stay logical. A directory symlink is listed and chosen under its own name, and `..` walks back along that path.

Names with spaces are ordinary arguments. The shell function passes the path as one quoted word.

## Scope

Version 0.1.0 is keyboard navigation, directory listing, the hidden toggle, scrolling, shell integration, and basic errors (empty folders, permissions, names with spaces or newlines).

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
