# Getting `stubc.exe`/`stubw.exe` back to exactly 4096 bytes

## Why 4096 matters (and 512 doesn't)

NTFS allocates disk space in whole **clusters**, not bytes. On this machine's
`D:` drive the cluster size is 4096 bytes (confirmed via
`Get-CimInstance -ClassName Win32_Volume -Filter "DriveLetter='D:'" |
Select-Object BlockSize`; `fsutil fsinfo ntfsinfo D:` needs elevation and
fails with access denied otherwise).

A file occupies a whole number of clusters, so:
- Any size from 1 to 4096 bytes costs **1 cluster** (4096 bytes on disk).
- Any size from 4097 to 8192 bytes costs **2 clusters** (8192 bytes on disk).

The 512-byte jumps visible via `ls -la out/stubc.exe` (5632 → 5120 → 4608
...) are the **PE linker's file alignment** (how `ld` packs sections inside
the exe), which is unrelated to NTFS allocation granularity. Shrinking from
5632 to 4608 saves **zero** real disk space — both round up to the same 2
clusters. Only landing at or under 4096 actually halves the on-disk
footprint (2 clusters -> 1).

## Current state

`stubc.exe` is 5120 bytes (`src/stub.c`, `CMD_SIZE = 1024`, `args_only()`
and `first_token()` kept as separate, single-purpose functions). This was a
deliberate choice: an earlier pass reached 4608 by merging `args_only()`
into `first_token()` (via a NULL-sink calling convention), but that was
reverted because it cost readability for a size change that, per the
`fsutil`/`BlockSize` finding above, doesn't save any real disk space anyway
(4608 and 5120 both round up to 2 clusters).

## What it would take to actually hit 4096

Verified by scratch-building variants (not applied to the repo):

1. **Shrinking `CMD_SIZE` alone is not enough.** `.data` is the only section
   whose size scales with `CMD_SIZE`, but it always starts at a fixed
   512-byte-aligned file offset (determined by `.text`'s end, which doesn't
   change) and always consumes at least one full 512-byte chunk on disk
   regardless of exact content size below that. Tested `CMD_SIZE` at 512,
   384, 320, 256 — all four land at the **same** 4608 bytes. There's no
   partial credit; the floor from this lever alone is 4608, not 4096.

2. **Combining `CMD_SIZE = 512` with re-merging `args_only()`/
   `first_token()`** (the reverted change) lands at exactly **4096 bytes**,
   verified by an actual build. This needs both changes together:
   - `CMD_SIZE` 1024 → 512 shaves one 512-byte chunk off `.data`'s
     contribution to the cumulative file offset.
   - Merging `args_only()` into `first_token()` (call it with a NULL
     tok/0 toklimit to just skip-and-discard a token, reusing the same
     quote-scanning loop instead of duplicating it) shaves the `.text`
     section enough to cross the remaining boundary.

   Side effect: `CMD_SIZE = 512` drops the max baked command length from
   1016 bytes to **504 bytes** (`MaxLen = CmdSize - Magic.len - 1`). Still
   generous for any realistic single command line, but worth knowing.

## Decision

Not applied. The combination reintroduces the less-readable merged
`args_only()`/`first_token()` function (an awkward NULL-sink calling
convention in place of two small, obvious functions) in exchange for a
real but modest win: 1 fewer NTFS cluster (4096 bytes) per generated
launcher exe, on a drive where clusters are 4096 bytes. Revisit this file
if that trade becomes worth making — the exact combination above
(`CMD_SIZE = 512` + the merge) is confirmed to work.

## Dead end: manually forcing inline/noinline

Tried `__attribute__((always_inline))` / `__attribute__((noinline))` on
`stub.c`'s helpers to see if hand-tuning could beat `-Os`'s own inlining
decisions. It can't, at least not here — checked actual `.text` size and
symbol tables (not just the final rounded file size, which can mask small
deltas) for each variant against the (then-current, 1024-byte-`CMD_SIZE`)
baseline:

| Change | `.text` size | Result |
|---|---|---|
| baseline, no hints | 1040 bytes | `-Os` already inlines every single-call-site helper (`args_only`, `first_token`, `self_dir_len`, `is_absolute`, `canon`) on its own; only `scpy` (called ~7×) stays out-of-line |
| `always_inline` on `first_token` | 1040 bytes | no-op — it only has one call site, `-Os` was already inlining it |
| `noinline` on the 4 auto-inlined helpers | 1152 bytes (+112) | worse — pure call/ret overhead, no duplication was happening to offset it |
| `always_inline` on `scpy` | 1136 bytes (+96) | worse — duplicates the copy loop at all ~7 call sites instead of sharing one out-of-line copy |
| `noinline` on `scpy` | 1040 bytes | no-op — matches what `-Os` already does |

Every hand-forced hint either matched `-Os`'s existing choice or made
things worse; none improved on it. `-Os`'s heuristic here is simple and
already optimal for this file: inline anything with exactly one call site,
keep anything called from multiple places (`scpy`) shared. Not worth
revisiting unless a future change to `stub.c` alters the call-site counts
enough to actually change what that heuristic would pick.
