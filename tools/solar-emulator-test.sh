#!/usr/bin/env bash
# Install and test the exact signed APK on Android 15 using software GLES.
set -euo pipefail

solar_sdk="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
[[ -n "$solar_sdk" ]] || { echo 'ANDROID_HOME or ANDROID_SDK_ROOT is required.' >&2; exit 1; }
solar_adb="$solar_sdk/platform-tools/adb"
solar_avdmanager="$solar_sdk/cmdline-tools/latest/bin/avdmanager"
solar_emulator="$solar_sdk/emulator/emulator"
solar_serial=emulator-5554
solar_apks="${SOLAR_APK_DIR:-solar-apks}"
solar_evidence="${SOLAR_EVIDENCE_DIR:-solar-evidence}"
solar_uhd="${SOLAR_UHD4K:-false}"
solar_8k="${SOLAR_UHD8K:-false}"
solar_package=com.cosmicx.solar
solar_apk=COSMIC-X-Solar.apk
solar_tests=COSMIC-X-Solar-tests.apk
solar_instrument_timeout=1000
solar_instrument_args=()
solar_verify_args=()
if [[ "$solar_8k" == true ]]; then
  solar_uhd=true
  solar_package=com.cosmicx.solar.ultra
  solar_apk=COSMIC-X-Solar-8K.apk
  solar_tests=COSMIC-X-Solar-8K-tests.apk
  solar_instrument_timeout=2400
  solar_instrument_args=(-e uhd4k true -e uhd8k true)
  solar_verify_args=(--uhd8k)
elif [[ "$solar_uhd" == true ]]; then
  solar_package=com.cosmicx.solar.uhd
  solar_apk=COSMIC-X-Solar-4K.apk
  solar_tests=COSMIC-X-Solar-4K-tests.apk
  solar_instrument_timeout=1800
  solar_instrument_args=(-e uhd4k true)
  solar_verify_args=(--uhd4k)
fi
mkdir -p "$solar_evidence"
solar_evidence="$(realpath "$solar_evidence")"

python3 - "$solar_apks" "$solar_uhd" "$solar_8k" <<'PY'
import hashlib
import subprocess
import sys
from pathlib import Path
directory = Path(sys.argv[1])
expected_names = {'COSMIC-X-Solar.apk', 'COSMIC-X-Solar-tests.apk'}
if sys.argv[3] == 'true':
    expected_names = {'COSMIC-X-Solar-8K.apk', 'COSMIC-X-Solar-8K-tests.apk', 'COSMIC-X-Solar-coexist.apk'}
elif sys.argv[2] == 'true':
    expected_names = {'COSMIC-X-Solar-4K.apk', 'COSMIC-X-Solar-4K-tests.apk', 'COSMIC-X-Solar-coexist.apk'}
found = set()
for row in (directory / 'SHA256SUMS.txt').read_text().splitlines():
    expected, name = row.split()
    if name not in expected_names or name in found:
        raise SystemExit('Unexpected APK checksum entry: ' + name)
    if hashlib.sha256((directory / name).read_bytes()).hexdigest() != expected:
        raise SystemExit('APK checksum mismatch: ' + name)
    found.add(name)
if found != expected_names:
    raise SystemExit('Missing APK checksums')
head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
if (directory / 'build-commit.txt').read_text().strip() != head:
    raise SystemExit('APK source commit differs from the checked validation source')
print('All signed APKs match build checksums and the exact checked source.')
PY
cp "$solar_apks/SHA256SUMS.txt" "$solar_evidence/apk-SHA256SUMS.txt"
cp "$solar_apks/build-commit.txt" "$solar_evidence/build-commit.txt"
cp "$solar_apks/build-provenance.json" "$solar_evidence/build-provenance.json"

# Each run gets isolated emulator configuration and an empty application sandbox.
export ANDROID_USER_HOME="${RUNNER_TEMP:-/tmp}/cosmic-solar-android"
export ANDROID_AVD_HOME="$ANDROID_USER_HOME/avd"
mkdir -p "$ANDROID_AVD_HOME"
printf 'no\n' | "$solar_avdmanager" create avd --force --name cosmic-solar \
  --package 'system-images;android-35;google_apis;x86_64' --device pixel_2 \
  --path "$ANDROID_AVD_HOME/cosmic-solar.avd"

"$solar_emulator" -avd cosmic-solar -port 5554 -no-window -no-audio -no-boot-anim \
  -gpu swiftshader -no-snapshot -memory 2048 -cores 2 \
  > "$solar_evidence/emulator.log" 2>&1 &
solar_emulator_pid=$!
solar_collected=false

solar_collect() {
  # Keep the first evidence snapshot: a second EXIT collection can overwrite
  # an already complete archive or screenshot after the app has terminated.
  [[ "$solar_collected" == false ]] || return 0
  solar_collected=true
  timeout 15 "$solar_adb" -s "$solar_serial" logcat -d > "$solar_evidence/logcat.txt" 2>&1 || true
  timeout 15 "$solar_adb" -s "$solar_serial" shell dumpsys meminfo "$solar_package" \
    > "$solar_evidence/app-memory.txt" 2>&1 || true
  timeout 15 "$solar_adb" -s "$solar_serial" exec-out screencap -p \
    > "$solar_evidence/device-screen.png" 2>/dev/null || true
  if timeout 60 "$solar_adb" -s "$solar_serial" exec-out run-as "$solar_package" \
      tar -C files -cf - solar-evidence > "$solar_evidence/runtime-evidence.tar" \
      2> "$solar_evidence/evidence-pull.log"; then
    tar -xf "$solar_evidence/runtime-evidence.tar" -C "$solar_evidence" --strip-components=1 || true
  fi
}
solar_cleanup() {
  solar_collect
  timeout 10 "$solar_adb" -s "$solar_serial" emu kill >/dev/null 2>&1 \
    || kill "$solar_emulator_pid" 2>/dev/null || true
}
trap solar_cleanup EXIT

"$solar_adb" start-server
solar_booted=false
for solar_attempt in $(seq 1 120); do
  if ! kill -0 "$solar_emulator_pid" 2>/dev/null; then
    tail -80 "$solar_evidence/emulator.log"
    exit 1
  fi
  if [[ $(timeout 5 "$solar_adb" -s "$solar_serial" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r') == 1 ]]; then
    solar_booted=true
    break
  fi
  sleep 2
done
[[ "$solar_booted" == true ]] || { echo 'Emulator did not boot within the limit.' >&2; exit 1; }

"$solar_adb" -s "$solar_serial" shell input keyevent 82
if [[ "$solar_uhd" == true ]]; then
  # A tall mobile display exercises FIT bands instead of accidentally matching UHD.
  "$solar_adb" -s "$solar_serial" shell wm size 960x1920
else
  "$solar_adb" -s "$solar_serial" shell wm size 960x1707
fi
"$solar_adb" -s "$solar_serial" shell wm density 320
"$solar_adb" -s "$solar_serial" shell settings put global window_animation_scale 0
"$solar_adb" -s "$solar_serial" shell settings put global transition_animation_scale 0
"$solar_adb" -s "$solar_serial" shell settings put global animator_duration_scale 0
# A fresh Android user otherwise shows a focus-stealing full-screen tutorial.
# This is the same isolated test-fixture setting used by AOSP window-manager tests.
"$solar_adb" -s "$solar_serial" shell settings put secure immersive_mode_confirmations confirmed
"$solar_adb" -s "$solar_serial" shell settings get secure immersive_mode_confirmations \
  | tr -d '\r' > "$solar_evidence/immersive-tutorial-setting.txt"
[[ $(cat "$solar_evidence/immersive-tutorial-setting.txt") == confirmed ]] \
  || { echo 'Could not acknowledge the emulator full-screen tutorial.' >&2; exit 1; }
if [[ "$solar_uhd" == true ]]; then
  "$solar_adb" -s "$solar_serial" install -r "$solar_apks/COSMIC-X-Solar-coexist.apk"
fi
"$solar_adb" -s "$solar_serial" install -r "$solar_apks/$solar_apk"
"$solar_adb" -s "$solar_serial" install -r "$solar_apks/$solar_tests"
if [[ "$solar_uhd" == true ]]; then
  "$solar_adb" -s "$solar_serial" shell pm list packages --user 0 com.cosmicx.solar \
    > "$solar_evidence/coinstallation.txt"
fi
"$solar_adb" -s "$solar_serial" shell svc wifi disable
"$solar_adb" -s "$solar_serial" shell svc data disable
"$solar_adb" -s "$solar_serial" logcat -c

set +e
timeout "$solar_instrument_timeout" "$solar_adb" -s "$solar_serial" shell am instrument -w -r "${solar_instrument_args[@]}" \
  "$solar_package.test/com.mundox.solar.SolarInstrumentation" \
  | tee "$solar_evidence/instrumentation.txt"
solar_status=${PIPESTATUS[0]}
set -e
solar_collect
[[ "$solar_status" == 0 ]] || { echo "Instrumentation process failed: $solar_status" >&2; exit 1; }
python3 tools/solar-verify-evidence.py "$solar_evidence" "${solar_verify_args[@]}"
