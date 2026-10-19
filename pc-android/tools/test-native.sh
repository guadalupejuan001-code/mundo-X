#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/native
for name in sparse_ram x86_cpu x86_long_mode x86_alu64_oracle boot_media pc_machine pc_iso pc_memory pc_video; do
  g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic "native/tests/${name}_test.cpp" -o "build/native/${name}_test"
  if "build/native/${name}_test"; then
    :
  else
    pc_test_status=$?
    if [[ "$name" != x86_alu64_oracle || "$pc_test_status" != 77 ]]; then exit "$pc_test_status"; fi
  fi
done
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic native/pc_cli.cpp -o build/native/cosmic_pc_cli
build/native/cosmic_pc_cli --demo --write-demo build/native/cosmic-x86-demo.img
build/native/cosmic_pc_cli --demo64 --ram-mib 2048 --resident-mib 1024 --write-demo build/native/cosmic-x64-demo.img
