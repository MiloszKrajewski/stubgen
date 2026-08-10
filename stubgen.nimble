# Package

version       = "0.1.0"
author        = "Milosz Krajewski"
description   = "first try"
license       = "MIT"
srcDir        = "src"
binDir        = "out"
bin           = @["stubgen"]

# Dependencies

requires "nim >= 2.2.10"

# stubgen.nim embeds out/stub.exe at compile time (staticRead), so it must
# already exist and be up to date before `nimble build` compiles stubgen.
before build:
  mkdir "out"
  exec "gcc -Os -s -o out/stubc.exe src/stub.c -mconsole -nostartfiles -Wl,-eWinMain -lkernel32 -ffunction-sections -fdata-sections -Wl,--gc-sections"
  exec "gcc -Os -s -o out/stubw.exe src/stub.c -mwindows -DSTUB_WINDOW -nostartfiles -Wl,-eWinMain -lkernel32 -ffunction-sections -fdata-sections -Wl,--gc-sections"
