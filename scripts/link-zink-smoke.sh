#!/usr/bin/env bash
# Links zink-smoke.bin (zink/zink_smoke.c): OpenGL through Zink on RADV, from scripts/build-zink.sh's archives. The
# link is link-title.sh's (PS5_Vulkan's recipe for titles that link RADV) without Outsider's wraps, with Mesa's GL
# on top as XashPS5 links it (its vulkan/link_xash_zink.py).
#
#   link-zink-smoke.sh <title folder> <build-zink.sh's output folder>
#
# Result: <title folder>/zink-smoke.bin, and <title folder>/../zink-smoke.elf.unstripped.
#
# Environment: PS5_RADV, PS5_RADV_SDK as for link-title.sh.
set -euo pipefail

: "${PS5_RADV:=/opt/ps5-radv}"
: "${PS5_RADV_SDK:=/opt/ps5-sdk-radv}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "${HERE}")"
vk="${PS5_RADV}/PS5_Vulkan"
sdk="${PS5_RADV_SDK}"
native="${vk}/tooling/native"
tool="${PS5_RADV}/bin/ps5-native-tool"
title="$(realpath -m "$1")"
zink="$(realpath "$2")"
lib="${zink}/lib"
# RADV from the same Mesa tree as Zink, not the image's
archive="${lib}/libvulkan_radeon.a"
for file in "${archive}" "${lib}/libzink_ps5.a" "${lib}/libps5zinkegl.a" "${tool}" "${sdk}/bin/prospero-lld"; do
    [[ -e ${file} ]] || { echo "link-zink-smoke.sh: missing ${file}" >&2; exit 2; }
done
mkdir -p "${title}"

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
mkdir -p "${work}/obj" "${work}/stubs"
cc() { PS5_PAYLOAD_SDK="${sdk}" sh "${vk}/tooling/prospero-clang18" "$@"; }
cc -std=c++20 -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
    -c "${native}/app_crt.cpp" -o "${work}/obj/app_crt.o"
cc -std=c++20 -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
    -c "${native}/app_cpp_runtime.cpp" -o "${work}/obj/app_cpp_runtime.o"
# EGL_NO_X11: eglplatform.h would otherwise take the console (a __unix__ target) for X11
cc -std=gnu11 -O2 -Wall -DEGL_NO_X11 -I"${zink}/include" -c "${ROOT}/zink/zink_smoke.c" -o "${work}/obj/zink_smoke.o"

stub() {
    cc -std=c11 -O2 -fPIC -c "${vk}/$2" -o "${work}/obj/$1_stub.o"
    "${sdk}/bin/prospero-lld" --shared -soname "$1.prx" -o "${work}/stubs/$1.so" "${work}/obj/$1_stub.o"
}
stub libSceAgc vendor/ps5/sdk/stubs/agc_canary_link_stub.c
stub libSceAgcDriver vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c

# shellcheck source=/dev/null
source "${vk}/tools/radv-link.sh"
mkdir -p "${work}/vk-root/build"
ln -s "${vk}/tooling" "${work}/vk-root/tooling"
radv_link_recipe "${work}/vk-root" "${sdk}" "${archive}" || exit 2

mesa=(
    "${lib}/libps5zinkegl.a" "${lib}/libmesa.a" "${lib}/libzink_ps5.a" "${lib}/libzinkwinsys.a"
    "${lib}/libgallium.a" "${lib}/libglapi_bridge.a" "${lib}/libglapi.a" "${lib}/libglsl.a" "${lib}/libglcpp.a"
    "${lib}/libglsl_util.a" "${lib}/libmesa_sse41.a" "${lib}/libgalliumvl_stub.a"
)
printf '{\n    local:\n        __wrap_*;\n        __real_*;\n        ps5_klog_capture_stderr;\n};\n' > "${work}/local.map"

# -u _ZTH23_mesa_glapi_tls_Context: the C++ TLS wrapper glapi names, which the patch's frontend defines empty; the
# converter refuses an undefined one (XashPS5's finding)
"${sdk}/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr --strip-debug --error-limit=0 \
    --no-dynamic-linker \
    "${radv_link_flags[@]}" \
    --version-script "${native}/app-symbols.map" --version-script "${work}/local.map" --exclude-libs=ALL \
    -e _start -u _ZTH23_mesa_glapi_tls_Context -o "${work}/llvm-pie.elf" \
    "${work}/obj/app_crt.o" "${work}/obj/app_cpp_runtime.o" "${work}/obj/zink_smoke.o" \
    --start-group "${mesa[@]}" --end-group \
    "${work}/stubs/libSceAgc.so" "${work}/stubs/libSceAgcDriver.so" \
    "${radv_link_inputs[@]}" \
    --as-needed "${sdk}"/target/lib/*.so
cp "${work}/llvm-pie.elf" "${title}/../zink-smoke.elf.unstripped"

"${tool}" link --in "${work}/llvm-pie.elf" --out "${work}/zink-smoke.elf" \
    --stub-dir "${sdk}/target/lib" --stub "${work}/stubs/libSceAgc.so" \
    --stub "${work}/stubs/libSceAgcDriver.so" --module-sdk 0x02000009 \
    --companion-sdk 0x08050001 --file-name zink-smoke.elf
"${tool}" self --sign --in "${work}/zink-smoke.elf" --out "${title}/zink-smoke.bin" --magic 0x1D3D154F
"${tool}" self --inspect --file "${title}/zink-smoke.bin" > /dev/null
echo "link-zink-smoke.sh: ${title}/zink-smoke.bin ($(stat -c %s "${title}/zink-smoke.bin") bytes)"
