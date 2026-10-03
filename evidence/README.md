# Verified configuration

Radeon 8060S (RADV STRIX_HALO), Mesa 26.2.3, Fedora 44, native x86-64 Linux.
AMD reference: SDK 2.3.0, FSR upscaler 4.1.1 INT8, with the guarded eligibility
and synchronization patches in capture-provider-patch.json. Reference setup
used GE-Proton11-5. The native executable links Vulkan and the normal C++/C
runtime, with no Wine or D3D dependency.

Eight frames per case, deterministic random color/depth/motion data and
matching jitter, reset and sharpening. Every RGBA16F byte matches the DLL
reference and every value is finite. Hashes are in parity.json.

| Input | Output | Model | Result |
| --- | --- | --- | --- |
| 1280x720 | 1920x1080 | Main | Exact |
| 640x360 | 1920x1080 | Ultra Performance | Exact |
| 1067x600 | 1600x900 | Main | Exact |
| 1707x960 | 2560x1440 | Main | Exact |
| 2259x1271 | 3840x2160 | Main | Exact |
| 1280x720 | 3840x2160 | Ultra Performance | Exact |
| 1707x720 | 2560x1080 | Main | Exact |

The same cases pass Khronos validation 1.4.365 with synchronization validation;
see validation.json. The system's older 1.4.341 layer does not understand
SPV_VALVE_mixed_float_dot_product, so the updated layer was built locally.
No system driver or layer was replaced.

Reproduce parity: tools/prepare-reference.sh, then python3 tools/verify.py
(PROTON_ROOT must point to a recent GE-Proton install). Dependency revisions and
all 124 shader/model hashes are pinned in dependencies.json.

These are synthetic correctness checks on one GPU. No gameplay, perceptual
quality, moving-camera, HDR or cross-vendor acceptance is claimed.
