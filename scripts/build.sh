#!/usr/bin/env bash
# Builds the title in the build image.
#
#   scripts/build.sh [outsider] [link] [launcher] [zink] [zink-smoke]   (steps: build-in-container.sh)
#
# Environment:
#   IMAGE          the build image (default outsider-ps5-dev: PS5 SDK, PS5_Vulkan with RADV and SDL3, SoLoud)
#   OUTSIDER_SRC   the Outsider checkout, the fork's jsc branch (default ../outsider)
#   JSC_OUT        JavaScriptCore's build (default build/jsc/out)
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${IMAGE:-outsider-ps5-dev}"
OUTSIDER_SRC="$(realpath "${OUTSIDER_SRC:-$REPO/../outsider}")"
JSC_OUT="$(realpath "${JSC_OUT:-$REPO/build/jsc/out}")"
exec docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$REPO:/src" -v "$OUTSIDER_SRC:/outsider:ro" -v "$JSC_OUT:/jsc:ro" \
    "$IMAGE" bash /src/scripts/build-in-container.sh "$@"
