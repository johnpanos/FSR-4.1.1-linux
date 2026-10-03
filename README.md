# FSR 4.1.1 on Linux

FSR 4.1.1 INT8 upscaling in a native Linux Vulkan program. No Wine, Proton or
DirectX is used at runtime. The prepared shaders and model weights are included.

The Vulkan implementation comes from [bbport](https://github.com/deadinside28/bloodborne_pc).
This repo extracts its upscaler and benchmark into a standalone build.

## Build and run

Needs a C++20 compiler, CMake, Ninja, Python 3, Git and Vulkan development files.
The build fetches a pinned revision of Vulkan Headers.

```sh
git clone https://github.com/johnpanos/FSR-4.1.1-linux.git
cd FSR-4.1.1-linux
./build.sh
./run.sh 1280x720 1920x1080 1 300
```

Arguments: input size, output size, preset (0–4), frame count (1–600).
Presets 0–3 use the main INT8 model; 4 uses Ultra Performance. Sizes are explicit;
the preset argument does not calculate them. Defaults: 1280x720 → 1920x1080,
preset 1, eight frames.

The test is headless. It prints GPU timings and can save the output:

```sh
BENCH_NOISE=1 BENCH_DUMP=output.raw ./run.sh 1280x720 1920x1080 1 8
```

`output.raw` is tightly packed RGBA16F. `BENCH_NOISE=1` selects deterministic
random color, depth and motion inputs; otherwise the input is a uniform frame.
Timings exclude startup compilation and discard up to 32 warmup frames. These
are synthetic upscaler timings, not game FPS.

## Hardware

Tested on Radeon 8060S (Strix Halo), RADV/Mesa 26.2.3. The current shader set
needs Vulkan 1.3, FP16/INT8/INT16 shader arithmetic, integer dot products,
formatless storage-image writes, and these extensions:

- `VK_KHR_push_descriptor`
- `VK_KHR_compute_shader_derivatives` (linear derivative groups)
- `VK_VALVE_shader_mixed_float_dot_product` (FP16 inputs, FP32 accumulation)

The last extension is recent. The installed driver and validation layer must
both know it. Other GPUs and drivers are untested; there is no automatic fallback.
Output is limited to 3840x2160.

## Verification

Seven configurations, both INT8 models, eight frames each: native output
matches the AMD DLL reference byte for byte. Cases include 1080p, 1440p, 4K,
ultrawide and input sizes not divisible by eight. All values are finite.
Khronos validation 1.4.365, including synchronization validation, passes those
same cases. Results and hashes are in [evidence/](evidence/).

This verifies the tested synthetic dispatches. It does not establish image
quality in games, moving-scene stability, HDR behavior or support on other GPUs.

## Integration

`src/fsr411.h` exposes `Fsr411::Upscaler::Record`. It records into a caller-owned
Vulkan command buffer using color, depth and motion images, an output image,
jitter, exposure and reset state. See the header for formats and lifetime rules.
The caller must finish GPU work before destroying the upscaler, replacing its
output size/model, or reusing an in-flight constant-buffer slot.

## Rebuild assets / compare with AMD's DLL

Normal builds do not need this. Asset extraction and the independent reference
use a recent GE-Proton installation, MinGW GCC, SPIRV-Tools, NumPy and the pinned
AMD SDK DLLs fetched from AMD's public repository. All Wine state stays in work/.

```sh
export PROTON_ROOT=/path/to/GE-Proton
./tools/rebuild-assets.sh
python3 tools/verify.py
```

The reference DLL receives guarded patches for INT8 hardware eligibility and
buffer-UAV synchronization. Its shader arithmetic and model weights stay
original. Pins, asset hashes and patch provenance are recorded in
[dependencies.json](dependencies.json) and [NOTICE.md](NOTICE.md).

## License and status

Unofficial, experimental, not endorsed by AMD. This is the INT8 upscaler;
there is no FP8 path, frame generation or ray reconstruction here.

Runtime and harness: **GPL-2.0-or-later**, inherited from bbport. AMD shader/model
assets: AMD's **MIT exception for the upscaler DLL**, with the complete SDK notice
retained. Other SDK files keep their own terms. Read [NOTICE.md](NOTICE.md) and
[LICENSE](LICENSE) before integrating or redistributing. Supplied without warranty.
