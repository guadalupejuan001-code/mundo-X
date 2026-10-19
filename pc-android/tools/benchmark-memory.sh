#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/native
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic native/tests/memory_benchmark.cpp -o build/native/memory_benchmark
# Separate processes keep independent RSS high-water marks. The dense baseline
# deliberately touches 2 GiB; run this on the build host, not on the phone.
build/native/memory_benchmark dense > build/native/memory-dense.json
build/native/memory_benchmark sparse > build/native/memory-sparse.json
python3 - <<'PY'
import json
from pathlib import Path
dense = json.loads(Path('build/native/memory-dense.json').read_text())
sparse = json.loads(Path('build/native/memory-sparse.json').read_text())
assert dense['logical_bytes'] == sparse['logical_bytes'] == 2**31
assert dense['working_set_bytes'] == sparse['working_set_bytes'] == 8 * 2**20
assert dense['hash_fnv1a64'] == sparse['hash_fnv1a64'] == '75186b12a481b7df'
assert dense['payload_bytes'] == 2**31
assert sparse['payload_bytes'] == sparse['working_set_bytes']
assert 0 < sparse['peak_rss_bytes'] < dense['peak_rss_bytes']
report = {'kind': 'synthetic_sparse_working_set', 'game_benchmark': False,
          'dense': dense, 'sparse': sparse}
Path('build/native/memory-benchmark.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
PY
