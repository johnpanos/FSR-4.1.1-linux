#!/usr/bin/env bash
# Rebuild the asset extractor and its isolated DLL oracle. Not needed for native use.
set -euo pipefail
cd "$(dirname "$0")/.."
: "${PROTON_ROOT:?Set PROTON_ROOT to a recent GE-Proton installation}"
python3 tools/fetch.py FidelityFX-SDK dxil-spirv
if git -C deps/dxil-spirv apply --check ../../tools/dxil-spirv-class-bindings.patch 2>/dev/null; then
    git -C deps/dxil-spirv apply ../../tools/dxil-spirv-class-bindings.patch
else
    git -C deps/dxil-spirv apply --reverse --check ../../tools/dxil-spirv-class-bindings.patch
fi
cmake -S deps/dxil-spirv -B build/dxil-spirv -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/dxil-spirv --target dxil-spirv --parallel "${JOBS:-8}"
mkdir -p work/capture evidence
sdk=deps/FidelityFX-SDK/Kits/FidelityFX
x86_64-w64-mingw32-gcc -std=c11 -O1 -Wall -I"$sdk/api/include" -I"$sdk/upscalers/include" \
    tools/fsr4cap.c tools/capture.c tools/rootsig.c -o work/capture/fsr4cap.exe -ld3d12 -ldxguid -static
cp "$sdk/signedbin/amd_fidelityfx_loader_dx12.dll" work/capture/
python3 tools/patch_capture_provider.py
if [[ ! -d work/prefix/drive_c ]]; then
    WINEPREFIX="$PWD/work/prefix" WINEDEBUG=-all "$PROTON_ROOT/files/bin/wine" wineboot -u
fi
cp "$PROTON_ROOT"/files/lib/wine/vkd3d-proton/x86_64-windows/d3d12*.dll work/prefix/drive_c/windows/system32/
cp "$PROTON_ROOT/files/lib/wine/dxvk/x86_64-windows/dxgi.dll" work/prefix/drive_c/windows/system32/
