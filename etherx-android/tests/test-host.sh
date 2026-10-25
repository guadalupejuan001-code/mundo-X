#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/host
javac -encoding UTF-8 -d build/host \
 app/src/main/java/com/etherx/filelab/core/{EtherCodec,FileInspector,QuantumToyLab,DeepInspector}.java tests/{EtherCodecTest,DeepInspectorTest}.java
java -cp build/host EtherCodecTest "$@"
java -cp build/host DeepInspectorTest
