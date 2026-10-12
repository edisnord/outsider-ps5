#!/usr/bin/env bash
# Builds SDL3 for the PS5 for the Vulkan build (see docs/BUILDING.md): SDL3 with the PS5 back-ends in
# vulkan/sdl3-ps5/ (scePad, sceAudioOut, and video for Vulkan: RADV linked into the program, presenting
# with VK_KHR_display), as a static library installed into PREFIX.
#
#   build-sdl3.sh <SDL3 checkout> <prefix>
#
# The checkout is copied; it is release-3.4.18 (docker/setup-vulkan.sh pins it).
set -euo pipefail

: "${PS5_PAYLOAD_SDK:=/opt/ps5-payload-sdk}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
src="$1"
prefix="$2"

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
cp -a "${src}/." "${work}/SDL3"
cp -r "${HERE}/sdl3-ps5/src/." "${work}/SDL3/src/"
git -C "${work}/SDL3" apply "${HERE}/sdl3-ps5/0001-ps5.patch"

# Only what the PS5 has, or what Outsider uses: no renderer (Outsider draws with SDL_GPU), no dialogs,
# camera, sensors, HIDAPI or tray. (The configure checks link with the payload SDK's libc.a, which the
# Vulkan program does not link: vulkan/vk_libc.c supplies what is found there and missing on the console.)
"${PS5_PAYLOAD_SDK}/bin/prospero-cmake" -S "${work}/SDL3" -B "${work}/build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="${prefix}" \
    -DCMAKE_VERBOSE_MAKEFILE=OFF \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    -DSDL_VIDEO=ON -DSDL_VULKAN=ON -DSDL_GPU=ON -DSDL_RENDER=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF \
    -DSDL_DIALOG=OFF -DSDL_CAMERA=OFF -DSDL_SENSOR=OFF -DSDL_HIDAPI=OFF -DSDL_TRAY=OFF -DSDL_POWER=OFF \
    -DSDL_DUMMYVIDEO=OFF -DSDL_OFFSCREEN=OFF -DSDL_DISKAUDIO=OFF -DSDL_DUMMYAUDIO=OFF \
    -DSDL_VIRTUAL_JOYSTICK=ON -DSDL_LIBUDEV=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_X11=OFF -DSDL_WAYLAND=OFF \
    -DSDL_KMSDRM=OFF -DSDL_OSS=OFF -DSDL_ALSA=OFF -DSDL_JACK=OFF -DSDL_PIPEWIRE=OFF -DSDL_PULSEAUDIO=OFF \
    -DSDL_SNDIO=OFF -DSDL_LIBURING=OFF -DSDL_RPATH=OFF -DSDL_CLOCK_GETTIME=ON -DSDL_UNIX_CONSOLE_BUILD=ON
cmake --build "${work}/build" -j"$(nproc)"
cmake --install "${work}/build"

# The configuration that matters, checked
cfg="${prefix}/include/SDL3/SDL_revision.h"
built="$(cat "${work}/build/include-config-release/build_config/SDL_build_config.h" 2>/dev/null || \
         find "${work}/build" -name SDL_build_config.h -exec cat {} +)"
for def in SDL_VIDEO_DRIVER_PS5 SDL_AUDIO_DRIVER_PS5 SDL_JOYSTICK_PS5 SDL_VIDEO_VULKAN SDL_GPU_VULKAN SDL_THREAD_PTHREAD; do
    grep -q "^#define ${def} 1" <<<"${built}" || { echo "build-sdl3.sh: ${def} is not set" >&2; exit 1; }
done
[ -f "${cfg}" ] && [ -f "${prefix}/lib/libSDL3.a" ]
echo "build-sdl3.sh: SDL3 for the PS5 in ${prefix}"
