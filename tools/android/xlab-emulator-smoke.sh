#!/usr/bin/env bash
set -euo pipefail

mkdir -p ui-evidence
ADB="$ANDROID_HOME/platform-tools/adb"
AVDMANAGER="$ANDROID_HOME/cmdline-tools/latest/bin/avdmanager"
EMULATOR="$ANDROID_HOME/emulator/emulator"
export ANDROID_USER_HOME="${RUNNER_TEMP:-/tmp}/xlab-android-user"
export ANDROID_AVD_HOME="$ANDROID_USER_HOME/avd"
mkdir -p "$ANDROID_AVD_HOME"
echo no | "$AVDMANAGER" create avd --force --name xlab-test \
  --package 'system-images;android-35;google_apis;x86_64' --device pixel_2 \
  --path "$ANDROID_AVD_HOME/xlab-test.avd"
"$EMULATOR" -list-avds | tee ui-evidence/avds.txt
grep -q '^xlab-test$' ui-evidence/avds.txt
"$EMULATOR" -avd xlab-test -no-window -no-audio -no-boot-anim \
  -gpu swiftshader -no-snapshot -memory 2048 \
  > ui-evidence/emulator.log 2>&1 &
EMULATOR_PID=$!
trap '"$ADB" emu kill >/dev/null 2>&1 || kill "$EMULATOR_PID" 2>/dev/null || true' EXIT
"$ADB" start-server
booted=false
for _ in $(seq 1 120); do
  if ! kill -0 "$EMULATOR_PID" 2>/dev/null; then
    tail -80 ui-evidence/emulator.log
    exit 1
  fi
  if [[ $(timeout 5 "$ADB" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r') == 1 ]]; then
    booted=true
    break
  fi
  sleep 2
done
[[ "$booted" == true ]]
"$ADB" shell input keyevent 82
"$ADB" shell settings put global window_animation_scale 0
"$ADB" shell settings put global transition_animation_scale 0
"$ADB" shell settings put global animator_duration_scale 0
"$ADB" install -r ui-apks/XLab-0.3.2-debug.apk
"$ADB" install -r ui-apks/XLab-instrumentation.apk
set +e
instrument_args=()
instrument_timeout=420
if [[ "${XLAB_LARGE_SCENE_ONLY:-false}" == true ]]; then
  # First-boot services on the Google APIs image compete for CPU and disk.
  # Let them settle before the dedicated large-scene workload.
  sleep 45
  instrument_args=(-e large_scene_only true)
  instrument_timeout=720
fi
timeout "$instrument_timeout" "$ADB" shell am instrument -w -r "${instrument_args[@]}" \
  com.mundox.xlab.test/com.mundox.lab.XLabInstrumentation \
  | tee ui-evidence/instrumentation.txt
run_status=${PIPESTATUS[0]}
set -e
"$ADB" logcat -d > ui-evidence/logcat.txt
"$ADB" pull /sdcard/Android/data/com.mundox.xlab/files/. ui-evidence/ || true
[[ "$run_status" == 0 ]]
grep -q 'INSTRUMENTATION_CODE: -1' ui-evidence/instrumentation.txt
if grep -Eq 'INSTRUMENTATION_RESULT: tests_failed=[1-9]' ui-evidence/instrumentation.txt; then
  exit 1
fi
