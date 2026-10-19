#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/native
for name in x86_cpu boot_media pc_machine pc_iso; do
  g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic "native/tests/${name}_test.cpp" -o "build/native/${name}_test"
  "build/native/${name}_test"
done
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic native/pc_cli.cpp -o build/native/cosmic_pc_cli
build/native/cosmic_pc_cli --demo --write-demo build/native/cosmic-x86-demo.img
