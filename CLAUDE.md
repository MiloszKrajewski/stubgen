# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

`run` — a windowless shell command launcher for Windows. Two parts:

- `src/stub.c` — a tiny CRT-free Windows executable that holds an embedded
  command string at a fixed offset and silently launches it via `CreateProcess`.
  Compiled **twice** from the same source into two templates with different
  subsystem flags and a `-D` conditional:
  - `out/stubc.exe` — `-mconsole`, waits for child, propagates exit code.
    Use for console programs run from a terminal.
  - `out/stubw.exe` — `-mwindows -DSTUB_WINDOW`, fire-and-forget, no console
    window ever. Use for GUI programs launched from Explorer or shortcuts.
- `src/stubgen.nim` — a Nim CLI (`stubgen`) that patches a copy of the chosen
  template, overwriting the embedded command with a caller-supplied one,
  producing a new launcher exe for that specific command.

Both templates' bytes are embedded into `stubgen.exe` **at Nim compile time**
via `staticRead` — so both `out/stubc.exe` and `out/stubw.exe` must already
exist before `stubgen.nim` is compiled, but the resulting `stubgen.exe` is
fully self-contained at runtime.

## Build commands

`nimble build` builds everything in order:

1. `before build` hook: compiles `stubc.exe` and `stubw.exe` via `gcc`
2. Nim compiles `stubgen.nim`, embedding both templates at compile time

```
nimble build
```

Building directly with `nim c src/stubgen.nim` skips the hook, so both
stub templates must already exist or compilation fails with
"cannot open file: ../out/stubc.exe".

Binary output goes to `out/` (`binDir` in `stubgen.nimble`); C-backend
intermediates go to `.cache/` (`nimcache` in `nim.cfg`). Both directories are
gitignored.

Debug build of a stub variant (adds CRT, so `printf` works):
```
gcc -o stubc_debug.exe src/stub.c -mconsole
gcc -o stubw_debug.exe src/stub.c -mwindows -DSTUB_WINDOW
```

## Using stubgen

`stubgen.exe` is standalone at runtime — only `out/stubgen.exe` itself is
needed.

```
stubgen [-w] [-r] [-x] <output-exe> <command...>
```

Examples:
```
out\stubgen.exe out\backup.exe "cmd /c robocopy D:\src D:\backup /MIR"
out\stubgen.exe -w out\notepad.exe notepad.exe
out\stubgen.exe -r out\tool.exe ..\..\vendor\tool\tool.exe --flag
out\stubgen.exe -x out\backup.exe "cmd /c robocopy D:\src D:\backup /MIR"
```

The `-w` flag selects `stubw.exe` (window/GUI stub); default is `stubc.exe`
(console stub). The command must be under 1016 bytes (1024-byte slot minus
the 7-byte `##CMD##` magic prefix and 1 mode-flag byte).

The `-r` flag puts the launcher in **relative mode**: at runtime, the first
token of the baked command (the target program path) is resolved relative
to the *launcher exe's own directory* (via `GetModuleFileNameA`) instead of
the caller's current directory. This lets a launcher and its target be
copied around together — anywhere — and still find each other, as long as
their relative offset is preserved. Only the first token is resolved; any
further baked-in args and the caller's own `%*`-style passthrough args are
untouched. `-w`, `-r` and `-x` combine, in any order.

The `-x` flag runs `upx` on the freshly-written `<output-exe>` after
patching, looking first for `upx.exe` next to `stubgen.exe` itself, then on
`PATH`. If `upx.exe` can't be found anywhere, or if it runs but exits
non-zero, `stubgen` prints a note and leaves the exe uncompressed rather
than failing the whole invocation — the exe was already written
successfully before compression was attempted, so a missing/failing `upx`
shouldn't take that away.

`<output-exe>` gets `.exe` appended automatically unless it already ends in
`.exe` (case-insensitive) — `stubgen gl git log` produces `gl.exe`, and
`stubgen do.stuff.with.stuff.exe ...` is left untouched rather than treating
the embedded dots as some other extension.

Argument parsing is hand-rolled (`parseCommandLine` in `stubgen.nim`, no
dependencies): tokens starting with `-` are scanned as flags until the first
token that doesn't — that token is `<output-exe>`, and everything after it
is taken verbatim as `<command...>`, regardless of any `-`/`--` prefixes it
contains. `-h` prints usage and exits. Because flag scanning stops at
`<output-exe>`, a baked-in `<command...>` that itself contains flag-like
tokens (like `--flag` above) needs no special escaping.

Both embedded templates are validated to contain the `##CMD##` magic marker
**exactly once** at compile time (`const ModeOffsetC` / `ModeOffsetW` in
`stubgen.nim`) — if `stub.c` changes incompatibly, `stubgen.nim` fails to
compile rather than patching the wrong offset at runtime. The byte
immediately after the marker is a 1-char mode flag (`'0'`/`'1'`, selecting
CWD-relative vs self-relative resolution); the command itself starts one
byte further.

## Architecture notes worth knowing before editing

- **Two stubs, one source**: `stub.c` is compiled twice. The `#ifdef
  STUB_WINDOW` block gates only the post-`CreateProcess` logic (fire-and-forget
  vs wait + exit code). Everything else — the magic-prefix trick, arg
  passthrough, `args_only()` — is shared.
- **The magic-prefix trick**: `stub.c` defines `CMD = CMD_MAGIC "0notepad.exe"`
  at compile time, so the binary is guaranteed to contain `##CMD##` followed by
  a 1-byte mode flag and then a command, all at fixed offsets. `stubgen` finds
  `##CMD##` and overwrites only the bytes after it — no recompilation needed
  per command. The stub deliberately does *not* re-check the magic string at
  runtime: doing so would embed `"##CMD##"` a second time in the binary,
  giving `stubgen`'s search two matches and making the correct patch offset
  ambiguous. The mode flag is folded into this same marker (rather than a
  second magic string) to avoid the size/complexity cost of a whole extra
  marker-plus-occurrence-check for a single byte of data.
- **Mind the file-alignment cliff**: the stub templates were exactly 4096
  bytes (one NTFS allocation unit) before relative-mode was added; `stubc.exe`
  is 5120 now. PE sections are padded to the linker's file alignment, so size
  grows in ~512-byte jumps rather than smoothly — a few added bytes can
  silently cost a whole extra page (check `ls -la out/stubc.exe` before/after
  non-trivial edits). Relative-mode's two extra imports (`GetModuleFileNameA`,
  `GetFullPathNameA`) and its resolution logic account for the growth; folding
  the mode flag into `##CMD##` (see above) clawed back one tier, but chasing
  the old 4096-byte boundary further would mean merging `args_only()` and
  `first_token()` into one function — tried once, reverted: it saved another
  512 bytes but still landed short of 4096, at the cost of a shared function
  with an awkward NULL-sink calling convention. Not worth it for a partial
  win; `stub.c` keeps `args_only()` and `first_token()` separate, each doing
  one obvious thing.
- **Why `-nostartfiles`**: skips MinGW's CRT startup (saves ~25-50 KB) but
  requires two manual fixes in `stub.c`: `lpCmdLine` is never filled by
  Windows without `WinMainCRTStartup`, so `GetCommandLineA()` is used instead
  (skipping past the exe name/quotes to get the `%*`-equivalent args); and
  `ExitProcess` must be called explicitly since nothing calls it after
  `WinMain` returns.
- **Why not `cmd /c start`**: `start` treats the first quoted argument as a
  window title, not part of the command — breaks when the command itself
  needs to be quoted. `CreateProcess` is already fire-and-forget, so `start`
  buys nothing; call the target exe directly, or `cmd /c` only when shell
  features (pipes, redirects, wildcards) are actually needed.
- **Console vs window subsystem trade-off**: `-mconsole` exes are waited on by
  the shell naturally; `-mwindows` exes are not — the shell returns immediately
  regardless of `WaitForSingleObject`. That is why two separate templates exist
  rather than one stub with runtime detection.
- **Argument passthrough** mirrors batch-file `%*`: args passed to the generated
  launcher exe are appended (space-separated) to the embedded command before
  `CreateProcess` runs it.
- **Relative-mode resolution**: when the mode byte is `'1'`, the stub splits
  the baked command into its first token (the target path) and the rest,
  via a hand-rolled quote-aware scanner (`first_token`, mirroring
  `args_only()`'s quote handling — no CRT). If that token is already
  drive-absolute or rooted/UNC (`is_absolute`), it's canonicalized as-is;
  otherwise it's concatenated onto the stub's own directory (from
  `GetModuleFileNameA`, with the filename stripped by a hand-rolled
  backslash scan — no `strrchr`, this build has no CRT) and canonicalized
  with `GetFullPathNameA`. That call is a plain `kernel32` export (no new
  link dependency) and, given an already drive-qualified input string,
  resolves `.`/`..` segments purely lexically without touching the
  process's current directory. The resolved path is always re-quoted before
  being spliced back in front of the remaining tokens, since its directory
  may contain spaces even when the original relative token didn't need
  quoting. Failures (e.g. `GetModuleFileNameA` overflow) fall through with
  the best-effort/unresolved path rather than adding new error UI —
  `CreateProcessA`'s existing failure path already surfaces the error via
  exit code. Buffers for the self-path/combined-path work (`SELF_SIZE`,
  `TOK_SIZE`, `PATH_SIZE`) deliberately avoid `MAX_PATH` (260), which is
  too tight once a deep self-directory is combined with a multi-`..\`
  relative token — sized generously by hand instead, same spirit as the
  existing 32767-byte final command buffer.
