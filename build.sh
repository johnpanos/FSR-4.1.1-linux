#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
python3 tools/fetch.py Vulkan-Headers
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel "${JOBS:-8}"
