#!/usr/bin/env bash
# Builds Mesa's Zink (OpenGL on Vulkan) with RADV for the PS5, in the build image: PS5_Mesa at the revision the
# image's RADV was built from, with zink/ps5-mesa-zink.patch, built as PS5_Vulkan's tools/build-radv.sh builds RADV
# (same cross file, same options, release) plus Zink, Mesa's OpenGL and the patch's EGL frontend.
#
#   build-zink.sh <output folder>
#
# Result in <output folder>:
#   lib/       libvulkan_radeon.a (RADV from the same tree: a program with Zink links this one, not the image's),
#              libzink_ps5.a (Zink without the vk_dispatch_table.c RADV's archive already has), libps5zinkegl.a,
#              and the rest of Mesa's GL
#   include/   EGL, GL and KHR headers
#   PROVENANCE.txt
#
# Environment:
#   PS5_RADV        PS5_Vulkan and RADV, default /opt/ps5-radv
#   PS5_RADV_SDK    the payload SDK fork, default /opt/ps5-sdk-radv
#   ZINK_MESA_REV   PS5_Mesa revision, default the one $PS5_RADV/PS5_Vulkan/tools/build-radv.sh pins
#   PS5_MESA_URL    default https://github.com/mihawk-99/PS5_Mesa.git
#
# Needs, besides the image's cross toolchain: network (the Mesa source and meson's zlib), meson >= 1.4, ninja, and
# for Mesa's build-time OpenCL tools (mesa_clc, vtn_bindgen2) LLVM, Clang, libclc and the SPIR-V translator 19
# (Debian: llvm-19-dev libclang-19-dev libclang-cpp19-dev libclc-19 libclc-19-dev libllvmspirvlib-19-dev
# llvm-spirv-19), python3-mako, python3-yaml, python3-ply, glslang-tools.
set -euo pipefail

: "${PS5_RADV:=/opt/ps5-radv}"
: "${PS5_RADV_SDK:=/opt/ps5-sdk-radv}"
: "${PS5_MESA_URL:=https://github.com/mihawk-99/PS5_Mesa.git}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "${HERE}")"
patch="${ROOT}/zink/ps5-mesa-zink.patch"
vk="${PS5_RADV}/PS5_Vulkan"
out="$(realpath -m "${1:?usage: build-zink.sh <output folder>}")"
work="${out}/work"

if [[ -z ${ZINK_MESA_REV:-} ]]; then
    ZINK_MESA_REV="$(sed -n 's/^mesa_revision=\([0-9a-f]\{40\}\)$/\1/p' "${vk}/tools/build-radv.sh")"
    [[ -n ${ZINK_MESA_REV} ]] || { echo "build-zink.sh: no mesa_revision in ${vk}/tools/build-radv.sh; set ZINK_MESA_REV" >&2; exit 2; }
fi
# The image's RADV and this one must be the same Mesa: the title's other programs link the image's
radv_provenance="${vk}/.deps/native/radv-release/PROVENANCE.txt"
if [[ -f ${radv_provenance} ]] && ! grep -q "^revision: ${ZINK_MESA_REV}$" "${radv_provenance}"; then
    echo "build-zink.sh: warning: the image's RADV is not PS5_Mesa ${ZINK_MESA_REV:0:12} ($(grep '^revision:' "${radv_provenance}"))" >&2
fi

missing=()
for tool in meson ninja git llvm-ar; do
    command -v "${tool}" > /dev/null || missing+=("${tool}")
done
llvm19=/usr/lib/llvm-19/bin
[[ -x ${llvm19}/llvm-config ]] || missing+=("LLVM 19 (${llvm19}/llvm-config)")
command -v llvm-spirv-19 > /dev/null || [[ -x ${llvm19}/llvm-spirv ]] || missing+=("llvm-spirv-19")
python3 -c 'import mako, yaml, ply' 2> /dev/null || missing+=("python3 mako, yaml, ply")
for file in "${patch}" "${vk}/tooling/radv/ps5-cross.ini" "${PS5_RADV_SDK}/bin/prospero-clang"; do
    [[ -e ${file} ]] || missing+=("${file}")
done
if (( ${#missing[@]} )); then
    printf 'build-zink.sh: missing: %s\n' "${missing[@]}" >&2
    exit 2
fi

stamp="${ZINK_MESA_REV} $(sha256sum "${patch}" | cut -d' ' -f1)"
if [[ -f ${out}/PROVENANCE.txt ]] && grep -qx "stamp: ${stamp}" "${out}/PROVENANCE.txt"; then
    echo "build-zink.sh: ${out} is Zink at ${ZINK_MESA_REV:0:12}"
    exit 0
fi
mkdir -p "${work}"

# The source: the pinned revision exported, then patched. A tree from another revision or patch starts again.
mesa="${work}/PS5_Mesa"
src="${work}/src"
if [[ ! -f ${src}/.zink-stamp || $(< "${src}/.zink-stamp") != "${stamp}" ]]; then
    [[ -d ${mesa}/.git ]] || git init -q "${mesa}"
    git -C "${mesa}" cat-file -e "${ZINK_MESA_REV}^{commit}" 2> /dev/null ||
        git -C "${mesa}" fetch -q --depth 1 "${PS5_MESA_URL}" "${ZINK_MESA_REV}"
    rm -rf "${src}" "${work}/build" "${work}/clc-build"
    mkdir -p "${src}"
    git -C "${mesa}" archive "${ZINK_MESA_REV}" | tar -x -C "${src}"
    (cd "${src}" && git init -q && git apply --whitespace=nowarn "${patch}")
    printf '%s\n' "${stamp}" > "${src}/.zink-stamp"
fi

# Host tools of the same revision, for the OpenCL C kernels RADV's geometry shaders run (as build-radv.sh builds them)
clc_build="${work}/clc-build"
clc_bin="${work}/clc-bin"
export PATH="${llvm19}:${PATH}"
if [[ ! -f ${clc_build}/build.ninja ]]; then
    meson setup "${clc_build}" "${src}" -Dbuildtype=release -Dmesa-clc=enabled -Dinstall-mesa-clc=true \
        -Dgallium-drivers= -Dvulkan-drivers= -Dplatforms= -Dglx=disabled -Degl=disabled -Dgbm=disabled \
        -Dopengl=false -Dgles1=disabled -Dgles2=disabled -Dllvm=enabled -Dshared-llvm=enabled \
        -Dbuild-tests=false -Dvalgrind=disabled -Dlibunwind=disabled -Dzstd=disabled -Dxmlconfig=disabled \
        -Dtools= > "${clc_build}.setup.log" 2>&1 || { tail -20 "${clc_build}.setup.log" >&2; exit 1; }
fi
ninja -C "${clc_build}" src/compiler/clc/mesa_clc src/compiler/spirv/vtn_bindgen2 > "${clc_build}.log" 2>&1 ||
    { grep -E "error|FAILED" "${clc_build}.log" | head -20 >&2; exit 1; }
mkdir -p "${clc_bin}"
ln -sf "${clc_build}/src/compiler/clc/mesa_clc" "${clc_bin}/mesa_clc"
ln -sf "${clc_build}/src/compiler/spirv/vtn_bindgen2" "${clc_bin}/vtn_bindgen2"
export PATH="${clc_bin}:${PATH}"

# The console build: build-radv.sh's release options, with Zink and OpenGL added. -Degl stays disabled: the EGL is
# the patch's frontend (ps5egl), not Mesa's.
build="${work}/build"
constants="${work}/cross-constants.ini"
printf "[constants]\nsdk = '%s'\n" "${PS5_RADV_SDK}" > "${constants}"
if [[ ! -f ${build}/build.ninja ]]; then
    meson setup "${build}" "${src}" --cross-file "${constants}" --cross-file "${vk}/tooling/radv/ps5-cross.ini" \
        -Dvulkan-drivers=amd -Dgallium-drivers=zink -Dplatforms= -Dradv-winsys=ps5 \
        -Dllvm=disabled -Damd-use-llvm=false -Dvideo-codecs= \
        -Dbuildtype=debugoptimized -Db_ndebug=true \
        -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dopengl=true -Dgles1=disabled -Dgles2=disabled \
        -Dvalgrind=disabled -Dlibunwind=disabled -Dzstd=disabled -Dzlib=enabled --force-fallback-for=zlib \
        -Dexpat=disabled -Dxmlconfig=disabled -Dshader-cache=enabled -Dbuild-tests=false -Dvulkan-layers= \
        -Dtools= -Dmesa-clc=system -Dgallium-va=disabled -Dgallium-rusticl=false \
        -Dradv-build-id="${ZINK_MESA_REV}" > "${build}.setup.log" 2>&1 || { tail -30 "${build}.setup.log" >&2; exit 1; }
fi
# The archives a program with Zink links (XashPS5's vulkan/build_vulkan.sh and link_xash_zink.py)
archives=(
    src/amd/vulkan/libvulkan_radeon.a
    src/gallium/frontends/ps5egl/libps5zinkegl.a
    src/mesa/libmesa.a
    src/mesa/libmesa_sse41.a
    src/gallium/drivers/zink/libzink.a
    src/gallium/winsys/zink/drm/libzinkwinsys.a
    src/gallium/auxiliary/libgallium.a
    src/gallium/auxiliary/libgalliumvl_stub.a
    src/mesa/glapi/glapi/libglapi_bridge.a
    src/mesa/glapi/shared-glapi/libglapi.a
    src/compiler/glsl/libglsl.a
    src/compiler/glsl/glcpp/libglcpp.a
    src/compiler/glsl/libglsl_util.a
)
ninja -C "${build}" "${archives[@]}" > "${build}.log" 2>&1 || { grep -E "error|FAILED" "${build}.log" | head -30 >&2; exit 1; }

rm -rf "${out:?}/lib" "${out:?}/include"
mkdir -p "${out}/lib" "${out}/include"
# Meson makes thin archives (members left in the build tree) of the libraries it does not install: each is copied
# out as a full one. Zink's loses its own copy of Mesa's Vulkan dispatch table, which RADV's archive (linked whole)
# already holds.
export_archive() { # <archive in the build> <archive out> [regex of the members to leave out]
    local from="${build}/$1" to=$2 drop=${3:-'^$'}
    if [[ $(head -c 7 "${from}") == '!<thin>' ]]; then
        (cd "$(dirname "${from}")" && llvm-ar t "${from}" | { grep -v -- "${drop}" || true; } | xargs llvm-ar qcS "${to}")
        llvm-ar s "${to}"
    else
        cp "${from}" "${to}"
        llvm-ar t "${to}" | { grep -- "${drop}" || true; } | xargs -r llvm-ar d "${to}"
    fi
}
for a in "${archives[@]}"; do
    case "${a}" in
        */libzink.a) export_archive "${a}" "${out}/lib/libzink_ps5.a" 'vk_dispatch_table\.c\.o$' ;;
        *) export_archive "${a}" "${out}/lib/$(basename "${a}")" ;;
    esac
done
cp -r "${src}/include/EGL" "${src}/include/GL" "${src}/include/KHR" "${out}/include/"

cat > "${out}/PROVENANCE.txt" << PROV
Mesa Zink and RADV for the PlayStation 5, built by scripts/build-zink.sh
fork: ${PS5_MESA_URL}
revision: ${ZINK_MESA_REV}
patch: zink/ps5-mesa-zink.patch, sha256 $(sha256sum "${patch}" | cut -d' ' -f1)
sdk: ${PS5_RADV_SDK}
stamp: ${stamp}
PROV
echo "build-zink.sh: Zink and RADV at ${ZINK_MESA_REV:0:12} in ${out}"
