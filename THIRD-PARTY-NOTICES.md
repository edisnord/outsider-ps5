# Third-party notices

This repository is GPL-3.0-or-later as a whole (`LICENSE`): Outsider, PS5_Vulkan and the PS5 payload SDK fork are
GPL-3.0 (Outsider is also offered commercially by its owner; this project uses it under the GPL). The license texts of
each part are in `licenses/third-party/`, and are copied into the title folder (`licenses/`) by the build.

| Part | Used for | License |
| --- | --- | --- |
| [Outsider](https://github.com/edisnord/outsider) (fork of General Arcade's Outsider) | the MV and MZ runtime | GPL-3.0 (dual-licensed by its owner) |
| `runtime/`, `sdl3/` | start-up, log, paths, SDL3 PS5 back-ends | GPL-3.0-or-later |
| [PS5_VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate) (`launcher/`) | the game list's foundation: Sascha Willems' Vulkan examples base and the PS5 layer | MIT |
| Dear ImGui, stb, volk, tinygltf, KTX, GLM, Vulkan headers (in `launcher/external/`) | the launcher | MIT / Apache-2.0 / zlib (see their folders) |
| Roboto Medium | the launcher's font | Apache-2.0 |
| [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan), [PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa) (RADV, Mesa) | the Vulkan driver, linked in | GPL-3.0 (PS5_Vulkan), MIT (Mesa) |
| [XashPS5](https://github.com/GordonProsperoMan/xash3d-ps5)'s Mesa patch (`zink/ps5-mesa-zink.patch`) | Zink on RADV and its EGL, only in the optional `zink-smoke.bin` probe | GPL-3.0-or-later (XashPS5), MIT (`ps5_zink_egl.c`, Mesa) |
| [PS5_PayloadSDK](https://github.com/mihawk-99/PS5_PayloadSDK) | the SDK and the platform layer, linked in | GPL-3.0 |
| SDL3 | window, input, audio | zlib |
| SoLoud | audio | zlib |
| JavaScriptCore, WTF, bmalloc (WebKit) | the JavaScript engine | LGPL-2.1 / BSD-2-Clause (per file; see `licenses/third-party/WebKit/`) |
| ICU | JavaScriptCore | Unicode license |
| FFmpeg, libwebp, libiconv, zlib | video, images, text conversion | LGPL-2.1+ / BSD / LGPL / zlib |
| QuickJS-NG | fallback JavaScript engine | MIT |

Corresponding source for a binary release: this repository, the Outsider fork at the commit named in the release
notes, and the pinned upstream sources named in the build image's recipe. If a license text here is missing a part
that is linked into a build, that is a bug: open an issue.
