# Nim solution howto

How to bootstrap a new Nim/Nimble project from scratch with the same shape
as this one: sources in `src/`, build output in `out/`, and a working
VS Code debug setup. Written as a walkthrough, not a reference — follow it
top to bottom the first time, then use the cheatsheet at the end.

Placeholders used below: `<NAME>` (project/package name), `<AUTHOR>`,
`<DESCRIPTION>`, `<ENTRY>` (main `.nim` file name, usually same as `<NAME>`).

## 1. One-time machine setup

You only need to do this once per machine, not per project.

1. **Install `choosenim`** (installs and manages Nim + Nimble versions).
   Download from https://nim-lang.org/install.html and run it. It puts
   `nim`, `nimble` and `choosenim` on `PATH` (typically under
   `~/.choosenim/`).
2. **Install a C compiler.** Nim's default backend compiles to C and then
   shells out to a real C compiler. On Windows, `choosenim` offers to
   install a bundled MinGW-w64 toolchain during setup — accept that unless
   you already have one (e.g. via MSYS2). Verify with:
   ```
   gcc --version
   ```
3. **Install VS Code extensions**:
   - `nimlang.nimlang` — official Nim language support (syntax, suggest,
     goto-def, `nimsuggest` integration).
   - `vadimcn.vscode-lldb` — debugger. It bundles its own LLDB that reads
     the DWARF debug info GCC emits, so no extra setup is needed on the
     debugger side.

Sanity check everything is wired up:
```
nim --version
nimble --version
gcc --version
```

## 2. Bootstrap the project

```
mkdir <NAME>
cd <NAME>
nimble init
```

`nimble init` infers the package name from the directory name and the
author from your git config, then prompts interactively:

```
Package type? [library/binary/hybrid]      -> binary
Initial version of package? [0.1.0]        -> <enter for default, or type one>
Package description? [...]                 -> <DESCRIPTION>
Package License? [...]                     -> MIT (or whatever applies)
Lowest supported Nim version? [2.2.10]     -> <enter for default>
```

Pick **binary** for an executable tool (like this project), **library**
if you're publishing something to be imported, **hybrid** for both.

This produces:
```
<NAME>.nimble
src/<NAME>.nim
```

`<NAME>.nimble` looks like:
```nim
# Package

version       = "0.1.0"
author        = "<AUTHOR>"
description   = "<DESCRIPTION>"
license       = "MIT"
srcDir        = "src"
bin           = @["<ENTRY>"]

# Dependencies

requires "nim >= 2.2.10"
```

`srcDir = "src"` is already the default `nimble init` picks. Output
binaries land in the project root by default though — that's the one
thing worth changing next.

## 3. Redirect build output to `out/`

Add a `binDir` line to `<NAME>.nimble` so compiled executables don't
clutter the project root:

```nim
srcDir        = "src"
binDir        = "out"
bin           = @["<ENTRY>"]
```

Then add a `nim.cfg` in the project root to pin compiler settings and
redirect intermediate C files (`nimcache`) out of the way too:

```
cpu = amd64
cc = gcc
backend = c
nimcache = "./.cache"
```

`nim.cfg` is picked up automatically by both `nim c` and `nimble build`
for any target in this directory — no separate wiring needed.

`.gitignore`:
```
/.cache
/out
```

## 4. Wire up VS Code

Three files under `.vscode/`:

**`.vscode/settings.json`** — a single variable so `launch.json` doesn't
hardcode the exe name twice:
```json
{
  "mainExePath": "out/<ENTRY>.exe"
}
```

**`.vscode/tasks.json`** — the build task, using `--debugger:native` so
GCC emits debug symbols LLDB can read:
```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "Nim: build project (debug)",
      "type": "shell",
      "command": "nimble",
      "args": ["build", "--debugger:native"],
      "group": { "kind": "build", "isDefault": true },
      "problemMatcher": []
    }
  ]
}
```

**`.vscode/launch.json`** — F5 builds (via `preLaunchTask`) then launches
under LLDB:
```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug <NAME>",
      "type": "lldb",
      "request": "launch",
      "program": "${workspaceFolder}/${config:mainExePath}",
      "args": [],
      "cwd": "${workspaceFolder}",
      "preLaunchTask": "Nim: build project (debug)"
    }
  ]
}
```

At this point `F5` in VS Code builds and debugs the project, and
`nimble build` from a terminal produces a release-ish build at
`out/<ENTRY>.exe`.

## 5. Files Nimble manages for you

Two extra files may appear once you build or add dependencies — don't
hand-author these:

- **`config.nims`** — created by Nimble to auto-include `nimble.paths` if
  present:
  ```nim
  # begin Nimble config (version 2)
  when withDir(thisDir(), system.fileExists("nimble.paths")):
    include "nimble.paths"
  # end Nimble config
  ```
- **`nimble.paths`** — holds extra `--path:` entries, e.g. when you
  `nimble develop` a local dependency package. Empty/absent projects
  never need it. Safe to commit (it only affects this project's own
  build) or gitignore, your call.

## 6. Optional: pre-build hooks (native code, embedded assets)

If part of the project is non-Nim (a C helper compiled separately, an
asset embedded via `staticRead`), add a `before build:` hook to the
`.nimble` file so it runs automatically ahead of the Nim compile:

```nim
before build:
  mkdir "out"
  exec "gcc -Os -s -o out/helper.exe src/helper.c"
```

The Nim side then does `staticRead("../out/helper.exe")` at compile time.
Order matters: `before build` runs before Nim compilation starts, so the
generated file must exist by the time any `staticRead` in your `.nim`
source runs. Building directly with `nim c src/<ENTRY>.nim` **skips**
this hook — always use `nimble build` (or make sure the hook's output
already exists) when such a dependency is in play.

## 7. Cheatsheet

```
nimble build                       # normal (release-ish) build -> out/
nimble build --debugger:native     # debug build, matches VS Code task
nimble build -d:release            # explicit release, extra optimization
nim c -o:out/<ENTRY>.exe src/<ENTRY>.nim   # compile directly, skips nimble hooks
choosenim list                     # installed/available Nim versions
choosenim <version>                # switch active Nim version
nimble tasks                       # list custom tasks defined in .nimble
```

## 8. Troubleshooting

- **`cannot open file: ../out/....exe`** during compile — a `before
  build` hook didn't run because you compiled with `nim c` directly
  instead of `nimble build`. Use `nimble build`.
- **LLDB won't stop at breakpoints** — check the build actually used
  `--debugger:native` (release builds strip debug info); confirm
  `preLaunchTask` in `launch.json` matches the `label` in `tasks.json`
  exactly.
- **`gcc` not found** — it's not on `PATH`. Re-run the `choosenim`
  MinGW install step, or add your MSYS2 `mingw64/bin` (or similar) to
  `PATH` manually.
