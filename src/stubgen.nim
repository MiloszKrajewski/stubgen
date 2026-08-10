## stubgen [-w] [-r] [-x] [-h] <output-exe> <command...>
##
## Patches an embedded copy of a stub.exe template, overwriting the 1-byte
## mode flag and command that follow its ##CMD## magic marker, and writes
## the result to <output-exe>. The template itself is never modified, and
## stubgen.exe is fully self-contained — no stub files need to exist on
## disk at runtime.
##
## -r (relative mode) makes the generated launcher resolve the first token
## of its command (the target program path) at runtime relative to its own
## directory (via GetModuleFileNameA) instead of the caller's current
## directory, so the launcher can be copied around together with its
## target as long as their relative offset is preserved.
##
## -x (compress) runs upx on the freshly-written <output-exe>, looking for
## upx.exe first next to stubgen.exe itself, then on PATH. If upx.exe can't
## be found anywhere, this is not an error — stubgen prints a note and
## leaves the exe uncompressed, since compression is a nice-to-have, not a
## requirement for a working launcher.
##
## Two stub templates are baked in at Nim compile time:
##   stubc.exe  (-mconsole)  waits for child, propagates exit code; use for
##              console programs run from a terminal.
##   stubw.exe  (-mwindows)  fire-and-forget, no console window; use for
##              GUI programs launched from Explorer or shortcuts.
##
## The templates are pulled from ../out/stubc.exe and ../out/stubw.exe
## (relative to this file) at compile time, so both must already exist —
## see the `before build` hook in stubgen.nimble which builds them automatically.

import std/[os, osproc, strformat, strutils]

const
  Magic = "##CMD##"
  CmdSize = 1024
  MaxLen = CmdSize - Magic.len - 1 ## -1 for the mode flag byte; must match CMD_SIZE in stub.c
  StubCTemplate = staticRead("../out/stubc.exe")
  StubWTemplate = staticRead("../out/stubw.exe")

const UsageText = """
stubgen [-w] [-r] [-x] [-h] <output-exe> <command...>

Creates <output-exe> (.exe appended automatically), a small launcher that
silently runs <command...> -- e.g. to launch a program without a console
window, or to shorten/relocate a long command into a single exe.
<command...> tokens are joined with spaces and must total under """ & $MaxLen & """ bytes.

  -w  window stub: fire-and-forget, no console window
      (default: console stub, waits for child, propagates exit code)
  -r  relative mode: resolve the command's first token (the target program
      path) at runtime, relative to the launcher exe's own directory
      instead of the caller's current directory
  -x  compress the output with upx (looked for next to stubgen.exe, then
      on PATH); silently skipped with a note if upx.exe can't be found
  -h  show this help and exit

Flags must come before <output-exe>; everything from <output-exe> onward,
including any further '-'-prefixed tokens, is taken verbatim as the output
path and command."""

type
  Options = object
    console: bool
    relative: bool
    compress: bool
    output: string
    command: seq[string]

proc helpAndQuit(code: cint) {.noreturn.} =
  echo UsageText
  quit(code)

proc parseCommandLine(args: seq[string]): Options =
  result = Options(console: true)

  var i = 0
  while i < args.len and args[i].startsWith('-'):
    case args[i]
      of "-w": result.console = false
      of "-r": result.relative = true
      of "-x": result.compress = true
      of "-h":
        helpAndQuit(0)
      else:
        echo fmt"error: unknown option '{args[i]}'"
        helpAndQuit(1)
    inc i

  if i >= args.len or i + 1 >= args.len:
    echo "error: missing <output-exe> or <command...>"
    helpAndQuit(1)

  result.output = args[i]
  result.command = args[i + 1 .. ^1]

func magicOffset(stub, fileName: string): int =
  ## offset of the mode flag byte, immediately after the magic marker;
  ## the command itself starts one byte further
  let occurrences = stub.count(Magic)
  doAssert occurrences == 1, fmt"embedded {fileName} must contain exactly one '{Magic}' marker, found {occurrences}"
  stub.find(Magic) + Magic.len

const
  ModeOffsetC = magicOffset(StubCTemplate, "stubc.exe")
  ModeOffsetW = magicOffset(StubWTemplate, "stubw.exe")

func fill(s: var string, offset, length: int, c: char) =
  s[offset ..< offset + length] = repeat(c, length)

func copy(s: var string, offset: int, src: string) =
  s[offset ..< offset + src.len] = src

proc patch(outPath: string, newCmd: string, stub: string, modeOffset: int, relative: bool) =
  if newCmd.len >= MaxLen:
    quit(fmt"error: command too long ({newCmd.len} >= {MaxLen})", 1)

  let cmdOffset = modeOffset + 1
  var data = stub
  data[modeOffset] = (if relative: '1' else: '0')
  # zero out the old command, then write the new one
  data.fill(cmdOffset, MaxLen, '\0')
  data.copy(cmdOffset, newCmd)

  writeFile(outPath, data)
  echo fmt"Wrote '{outPath}' -> {newCmd}"

proc findUpx(): string =
  ## next to stubgen.exe itself takes priority over PATH, so a upx.exe
  ## dropped alongside a distributed stubgen.exe is picked up without
  ## needing to touch PATH at all
  let nextToSelf = getAppDir() / "upx.exe"
  if fileExists(nextToSelf): return nextToSelf
  findExe("upx")

proc tryCompress(path: string) =
  let upx = findUpx()
  if upx.len == 0:
    echo "note: -x requested but upx.exe was not found (checked stubgen's own directory and PATH) -- compression not performed"
    return

  let (output, code) = 
    execCmdEx(fmt"{quoteShell(upx)} -q -9 {quoteShell(path)}", options = {poUsePath, poStdErrToStdOut})
  if code != 0:
    echo fmt"note: upx compression failed (exit {code}), leaving '{path}' uncompressed"
    if output.len > 0: echo output
  else:
    echo fmt"Compressed '{path}' with upx"

proc main(opts: Options) =
  var targetPath = opts.output
  if not targetPath.toLowerAscii.endsWith(".exe"):
    targetPath.add(".exe")

  let (stub, modeOffset) =
    if opts.console: (StubCTemplate, ModeOffsetC)
    else: (StubWTemplate, ModeOffsetW)

  patch(targetPath, opts.command.join(" "), stub, modeOffset, opts.relative)

  if opts.compress:
    tryCompress(targetPath)

when isMainModule:
  main(parseCommandLine(commandLineParams()))
