#!/usr/bin/env python3
"""Independent DLL-versus-native Vulkan oracle on deterministic color/depth/motion."""
import hashlib, json, os, subprocess
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parent.parent
os.chdir(ROOT)
cases = [('1280x720', '1920x1080', 1), ('640x360', '1920x1080', 4),
         ('1067x600', '1600x900', 1), ('1707x960', '2560x1440', 1),
         ('2259x1271', '3840x2160', 2), ('1280x720', '3840x2160', 4),
         ('1707x720', '2560x1080', 1)]
results = []
for render, output, preset in cases:
    label = f'{render}-{output}'
    ref = ROOT / f'work/capture/output_{output}.raw'
    native = ROOT / f'evidence/native-{label}.raw'
    ref.unlink(missing_ok=True)
    native.unlink(missing_ok=True)
    with open(f'evidence/reference-{label}.log', 'w') as log:
        subprocess.run(['tools/reference.sh', render, output, '8', 'noise'],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    provider = Path('work/capture/fsr4cap.log').read_text()
    if 'provider: 4.1.1' not in provider or '8 frames done' not in provider:
        raise RuntimeError('Reference did not complete FSR 4.1.1')
    Path(f'evidence/provider-verify-{label}.log').write_text(provider)
    env = dict(os.environ, BENCH_NOISE='1', BENCH_DUMP=str(native), BB_FSR4_PROFILE='0')
    with open(f'evidence/verify-native-{label}.log', 'w') as log:
        subprocess.run(['build/native/fsr411-bench', render, output, str(preset), '8'],
                       env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    a, b = native.read_bytes(), ref.read_bytes()
    pixels = np.frombuffer(a, dtype='<f2')
    w, h = map(int, output.split('x'))
    exact = a == b
    finite = bool(np.isfinite(pixels).all())
    shaped = len(a) == len(b) == w*h*8
    result = dict(render=render, output=output, model='m1' if preset==4 else 'm0', frames=8,
                  bit_exact=exact, finite=finite, size_ok=shaped,
                  native_sha256=hashlib.sha256(a).hexdigest(), reference_sha256=hashlib.sha256(b).hexdigest(),
                  half_values=w*h*4)
    results.append(result)
    Path('evidence/parity.json').write_text(json.dumps(results, indent=2)+'\n')
    print(f'{render} -> {output} {result["model"]}: exact={exact}, finite={finite}, shape={shaped}', flush=True)
    if not (exact and finite and shaped):
        raise RuntimeError('Parity verification failed')
print('All seven native Vulkan outputs match the FSR 4.1.1 INT8 reference byte for byte.')
