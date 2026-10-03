#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import hashlib, json
from pathlib import Path
root = Path(__file__).resolve().parent.parent
expected = json.loads((root/'dependencies.json').read_text())['asset_sha256']
actual = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
          for p in sorted((root/'assets').rglob('*')) if p.is_file() and p.name != 'NOTICE.md'}
if actual != expected:
    raise RuntimeError('Shader/model inventory differs from the pinned asset set')
print(f'{len(actual)} shader/model files verified')
