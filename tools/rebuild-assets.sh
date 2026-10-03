#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
tools/prepare-reference.sh
for sizes in '1280x720 1920x1080' '640x360 1920x1080' '1707x960 2560x1440' '853x480 2560x1440'; do
    read -r render output <<< "$sizes"
    tools/reference.sh "$render" "$output" 3
    cp work/capture/fsr4cap.log "evidence/provider-$render-$output.log"
done
python3 tools/extract.py build/dxil-spirv/dxil-spirv work/capture assets
python3 tools/check-assets.py
