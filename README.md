# Outsider PS5

RPG Maker MV and MZ games on a jailbroken PS5: [Outsider](https://github.com/edisnord/outsider) (branch `jsc`, with
JavaScriptCore and the JIT) on SDL3's GPU API, drawing with RADV (Mesa's Vulkan driver for the console), as one title (a game list and Outsider).

The Vulkan stack is [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan); the game list is built on
[PS5_VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate).

## Install and use (v0.0.1, early alpha)

You need a jailbroken PS5 that can run homebrew (an ELF loader and an FTP server such as ftpsrv on port 2121) and
a way to mount a title folder (a title mounter). This package does not include a game.

1. Download `outsider-ps5-v0.0.1.zip` from the Releases page and unpack it. It holds one folder, `PPSA77110`.
2. Copy the folder to the console, for example with FTP: `PPSA77110` to `/data/homebrew/PPSA77110`.
3. Put a game on the console, the folder that holds `index.html` (for MV, usually its `www` folder, or the folder
   above it):
   - `/data/outsider-ps5/<Name>/` (internal), or
   - `/mnt/usb0/games/<Name>/` or `/mnt/usb1/games/<Name>/` (USB).
4. Mount and start the title with your title mounter (title id `PPSA77110`, "Outsider PS5"). A list of the games it found
   appears: D-pad to move, CROSS to choose. "Rescan" looks again, "Quit" leaves.
5. The chosen game starts in Outsider.

Without the list's choice (for example if it never starts), Outsider starts the game named in the first line of
`/data/outsider-ps5/game`, otherwise the first game folder it finds.

**Logs.** `/data/outsider-ps5/last-run.log` is the log of the last run; copy it over FTP when reporting a problem. It
includes a "compat" section naming plugins that failed and features a game asked for that are not there.

**Diagnostics.** Off by default, so a recording does not pay for them. To turn on the per-120-frame perf lines (frame
times, audio mixer statistics) and SDL's GPU and video messages, create an empty file `/data/outsider-ps5/diag`;
delete it to turn them off again. A file `/data/outsider-ps5/outsider/profile` adds the sampling profiler.

**Known limits.**
- Early alpha: expect crashes and missing features. Tested with one game (Fear & Hunger 2: Termina, MV); occasional
  frame hitches of half a second or more happen at map and battle loads.
- Only MV and MZ games. Plugins that need WebGL (for example UltraMode7) fail to load.
- Games are not included and nothing is converted: bring your own, legally obtained copy.

## Building

`scripts/build.sh` needs the build image (below) and, next to this repository, a checkout of the
[Outsider fork](https://github.com/edisnord/outsider) at the revision in `OUTSIDER_REV`, plus a JavaScriptCore build
(`JSC_OUT`, default `build/jsc/out`). Then:

```
scripts/build.sh outsider link launcher   # build/PPSA77110
scripts/package.sh                        # dist/outsider-ps5-v<VERSION>.zip and SHA256SUMS
```

## Releasing

1. Set `VERSION` (and `OUTSIDER_REV` to the Outsider commit to ship), build as above, run `scripts/package.sh`.
2. Tag `v<VERSION>`, push the tag and upload `dist/outsider-ps5-v<VERSION>.zip` and `dist/SHA256SUMS` to the release by hand.
   The build needs the PS5 build image and a JavaScriptCore build, which is why there is no CI for it.

## State

- `scripts/build.sh` builds `build/PPSA77110` in the build image `outsider-ps5-dev` (a Dockerfile of its own is to do): `eboot.bin` is the game list (`launcher/`, the PS5 Vulkan Template's foundation, ImGui overlay),
  `outsider.bin` is Outsider (`runtime/`), `outsider/shims` its scripts.
- The list scans `/data/outsider-ps5` and the USB `games` folders for MV and MZ folders, writes
  `/data/outsider-ps5/launch.txt` and starts `outsider.bin` through the shell (`sceSystemServiceLoadExec`). Without
  that file `outsider.bin` starts the game named in `/data/outsider-ps5/game`, else the first game folder found.
- The log is `/data/outsider-ps5/last-run.log`.

## Planned

An own build image and JavaScriptCore build script; a prettier list (the template's UI kit is GPL-3.0 and could be used).

License: GPL-3.0-or-later (see LICENSE).
