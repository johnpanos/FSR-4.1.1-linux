#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
# Leave GPU selection to the user's Vulkan loader unless explicitly overridden.
exec build/native/fsr411-bench "$@"
