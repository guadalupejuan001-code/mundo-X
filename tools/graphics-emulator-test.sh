#!/usr/bin/env bash
# Reproducible Android 15 / x86_64 / SwiftShader validation. No release publishing.
set -euo pipefail

graphics_sdk="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
[[ -n "$graphics_sdk" ]] || { echo 'ANDROID_HOME or ANDROID_SDK_ROOT is required.' >&2; exit 1; }
graphics_adb="$graphics_sdk/platform-tools/adb"
graphics_avdmanager="$graphics_sdk/cmdline-tools/latest/bin/avdmanager"
graphics_emulator="$graphics_sdk/emulator/emulator"
graphics_serial=emulator-5554
graphics_apks="${GRAPHICS_APK_DIR:-graphics-apks}"
graphics_evidence="${GRAPHICS_EVIDENCE_DIR:-graphics-evidence}"
graphics_uhd="${GRAPHICS_UHD4K:-false}"
graphics_ultra="${GRAPHICS_ULTRA8K:-false}"
graphics_mirrors="${GRAPHICS_MIRRORS:-false}"
graphics_package=com.mundox.graphics
graphics_apk=COSMIC-X-Engine.apk
graphics_tests=COSMIC-X-Engine-tests.apk
graphics_instrument_args=()
graphics_verify_args=()
if [[ "$graphics_uhd" == true ]]; then
  graphics_package=com.mundox.graphics.uhd
  graphics_apk=COSMIC-X-Engine-4K.apk
  graphics_tests=COSMIC-X-Engine-4K-tests.apk
  graphics_instrument_args=(-e uhd4k true)
  graphics_verify_args=(--uhd4k)
fi
if [[ "$graphics_ultra" == true ]]; then
  graphics_uhd=true
  graphics_package=com.mundox.graphics.ultra
  graphics_apk=COSMIC-X-Engine-8K.apk
  graphics_tests=COSMIC-X-Engine-8K-tests.apk
  graphics_instrument_args=(-e uhd4k true -e ultra8k true)
  graphics_verify_args=(--ultra8k)
fi
if [[ "$graphics_mirrors" == true ]]; then
  graphics_instrument_args+=(-e mirrors true)
  graphics_verify_args+=(--mirrors)
fi
mkdir -p "$graphics_evidence"
graphics_evidence="$(realpath "$graphics_evidence")"

# Dedicated Android tool configuration prevents reusing an unrelated emulator.
export ANDROID_USER_HOME="${RUNNER_TEMP:-/tmp}/mundox-graphics-android"
export ANDROID_AVD_HOME="$ANDROID_USER_HOME/avd"
mkdir -p "$ANDROID_AVD_HOME"
printf 'no\n' | "$graphics_avdmanager" create avd --force --name mundox-graphics \
  --package 'system-images;android-35;google_apis;x86_64' --device pixel_2 \
  --path "$ANDROID_AVD_HOME/mundox-graphics.avd"

python3 - "$graphics_apks" "$graphics_uhd" "$graphics_ultra" <<'PY'
import hashlib
import sys
from pathlib import Path
directory = Path(sys.argv[1])
rows = (directory / 'SHA256SUMS.txt').read_text().splitlines()
expected_names = {'COSMIC-X-Engine.apk', 'COSMIC-X-Engine-tests.apk'}
if sys.argv[2] == 'true':
    expected_names = {'COSMIC-X-Engine-4K.apk', 'COSMIC-X-Engine-4K-tests.apk', 'COSMIC-X-Engine-coexist.apk'}
if sys.argv[3] == 'true':
    expected_names = {'COSMIC-X-Engine-8K.apk', 'COSMIC-X-Engine-8K-tests.apk', 'COSMIC-X-Engine-coexist.apk'}
found = set()
for row in rows:
    expected, name = row.split()
    if name not in expected_names or name in found:
        raise SystemExit('Unexpected APK checksum entry: ' + name)
    if hashlib.sha256((directory / name).read_bytes()).hexdigest() != expected:
        raise SystemExit('APK checksum mismatch: ' + name)
    found.add(name)
if found != expected_names:
    raise SystemExit('Missing APK checksums')
print('All APKs match build checksums.')
PY
cp "$graphics_apks/SHA256SUMS.txt" "$graphics_evidence/apk-SHA256SUMS.txt"
cp "$graphics_apks/build-commit.txt" "$graphics_evidence/build-commit.txt"
if [[ "$graphics_uhd" == true ]]; then
  cp "$graphics_apks/build-provenance.json" "$graphics_evidence/build-provenance.json"
fi

"$graphics_emulator" -avd mundox-graphics -port 5554 -no-window -no-audio -no-boot-anim \
  -gpu swiftshader -no-snapshot -memory 2048 -cores 2 \
  > "$graphics_evidence/emulator.log" 2>&1 &
graphics_emulator_pid=$!

graphics_collect() {
  timeout 15 "$graphics_adb" -s "$graphics_serial" logcat -d > "$graphics_evidence/logcat.txt" 2>&1 || true
  timeout 15 "$graphics_adb" -s "$graphics_serial" shell dumpsys meminfo "$graphics_package" \
    > "$graphics_evidence/app-memory.txt" 2>&1 || true
  if timeout 60 "$graphics_adb" -s "$graphics_serial" exec-out run-as "$graphics_package" \
      tar -C files -cf - graphics-evidence > "$graphics_evidence/runtime-evidence.tar" \
      2> "$graphics_evidence/evidence-pull.log"; then
    tar -xf "$graphics_evidence/runtime-evidence.tar" -C "$graphics_evidence" --strip-components=1 || true
  fi
}
graphics_cleanup() {
  graphics_collect
  timeout 10 "$graphics_adb" -s "$graphics_serial" emu kill >/dev/null 2>&1 \
    || kill "$graphics_emulator_pid" 2>/dev/null || true
}
trap graphics_cleanup EXIT

"$graphics_adb" start-server
graphics_booted=false
for graphics_attempt in $(seq 1 120); do
  if ! kill -0 "$graphics_emulator_pid" 2>/dev/null; then
    tail -80 "$graphics_evidence/emulator.log"
    exit 1
  fi
  if [[ $(timeout 5 "$graphics_adb" -s "$graphics_serial" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r') == 1 ]]; then
    graphics_booted=true
    break
  fi
  sleep 2
done
[[ "$graphics_booted" == true ]] || { echo 'Emulator did not boot within the limit.' >&2; exit 1; }

"$graphics_adb" -s "$graphics_serial" shell input keyevent 82
"$graphics_adb" -s "$graphics_serial" shell wm size 960x1707
"$graphics_adb" -s "$graphics_serial" shell wm density 320
"$graphics_adb" -s "$graphics_serial" shell settings put global window_animation_scale 0
"$graphics_adb" -s "$graphics_serial" shell settings put global transition_animation_scale 0
"$graphics_adb" -s "$graphics_serial" shell settings put global animator_duration_scale 0
"$graphics_adb" -s "$graphics_serial" shell settings put secure immersive_mode_confirmations confirmed
if [[ "$graphics_uhd" == true ]]; then
  "$graphics_adb" -s "$graphics_serial" install -r "$graphics_apks/COSMIC-X-Engine-coexist.apk"
fi
"$graphics_adb" -s "$graphics_serial" install -r "$graphics_apks/$graphics_apk"
"$graphics_adb" -s "$graphics_serial" install -r "$graphics_apks/$graphics_tests"
if [[ "$graphics_uhd" == true ]]; then
  "$graphics_adb" -s "$graphics_serial" shell pm list packages --user 0 com.mundox.graphics \
    > "$graphics_evidence/coinstallation.txt"
  [[ -s "$graphics_evidence/coinstallation.txt" ]] || { echo 'Coinstallation evidence is empty.' >&2; exit 1; }
fi
"$graphics_adb" -s "$graphics_serial" logcat -c

set +e
timeout "${GRAPHICS_TEST_TIMEOUT:-1000}" "$graphics_adb" -s "$graphics_serial" shell am instrument -w -r "${graphics_instrument_args[@]}" \
  "$graphics_package.test/com.mundox.graphics.GraphicsInstrumentation" \
  | tee "$graphics_evidence/instrumentation.txt"
graphics_status=${PIPESTATUS[0]}
set -e
graphics_collect
[[ "$graphics_status" == 0 ]] || { echo "Instrumentation process failed: $graphics_status" >&2; exit 1; }
python3 tools/graphics-verify-evidence.py "$graphics_evidence" "${graphics_verify_args[@]}"
if [[ "$graphics_ultra" == true ]]; then
  python3 tools/graphics-verify-ultra.py "$graphics_evidence"
fi
