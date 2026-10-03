#!/usr/bin/env python3
"""Prepare a private reference DLL for the unsupported 8060S; shaders stay original.
Patch locations/guards from bc250-fsr4-fork dll/repack_dll.py (MIT new-code).
"""
from pathlib import Path
import hashlib, json
source = Path('deps/FidelityFX-SDK/Kits/FidelityFX/signedbin/amd_fidelityfx_upscaler_dx12.dll')
data = bytearray(source.read_bytes())
original = hashlib.sha256(data).hexdigest()
if original != 'd0dcccc74a43c44ba435b7a369b456e0970d8a4464e4bd683119b374f2c9fb46':
    raise RuntimeError('Unexpected AMD SDK DLL: refusing to patch')
patches = [(0x8189, '817c', 'c744'),
           (0x8191, '440fb6c075238b5424308d4aff83f90e', 'c74424300100000041b801000000eb15'),
           (0x4782, '41f686d80b000001', '41f686d80b000000')]
for offset, before, after in patches:
    old, new = bytes.fromhex(before), bytes.fromhex(after)
    if data[offset:offset+len(old)] != old or len(old) != len(new):
        raise RuntimeError('Unexpected instruction bytes')
    data[offset:offset+len(new)] = new
out = Path('work/capture/amd_fidelityfx_upscaler_dx12.dll')
out.write_bytes(data)
record = dict(original_sha256=original, patched_sha256=hashlib.sha256(data).hexdigest(),
              patches=[dict(offset=off, before=a, after=b) for off,a,b in patches],
              purpose='INT8 eligibility and buffer-UAV synchronization only; no shader or model edits')
Path('evidence/capture-provider-patch.json').write_text(json.dumps(record, indent=2)+'\n')
print(record['patched_sha256'])
