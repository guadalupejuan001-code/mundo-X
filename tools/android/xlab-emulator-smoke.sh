#!/usr/bin/env bash
set -euo pipefail

mkdir -p ui-evidence
ADB="$ANDROID_HOME/platform-tools/adb"
AVDMANAGER="$ANDROID_HOME/cmdline-tools/latest/bin/avdmanager"
EMULATOR="$ANDROID_HOME/emulator/emulator"
echo no | "$AVDMANAGER" create avd --force --name xlab-test \
  --package 'system-images;android-35;google_apis;x86_64' --device pixel_2
"$EMULATOR" -avd xlab-test -no-window -no-audio -no-boot-anim \
  -gpu swiftshader_indirect -no-snapshot -memory 2048 \
  > ui-evidence/emulator.log 2>&1 &
EMULATOR_PID=$!
trap '"$ADB" emu kill >/dev/null 2>&1 || kill "$EMULATOR_PID" 2>/dev/null || true' EXIT
timeout 180 "$ADB" wait-for-device
booted=false
for _ in $(seq 1 120); do
  if [[ $("$ADB" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r') == 1 ]]; then
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
"$ADB" install -r ui-apks/XLab-0.2.0-debug.apk
"$ADB" install -r ui-apks/XLab-instrumentation.apk
set +e
timeout 240 "$ADB" shell am instrument -w \
  com.mundox.lab.xlab.test/com.mundox.lab.XLabInstrumentation \
  | tee ui-evidence/instrumentation.txt
run_status=${PIPESTATUS[0]}
set -e
"$ADB" logcat -d > ui-evidence/logcat.txt
"$ADB" pull /sdcard/Android/data/com.mundox.lab.xlab/files/. ui-evidence/ || true
[[ "$run_status" == 0 ]]
rg -q 'INSTRUMENTATION_CODE: -1' ui-evidence/instrumentation.txt
if rg -q 'INSTRUMENTATION_RESULT: tests_failed=[1-9]' ui-evidence/instrumentation.txt; then
  exit 1
fi
