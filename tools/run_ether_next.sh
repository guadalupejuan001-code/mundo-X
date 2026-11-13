#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
output=${1:-/tmp/ether-next-evidence}
mkdir -p "$output"
if [[ ${ETHER_REQUIRE_ARM64:-0} == 1 && $(uname -m) != aarch64 ]]; then
  echo 'Native AArch64 runner required; emulation is not accepted.' >&2
  exit 1
fi
git rev-parse HEAD > "$output/source-sha.txt"
uname -a > "$output/environment.txt"
"${CXX:-g++}" --version | head -1 >> "$output/environment.txt"
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Werror -I native/include \
  native/tests/ether_next.cpp native/src/ether_transform.cpp native/src/ether_neon.cpp \
  -o "$output/ether-next"
"$output/ether-next" "$output/neon.bin" "$output/raw-neon.bin" | tee "$output/result.json"
"${CXX:-g++}" -std=c++17 -O2 -I native/include \
  native/tests/ether_next_quantum.cpp -o "$output/ether-next-quantum"
"$output/ether-next-quantum" | tee "$output/quantum.txt"
if [[ $(uname -m) == aarch64 ]]; then
  objdump -D -b binary -m aarch64 "$output/neon.bin" > "$output/neon-disassembly.txt"
  objdump -D -b binary -m aarch64 "$output/raw-neon.bin" > "$output/raw-neon-disassembly.txt"
  python3 - "$output/result.json" "$output/neon-disassembly.txt" <<'PY'
import json, pathlib, re, sys
result = json.loads(pathlib.Path(sys.argv[1]).read_text())
assembly = pathlib.Path(sys.argv[2]).read_text()
assert result['native_neon'] and result['native_cases'] == result['equivalence_cases']
assert result['native_iterations'] == 20000 and result['raw_native_cases'] == 24
assert result['raw_neon_arithmetic_per_call'] > result['neon_arithmetic_per_call']
assert result['raw_emitted_words'] > result['emitted_words']
assert result['reduced_operations'] < result['source_operations']
assert re.search(r'\b(add|sub|eor|and|orr|bic|movi)\s+v\d+\.', assembly), assembly
PY
fi
