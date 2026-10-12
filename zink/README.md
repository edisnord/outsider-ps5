# Zink: OpenGL on RADV

A probe, not part of a release. Mesa's Zink runs OpenGL on top of the RADV Vulkan driver the title already has. If
it holds up on the console, Outsider's OpenGL 4.5 renderer could run on it, and a WebGL context for plugins that need
one (UltraMode7 and the like) could sit on top of it. This folder builds Zink and runs a smoke test. Nothing of
Outsider uses it yet.

The PS5 work comes from [XashPS5](https://github.com/GordonProsperoMan/xash3d-ps5) V1.3, which ships Half-Life on
Zink + RADV at 4K. `ps5-mesa-zink.patch` is its Mesa patch without one hunk; its header says which hunk and why. The
patch does three things:

- Zink calls RADV in the same program, with no `dlopen` and no Vulkan loader.
- Zink presents on VideoOut (`VK_KHR_display`) through its kopper layer.
- It adds a small EGL 1.4 for Zink (`ps5_zink_egl.c`). This EGL offers desktop OpenGL contexts and one window
  surface, the screen. It has no pbuffers and no GLES API (`eglBindAPI` accepts only `EGL_OPENGL_API`).

## Building

In the build image, after the usual steps:

```
scripts/build.sh zink zink-smoke launcher
```

- **`zink`** (`scripts/build-zink.sh`) builds PS5_Mesa at the revision the image's RADV is built from (PS5_Vulkan's
  `tools/build-radv.sh` pins it), with the patch, into `build/zink`. It builds RADV again in the same tree, because
  Zink and RADV must come from one Mesa build. It needs network for the Mesa source and meson's zlib, plus meson,
  ninja, and LLVM/Clang/libclc/llvm-spirv 19 for Mesa's build-time OpenCL tools. The script lists anything missing.
  `ZINK_MESA_REV` picks another revision.
- **`zink-smoke`** (`scripts/link-zink-smoke.sh`) links `zink-smoke.bin` from `zink_smoke.c` against those archives.
- **`launcher`**: the game list shows a **Zink test** button when `zink-smoke.bin` is in the title.

`scripts/package.sh` leaves `zink-smoke.bin` out unless `INCLUDE_ZINK_SMOKE=1`.

## Running

Choose **Zink test** in the game list. For about ten seconds it shows a colour cycle and a spinning triangle, then
it goes back to the list. Read `/data/outsider-ps5/zink-smoke.log` (and klog):

| line | means |
| --- | --- |
| `GL_RENDERER=zink (...)`, `GL_VERSION=4.6 ...` | Zink is up, on RADV |
| `es100 shader: ok` | a WebGL 1 shader (GLSL ES 1.00) compiles as-is on the desktop context |
| `fps=...` | it presents, and how fast |
| `eglInitialize failed` / no log at all | Zink or RADV did not start; klog has Mesa's messages |

## What comes after

1. Run Outsider's GL renderer (`RMMZ_RENDER_BACKEND=gl`, the old SDL2/OSMesa port's) on this EGL instead of SDL3's
   GPU API, and compare frame times with the Vulkan build on the same game. Zink owns VideoOut, so one program cannot
   present through both.
2. A WebGL 1 context in Outsider's JavaScript bindings, in place of the stub `getContext("webgl")` returns.
3. For plugins built on pixi.js's WebGL internals, the real pixi.js v4 and pixi-tilemap for those games.

## Not known yet

- Whether the image's payload SDK fork has `ps5_klog_capture_stderr`, which the EGL frontend calls.
  `zink_smoke.c` defines a no-op fallback, which may hide Mesa's messages from klog.
- PS5_Mesa `b3588f7` fixes Zink's persistently mapped buffers on a device without host-visible device-local memory.
  XashPS5 ran without it, but GL code that maps buffers persistently may need it.
- The frontend always makes a 3840x2160 swapchain and scales the GL back buffer into it. PS5_Mesa `47e9eb6` now
  also offers 1440p and 1080p modes, which the frontend does not use.
