#!/usr/bin/env bash
# Asset preparation/reference oracle only. The native bench never calls this script.
set -euo pipefail
cd "$(dirname "$0")/.."
: "${PROTON_ROOT:?Set PROTON_ROOT to a recent GE-Proton installation}"
export WINEPREFIX="$PWD/work/prefix" WINEDEBUG=-all VKD3D_DEBUG=none
export WINEDLLOVERRIDES='d3d12,d3d12core,dxgi=n'
export VK_DRIVER_FILES="${VK_DRIVER_FILES:-/usr/share/vulkan/icd.d/radeon_icd.x86_64.json}"
export DXVK_LOG_LEVEL=error VKD3D_SHADER_CACHE_PATH="$PWD/work/capture"
exec "$PROTON_ROOT/files/bin/wine" "$PWD/work/capture/fsr4cap.exe" 4.1.1 "$@"
