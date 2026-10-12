#!/usr/bin/env bash
# Builds the title inside the build image, with this repository at /src, the Outsider checkout at /outsider and
# JavaScriptCore's build at /jsc (see build.sh).
#
#   build-in-container.sh [outsider] [link] [launcher]
#
#   outsider   Outsider and SoLoud for the PS5 (CMake, RMMZ_PS5_VULKAN, JavaScriptCore): build/obj/liboutsider.a
#   link       outsider.bin, the program a game runs in (scripts/link-title.sh), in the title folder build/PPSA77110, and the shims
#   launcher   eboot.bin, the game list (launcher/, the PS5 Vulkan Template): the title folder's program, sce_sys, libc.prx
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${PS5_PAYLOAD_SDK:=/opt/ps5-payload-sdk}"
: "${PS5_VULKAN:=/opt/ps5-vulkan}"
: "${SOLOUD:=$(ls -d /opt/*/src/soloud 2>/dev/null | head -1)}"
: "${OUTSIDER_SRC:=/outsider}"
: "${JSC:=/jsc}"
HB="$PS5_PAYLOAD_SDK/target/user/homebrew"
OBJ="$REPO/build/obj"
TITLE="$REPO/build/$(python3 -c 'import json;print(json.load(open("'"$REPO"'/sce_sys/param.json"))["titleId"])')"
mkdir -p "$OBJ"
STEPS=("$@")
[ $# -eq 0 ] && STEPS=(outsider link launcher)

# The SDK's clang wrapper does not search the homebrew prefix that the libraries install into
export CPATH="$HB/include"
export LIBRARY_PATH="$HB/lib"

for step in "${STEPS[@]}"; do
    echo "=== $step ($(date +%H:%M:%S)) ==="
    case "$step" in
        outsider)
            [ -f "$PS5_VULKAN/sdl3/lib/libSDL3.a" ] || { echo "no SDL3 in $PS5_VULKAN/sdl3" >&2; exit 2; }
            [ -f "$JSC/lib/libJavaScriptCore.a" ] || { echo "no JavaScriptCore in $JSC (tools/jsc/build.sh)" >&2; exit 2; }
            # shellcheck source=/dev/null
            source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"
            ${CMAKE} -DCMAKE_BUILD_TYPE=Release \
                     -DRMMZ_PS5=ON -DRMMZ_PS5_VULKAN=ON \
                     -DRMMZ_PS5_PREFIX="$HB" \
                     -DRMMZ_PS5_SDL3_PREFIX="$PS5_VULKAN/sdl3" \
                     -DRMMZ_PS5_SOLOUD_DIR="$SOLOUD" \
                     -DRMMZ_JS_ENGINE=jsc -DRMMZ_JSC_HEADERS="$JSC/JavaScriptCore/Headers" \
                     -B "$REPO/build/outsider" -S "$OUTSIDER_SRC"
            ${MAKE} -C "$REPO/build/outsider" -j"$(nproc)" outsider soloud
            cp "$REPO/build/outsider/liboutsider.a" "$REPO/build/outsider/libsoloud.a" "$OBJ/"
            ;;
        launcher)
            # The game list: launcher/ is the PS5 Vulkan Template's foundation with one program (examples/launcher)
            : "${PS5_RADV:=/opt/ps5-radv}" "${PS5_RADV_SDK:=/opt/ps5-sdk-radv}"
            L="$REPO/launcher"
            export PS5_NATIVE_TOOL="$PS5_RADV/bin/ps5-native-tool"
            export LIBC_PRX="${LIBC_PRX:-/opt/ps5-native/boilerplate/runtime/libc.prx}"
            export PS5_ALLOW_NULL_IMPORTS="mkstemp readlink" # the image's SDK pin predates their platform-layer binding
            cmake -S "$L/ps5" -B "$L/build/ps5" -G Ninja \
                -DCMAKE_TOOLCHAIN_FILE="$PS5_RADV_SDK/toolchain/prospero.cmake" \
                -DPS5_VULKAN_DIR="$PS5_RADV/PS5_Vulkan" -DCMAKE_BUILD_TYPE=Release
            ninja -C "$L/build/ps5" title
            python3 "$L/ps5/tools/build-assets.py" --install "$L/dist/$(basename "$TITLE")"
            mkdir -p "$TITLE"
            cp -a "$L/dist/$(basename "$TITLE")/." "$TITLE/"
            ;;
        link)
            extra=()
            for lib in webp sharpyuv avformat avcodec swscale swresample avutil iconv charset; do
                [ -f "$HB/lib/lib$lib.a" ] && extra+=("$HB/lib/lib$lib.a")
            done
            # JavaScriptCore: its JIT sources are an object subtarget that only the final link takes
            mapfile -t js < <(find "$JSC/Source/JavaScriptCore/CMakeFiles/JavaScriptCoreJIT.dir" -name '*.o' | sort)
            js+=("$JSC/lib/libJavaScriptCore.a" "$JSC/lib/libWTF.a" "$JSC/lib/libbmalloc.a"
                 "$HB/lib/libicui18n.a" "$HB/lib/libicuuc.a" "$HB/lib/libicudata.a")
            bash "$REPO/scripts/link-title.sh" "$TITLE" \
                "$OBJ/liboutsider.a" "$OBJ/libsoloud.a" "$PS5_VULKAN/sdl3/lib/libSDL3.a" "${js[@]}" "${extra[@]}"
            # The shims it loads at run time, as in the Outsider checkout
            rm -rf "$TITLE/outsider"
            mkdir -p "$TITLE/outsider/shims"
            for f in "$OUTSIDER_SRC"/src/shims/*.js; do
                [ "$(basename "$f")" = "control_shim.js" ] && continue
                cp "$f" "$TITLE/outsider/shims/"
            done
            # The notices a binary carries
            mkdir -p "$TITLE/licenses"
            cp -a "$REPO/licenses/." "$TITLE/licenses/"
            cp "$REPO/LICENSE" "$REPO/LEGAL.md" "$REPO/THIRD-PARTY-NOTICES.md" "$TITLE/"
            echo "title ready: $TITLE"
            ;;
        *) echo "unknown step $step" >&2; exit 2 ;;
    esac
done
