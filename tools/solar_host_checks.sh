#!/usr/bin/env bash
# Real Solar Java + shared JNI; no replacement core. Run from the repo root.
set -euo pipefail
solar_work="${SOLAR_HOST_WORK:-build/solar-host}"
solar_java="${JAVA_HOME:?Set JAVA_HOME to JDK 17}"
mkdir -p "$solar_work/classes" "$solar_work/jni" "$solar_work/reference"
git show af0b1797573b55a739dcaef7b784eb98825ff07c:solar-android/app/src/main/java/com/mundox/solar/SolarFlightPhysics.java \
  | sed 's/SolarFlightPhysics/LegacySolarFlightPhysics/g' > "$solar_work/reference/LegacySolarFlightPhysics.java"
"$solar_java/bin/javac" -encoding UTF-8 -d "$solar_work/classes" \
  solar-android/app/src/main/java/com/mundox/solar/{SolarData,SolarEnvironment,SolarFlightPhysics,SolarVisualEther,QuantumSolarBridge}.java \
  graphics-android/app/src/main/java/com/mundox/graphics/{NativeCore,EtherCoreNative,InfiniteProgram}.java \
  solar-android/tests/{SolarDataChecks,SolarVisualEtherChecks,SolarOptimizationChecks,SolarBridgeChecks,SolarPhysicsComparison,SolarRenderFixtures,SolarOcclusion}.java \
  "$solar_work/reference/LegacySolarFlightPhysics.java"
"${CXX:-g++}" -std=c++17 -O3 -shared -fPIC -pthread -I native/include \
  -I "$solar_java/include" -I "$solar_java/include/linux" \
  native/src/graphics_core_bridge.cpp native/src/infinite_bridge.cpp \
  graphics-android/app/src/main/cpp/graphics_jni.cpp -o "$solar_work/jni/libmx_graphics.so"
for solar_check in SolarDataChecks SolarVisualEtherChecks SolarOptimizationChecks SolarPhysicsComparison SolarBridgeChecks; do
  "$solar_java/bin/java" -Xms32m -Xmx256m -XX:+UseSerialGC -XX:ActiveProcessorCount=1 \
    -XX:CompressedClassSpaceSize=64m -XX:ReservedCodeCacheSize=64m \
    -Djava.library.path="$solar_work/jni" -cp "$solar_work/classes" "com.mundox.solar.$solar_check"
done
