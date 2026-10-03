#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Fetch only named, revision-pinned build dependencies."""
import json, subprocess, sys
from pathlib import Path
root = Path(__file__).resolve().parent.parent
pins = json.loads((root/'dependencies.json').read_text())['dependencies']
for name in sys.argv[1:]:
    pin = pins[name]
    path = root/'deps'/name
    if not path.exists():
        path.mkdir(parents=True)
        subprocess.run(['git', 'init', str(path)], check=True)
        subprocess.run(['git', '-C', str(path), 'remote', 'add', 'origin', pin['url']], check=True)
        subprocess.run(['git', '-C', str(path), 'fetch', '--depth', '1', 'origin', pin['commit']], check=True)
        subprocess.run(['git', '-C', str(path), 'checkout', '--detach', pin['commit']], check=True)
    actual = subprocess.check_output(['git', '-C', str(path), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != pin['commit']:
        raise RuntimeError(f'{name}: expected {pin["commit"]}, found {actual}')
    if name == 'dxil-spirv':
        subprocess.run(['git', '-C', str(path), 'submodule', 'update', '--init', '--recursive'], check=True)
