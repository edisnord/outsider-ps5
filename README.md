# Outsider PS5

RPG Maker MV and MZ games on a jailbroken PS5: [Outsider](https://github.com/edisnord/outsider) (branch `jsc`, with
JavaScriptCore and the JIT) on SDL3's GPU API, drawing with RADV (Mesa's Vulkan driver for the console), as one title (a game list and Outsider).

The Vulkan stack is [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan); the game list is built on
[PS5_VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate).

## State

- `scripts/build.sh` builds `build/PPSA77110` in the build image `outsider-ps5-dev` (a Dockerfile of its own is to do): `eboot.bin` is the game list (`launcher/`, the PS5 Vulkan Template's foundation, ImGui overlay),
  `outsider.bin` is Outsider (`runtime/`), `outsider/shims` its scripts.
- The list scans `/data/games` and the USB `games` folders for MV and MZ folders, writes
  `/data/outsider-ps5/launch.txt` and starts `outsider.bin` through the shell (`sceSystemServiceLoadExec`). Without
  that file `outsider.bin` starts the game named in `/data/outsider-ps5/game`, else the first game folder found.
- The log is `/data/outsider-ps5/last-run.log`.

## Planned

An own build image and JavaScriptCore build script; a prettier list (the template's UI kit is GPL-3.0 and could be used).

License: GPL-3.0-or-later (see LICENSE).
