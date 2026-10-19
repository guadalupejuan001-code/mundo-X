#!/usr/bin/env bash
set -euo pipefail
mkdir -p micro-evidence
ADB="$ANDROID_HOME/platform-tools/adb"
export ANDROID_USER_HOME="${RUNNER_TEMP:-/tmp}/micro-android-user"
export ANDROID_AVD_HOME="$ANDROID_USER_HOME/avd"
mkdir -p "$ANDROID_AVD_HOME"
echo no | "$ANDROID_HOME/cmdline-tools/latest/bin/avdmanager" create avd --force --name micro-test \
  --package 'system-images;android-35;google_apis;x86_64' --device pixel_2 --path "$ANDROID_AVD_HOME/micro-test.avd"
"$ANDROID_HOME/emulator/emulator" -avd micro-test -no-window -no-audio -no-boot-anim \
  -gpu swiftshader -no-snapshot -memory 2048 > micro-evidence/emulator.log 2>&1 &
EMULATOR_PID=$!
trap '"$ADB" emu kill >/dev/null 2>&1 || kill "$EMULATOR_PID" 2>/dev/null || true' EXIT
"$ADB" start-server
booted=false
for _ in $(seq 1 120); do
  kill -0 "$EMULATOR_PID" 2>/dev/null || { tail -80 micro-evidence/emulator.log; exit 1; }
  if [[ $(timeout 5 "$ADB" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r') == 1 ]]; then booted=true; break; fi
  sleep 2
done
[[ "$booted" == true ]]
"$ADB" shell input keyevent 82
"$ADB" shell settings put global window_animation_scale 0
"$ADB" shell settings put global transition_animation_scale 0
"$ADB" shell settings put global animator_duration_scale 0
"$ADB" install -r micro-apks/Mundo-X-Micro-preview.apk
"$ADB" install -r micro-apks/Micro-instrumentation.apk
set +e
timeout 360 "$ADB" shell am instrument -w -r com.mundox.micropreview.test/com.mundox.lab.MicroLabInstrumentation | tee micro-evidence/instrumentation.txt
status=${PIPESTATUS[0]}
set -e
"$ADB" logcat -d > micro-evidence/logcat.txt
"$ADB" pull /sdcard/Android/data/com.mundox.micropreview/files/. micro-evidence/ || true
[[ "$status" == 0 ]]
grep -q 'INSTRUMENTATION_CODE: -1' micro-evidence/instrumentation.txt
! grep -Eq 'INSTRUMENTATION_RESULT: tests_failed=[1-9]' micro-evidence/instrumentation.txt
