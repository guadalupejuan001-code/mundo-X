#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/native
for name in sparse_ram x86_cpu x86_cpu_profile x86_long_mode x86_alu64_oracle x86_enter_oracle boot_media windows_iso_probe pc_machine pc_iso pc_memory pc_memory_fabric pc_video pc_quantum; do
  g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -I../native/include "native/tests/${name}_test.cpp" -o "build/native/${name}_test"
  if "build/native/${name}_test"; then
    :
  else
    pc_test_status=$?
    if [[ "$pc_test_status" != 77 || ( "$name" != x86_alu64_oracle && "$name" != x86_enter_oracle ) ]]; then exit "$pc_test_status"; fi
  fi
done
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -I../native/include native/pc_cli.cpp -o build/native/cosmic_pc_cli
build/native/cosmic_pc_cli --demo --write-demo build/native/cosmic-x86-demo.img
build/native/cosmic_pc_cli --demo64 --ram-mib 2048 --resident-mib 1024 --write-demo build/native/cosmic-x64-demo.img
build/native/cosmic_pc_cli --demo-quantum --write-demo build/native/cosmic-quantum-x86.img
build/native/cosmic_pc_cli --demo-quantum64 --write-demo build/native/cosmic-quantum-x64.img
