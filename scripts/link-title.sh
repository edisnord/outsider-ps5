#!/usr/bin/env bash
# Links the title's program with Outsider (runtime/vk_main.c: Outsider on SDL3's GPU API and RADV) and writes the
# title folder. The link is PS5_Vulkan's for titles that link RADV (its
# tools/build-radv-title.sh and tools/radv-link.sh): its start-up code, the AGC import stubs, the RADV archive linked
# whole, the payload SDK fork's platform layer bound to libc's names, and its converter and signer (ps5-native-tool).
#
#   link-title.sh <title folder> <objects and static libraries...>
#
# Result: <title folder>/outsider.bin, and <title folder>/../outsider.elf.unstripped (with its symbols, for
# llvm-symbolizer).
#
# Environment:
#   PS5_RADV        PS5_Vulkan and RADV, default /opt/ps5-radv
#   PS5_RADV_SDK    the payload SDK fork, default /opt/ps5-sdk-radv
set -euo pipefail

: "${PS5_RADV:=/opt/ps5-radv}"
: "${PS5_RADV_SDK:=/opt/ps5-sdk-radv}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "${HERE}")"
RT="${ROOT}/runtime"
vk="${PS5_RADV}/PS5_Vulkan"
sdk="${PS5_RADV_SDK}"
native="${vk}/tooling/native"
tool="${PS5_RADV}/bin/ps5-native-tool"
archive="${vk}/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"

title="$(realpath -m "$1")"
shift
inputs=()
for a in "$@"; do
    inputs+=("$(realpath "$a")")
done
# libc.prx: the console's libc as a title ships it, the file PS5_Vulkan's titles are built for (its sha256 is in
# PS5_Vulkan's runtime/). The image has it in the ps5-opengl boilerplate; PS5_Vulkan builds it with tools/rebuild-libc.sh.
libc_prx="${LIBC_PRX:-}"
if [[ -z ${libc_prx} ]]; then
    for c in "${vk}/runtime/libc.prx" /opt/ps5-native/boilerplate/runtime/libc.prx; do
        [[ -f ${c} ]] && { libc_prx="${c}"; break; }
    done
fi
[[ -f ${libc_prx} ]] || { echo "link-title.sh: no libc.prx (set LIBC_PRX)" >&2; exit 2; }
[[ "$(sha256sum < "${libc_prx}" | cut -d' ' -f1)" == "$(cut -d' ' -f1 "${vk}/runtime/libc.prx.sha256")" ]] || {
    echo "link-title.sh: ${libc_prx} is not the libc.prx PS5_Vulkan's titles use" >&2; exit 2; }
for file in "${archive}" "${tool}" "${sdk}/bin/prospero-lld"; do
    [[ -e ${file} ]] || { echo "link-title.sh: missing ${file}" >&2; exit 2; }
done
mkdir -p "${title}"

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
mkdir -p "${work}/obj" "${work}/stubs"
cc() { PS5_PAYLOAD_SDK="${sdk}" sh "${vk}/tooling/prospero-clang18" "$@"; }
# Start-up code and C++ runtime of PS5_Vulkan's titles; the hand-off, the run-time glue and the path shim
cc -std=c++20 -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
    -c "${native}/app_crt.cpp" -o "${work}/obj/app_crt.o"
cc -std=c++20 -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
    -c "${native}/app_cpp_runtime.cpp" -o "${work}/obj/app_cpp_runtime.o"
cc -std=gnu11 -O2 -Wall -c "${RT}/vk_main.c" -o "${work}/obj/vk_main.o"
cc -std=gnu11 -O2 -Wall -c "${RT}/runtime.c" -o "${work}/obj/runtime.o"
# PS5PATH_PLATFORM_DIRS: the platform layer's opendir() lists title folders itself, and its DIR* must only
# meet its own readdir()
cc -std=gnu11 -O2 -Wall -DPS5PATH_NATIVE -DPS5PATH_PLATFORM_DIRS -c "${RT}/ps5path.c" \
    -o "${work}/obj/ps5path.o"
cc -std=gnu11 -O2 -Wall -c "${RT}/vk_libc.c" -o "${work}/obj/vk_libc.o"
cc -std=gnu11 -O2 -Wall -c "${RT}/vk_conformance.c" -o "${work}/obj/vk_conformance.o"

# AGC comes from system modules; these link stubs only name the imports (as in build-radv-title.sh)
stub() {
    cc -std=c11 -O2 -fPIC -c "${vk}/$2" -o "${work}/obj/$1_stub.o"
    "${sdk}/bin/prospero-lld" --shared -soname "$1.prx" -o "${work}/stubs/$1.so" "${work}/obj/$1_stub.o"
}
stub libSceAgc vendor/ps5/sdk/stubs/agc_canary_link_stub.c
stub libSceAgcDriver vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c

# shellcheck source=/dev/null
source "${vk}/tools/radv-link.sh"
# The recipe writes a version script into <root>/build, and the image's PS5_Vulkan belongs to root while the
# build runs as the user: give it a root of its own, with PS5_Vulkan's tooling linked in
mkdir -p "${work}/vk-root/build"
ln -s "${vk}/tooling" "${work}/vk-root/tooling"
radv_link_recipe "${work}/vk-root" "${sdk}" "${archive}" || exit 2

wraps=()
# Names the recipe binds to the platform layer (--defsym=<name>=ps5_<name>: opendir, readdir, access, fstatat,
# localtime_r, ...). Wrapping one of them too left ps5path's __real_<name> at address 0 (outsider-vk.bin jumped
# to 0 from ci_find()), so those are not wrapped here: the platform's function serves the name, and
# __real_<name> is bound to it directly for ps5path's own calls.
declare -A platform=()
for flag in "${radv_link_flags[@]}"; do
    [[ ${flag} == --defsym=* ]] || continue
    flag=${flag#--defsym=}
    platform[${flag%%=*}]=${flag#*=}
done
wrap() {
    if [[ -n ${platform[$1]:-} ]]; then
        wraps+=("--defsym=__real_$1=${platform[$1]}")
    else
        wraps+=("--wrap=$1")
    fi
}
# runtime/ps5path.c: paths, and the directory functions of its opendir() fallback. Where the platform layer binds
# the same name (opendir, access, ...), the wrapper reaches the platform's function as __real_<name>.
# Not localtime() and gmtime(): the platform layer's localtime_r() and gmtime_r() (bound to those names by the
# recipe) call them, and ps5path's wrappers of them call localtime_r() and gmtime_r() back, which recursed until
# the stack ran out. ps5path still wraps the _r functions, on top of the platform's.
for sym in $(tr ',' '\n' < "${RT}/wrap-flags.txt" | sed -n 's/^.*--wrap=//p'); do
    case "${sym}" in localtime|gmtime) continue ;; esac
    wrap "${sym}"
done
for sym in readdir readdir_r closedir rewinddir dirfd telldir seekdir; do
    wrap "${sym}"
done
# runtime/runtime.c: the working directory (getcwd() does not work in a title) and ending the title through
# the shell
for sym in getcwd exit _exit _Exit; do
    wrap "${sym}"
done
# runtime/runtime.c: what is printed to stdout and stderr goes to the log buffer, not through a FILE (whose lock a
# flush of the file holds for the whole write)
for sym in fprintf vfprintf printf vprintf fputs puts fwrite fputc putc putchar; do
    wraps+=("--wrap=${sym}")
done
# runtime/vk_main.c: Outsider's log messages also go to the title log and UDP
wraps+=("--wrap=error_handler_log")
# runtime/vk_conformance.c: RADV's conformance version as SDL3's GPU API needs it
wraps+=("--wrap=vk_icdGetInstanceProcAddr")

# The SDK's stub libraries define some of the wrappers' names too (__wrap_mmap, ...), and a name a shared
# library defines would be exported from the program; the converter refuses exports, so they stay local
printf '{\n    local:\n        __wrap_*;\n        __real_*;\n};\n' > "${work}/wraps-local.map"

# Mesa's dispatch tables name entry points RADV does not have through weak references, which stay null and
# must not become imports (as PS5_vkQuake's tools/build-title.sh links)
"${sdk}/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr --strip-debug --error-limit=0 \
    --no-dynamic-linker \
    "${radv_link_flags[@]}" "${wraps[@]}" \
    --version-script "${native}/app-symbols.map" --version-script "${work}/wraps-local.map" --exclude-libs=ALL \
    -e _start -o "${work}/llvm-pie.elf" \
    "${work}/obj/app_crt.o" "${work}/obj/app_cpp_runtime.o" "${work}/obj/vk_main.o" \
    "${work}/obj/runtime.o" "${work}/obj/ps5path.o" "${work}/obj/vk_libc.o" "${work}/obj/vk_conformance.o" \
    --start-group "${inputs[@]}" --end-group \
    "${work}/stubs/libSceAgc.so" "${work}/stubs/libSceAgcDriver.so" \
    "${radv_link_inputs[@]}" \
    --as-needed "${sdk}"/target/lib/*.so
cp "${work}/llvm-pie.elf" "${title}/../outsider.elf.unstripped"

# The converter's settings are build-radv-title.sh's
"${tool}" link --in "${work}/llvm-pie.elf" --out "${work}/outsider.elf" \
    --stub-dir "${sdk}/target/lib" --stub "${work}/stubs/libSceAgc.so" \
    --stub "${work}/stubs/libSceAgcDriver.so" --module-sdk 0x02000009 \
    --companion-sdk 0x08050001 --file-name outsider.elf
"${tool}" self --sign --in "${work}/outsider.elf" --out "${title}/outsider.bin" --magic 0x1D3D154F
"${tool}" self --inspect --file "${title}/outsider.bin" > /dev/null
echo "link-title.sh: ${title}/outsider.bin ($(stat -c %s "${title}/outsider.bin") bytes)"
