# stubgen

A tiny Windows CLI that bakes a command line into a standalone launcher
`.exe` — no console flash, no shell scripting, no dependencies at runtime.

## Why

Windows doesn't have a good native way to say "run this command, silently,
from a double-clickable file":

- **Symlinks aren't native enough.** `mklink` needs elevation (or Developer
  Mode) and creates a filesystem link, not something you can hand someone
  as a portable `.exe` — and it doesn't help at all if what you want is to
  run a *command*, not just alias a path.
- **`.cmd` / `.bat` files flash a console window** even for one-line
  wrappers, and if the target is a GUI program, you get an ugly visible
  console for the split second before it exits.
- **Shortcuts (`.lnk`) are fragile and non-portable.** They store absolute
  paths, don't survive being moved to another machine well, and can't
  easily be generated/scripted without COM gymnastics.
- Sometimes you just want a **single self-contained `.exe`** you can drop
  in a folder, pin to the taskbar, or hand to someone else — that silently
  runs one specific command, every time, with no visible window and no
  extra files alongside it.

`stubgen` solves this by patching a pre-built launcher template with your
command, producing a real `.exe` that does exactly one thing.

## What it does

`stubgen` takes a command line and an output path, and writes a small
native launcher exe that runs that exact command via `CreateProcess` when
double-clicked or invoked. There are two variants:

- **Console stub** (default) — waits for the child process and propagates
  its exit code. Use this for command-line tools you're calling from a
  terminal or script and still want exit-code checking for.
- **Window stub** (`-w`) — fire-and-forget, no console window ever, not
  even briefly. Use this for GUI programs or anything launched from
  Explorer, a shortcut, or the taskbar.

Both variants are CRT-free (no `printf`/`malloc`/etc. — just raw
`kernel32` calls), which keeps them tiny (a few KB) and dependency-free:
the generated `.exe` needs nothing but itself to run.

Any arguments passed to the generated launcher are appended to the baked
command, the same way `%*` works in a batch file — so `mylauncher.exe foo
bar` runs `<baked command> foo bar`.

## How to run it

```
stubgen [-w] [-r] [-x] <output-exe> <command...>
```

- `<output-exe>` — path for the generated launcher. `.exe` is appended
  automatically if not already present.
- `<command...>` — everything after the output path is taken verbatim as
  the command to bake in (must be under 1016 bytes).
- `-w` — use the windowed (GUI) stub instead of the console stub.
- `-r` — **relative mode**: resolve the target program's path relative to
  the *launcher's own folder* instead of the caller's current directory.
  Use this when you want to copy a launcher and its target program around
  together (e.g. on a USB stick or a synced folder) and have it keep
  working regardless of where it ends up, as long as the relative
  position between the two is preserved.
- `-x` — compress the output with [UPX](https://upx.github.io/) after
  writing it (see below). If UPX can't be found or fails, `stubgen` just
  leaves the exe uncompressed and prints a note — it won't fail the build.

Flags can be given in any order (`-w -r -x`, `-x -w`, etc.), but each needs
its own `-`; bundled short flags like `-wrx` are not supported.

### Examples

```
:: Run a console command silently, without a lingering console flash
stubgen backup.exe "cmd /c robocopy D:\src D:\backup /MIR"

:: Launch a GUI app with no console window at all
stubgen -w notepad.exe notepad.exe

:: Bundle a launcher next to a portable tool, so both can be moved together
stubgen -r tool.exe ..\..\vendor\tool\tool.exe --flag

:: Same as the first example, but UPX-compressed
stubgen -x backup.exe "cmd /c robocopy D:\src D:\backup /MIR"
```

## Integrating with UPX

Pass `-x` and `stubgen` will run [UPX](https://upx.github.io/) on the
freshly written launcher right after patching it, shrinking it further.

To make this work, put `upx.exe` in either of:

1. The same folder as `stubgen.exe`, or
2. Anywhere on your `PATH`.

`stubgen` checks both locations in that order. If UPX isn't found, or it
runs but returns a non-zero exit code, `stubgen` doesn't fail the whole
invocation — the launcher exe was already written successfully before
compression was attempted, so a missing or failing `upx` just means you
keep the uncompressed exe instead of losing the output entirely.

## Building from source

Requires [Nim](https://nim-lang.org/) (>= 2.2.10) and a `gcc` toolchain
(e.g. MinGW-w64) on `PATH`.

```
nimble build
```

This compiles the two stub templates (`out/stubc.exe`, `out/stubw.exe`)
via `gcc` first, then compiles `stubgen.nim`, which embeds both templates'
bytes at compile time. The result, `out/stubgen.exe`, is fully
self-contained — only that one file is needed at runtime, no DLLs or
templates alongside it.

See [`CLAUDE.md`](CLAUDE.md) for implementation details and design notes.

## License

[MIT](LICENSE)
