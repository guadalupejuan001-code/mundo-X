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
solar_4k="${SOLAR_UHD4K:-false}"
solar_8k="${SOLAR_UHD8K:-false}"
[[ "$solar_4k" != true || "$solar_8k" != true ]] || { echo 'Choose Solar 4K or 8K, not both.' >&2; exit 1; }
solar_profile=normal
solar_system_image='system-images;android-35;google_apis;x86_64'
solar_requested_ram_mib=2048
solar_requested_cores=2
solar_package=com.cosmicx.solar
solar_apk=COSMIC-X-Solar.apk
solar_tests=COSMIC-X-Solar-tests.apk
solar_instrument_timeout=1000
solar_instrument_args=()
solar_verify_args=()
if [[ "$solar_8k" == true ]]; then
  solar_profile=8k
  # Match XLab's AOSP fixture: Solar has no Google Play dependency. The
  # Google APIs image hit memory pressure and system-service crashes before
  # the focused 8K tests. Keep that environment separate from app assertions.
  solar_system_image='system-images;android-35;default;x86_64'
  solar_package=com.cosmicx.solar.ultra
  solar_apk=COSMIC-X-Solar-8K.apk
  solar_tests=COSMIC-X-Solar-8K-tests.apk
  solar_instrument_timeout=2400
  solar_instrument_args=(-e ultra8k true)
  solar_verify_args=(--ultra8k)
elif [[ "$solar_4k" == true ]]; then
  solar_profile=4k
  solar_package=com.cosmicx.solar.uhd
  solar_apk=COSMIC-X-Solar-4K.apk
  solar_tests=COSMIC-X-Solar-4K-tests.apk
  solar_instrument_timeout=1800
  solar_instrument_args=(-e uhd4k true)
  solar_verify_args=(--uhd4k)
fi
mkdir -p "$solar_evidence"
solar_evidence="$(realpath "$solar_evidence")"
printf '%s\n' "$solar_system_image" > "$solar_evidence/system-image.txt"

python3 - "$solar_apks" "$solar_profile" <<'PY'
import hashlib
import subprocess
import sys
from pathlib import Path
directory = Path(sys.argv[1])
expected_names = {'COSMIC-X-Solar.apk', 'COSMIC-X-Solar-tests.apk'}
profile = sys.argv[2]
if profile == '4k':
    expected_names = {'COSMIC-X-Solar-4K.apk', 'COSMIC-X-Solar-4K-tests.apk', 'COSMIC-X-Solar-coexist.apk'}
elif profile == '8k':
    expected_names = {'COSMIC-X-Solar-8K.apk', 'COSMIC-X-Solar-8K-tests.apk', 'COSMIC-X-Solar-coexist.apk'}
elif profile != 'normal':
    raise SystemExit('Unexpected Solar profile: ' + profile)
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
  --package "$solar_system_image" --device pixel_2 \
  --path "$ANDROID_AVD_HOME/cosmic-solar.avd"

"$solar_emulator" -avd cosmic-solar -port 5554 -no-window -no-audio -no-boot-anim \
  -gpu swiftshader -no-snapshot -memory "$solar_requested_ram_mib" -cores "$solar_requested_cores" \
  > "$solar_evidence/emulator.log" 2>&1 &
solar_emulator_pid=$!
solar_collected=false
solar_collection_attempted=false

solar_collect() {
  # A completed snapshot is immutable. A failed collection also gets only
  # three bounded attempts; EXIT cleanup must not repeat or hide that failure.
  [[ "$solar_collected" == false ]] || return 0
  [[ "$solar_collection_attempted" == false ]] || return 1
  solar_collection_attempted=true
  timeout 15 "$solar_adb" -s "$solar_serial" logcat -d > "$solar_evidence/logcat.txt" 2>&1 || true
  timeout 15 "$solar_adb" -s "$solar_serial" shell dumpsys meminfo "$solar_package" \
    > "$solar_evidence/app-memory.txt" 2>&1 || true
  timeout 15 "$solar_adb" -s "$solar_serial" exec-out screencap -p \
    > "$solar_evidence/device-screen.png" 2>/dev/null || true
  local solar_copy_attempt solar_extract solar_partial
  # adb can return success with a truncated stream. Match the existing COSMIC
  # collector: validate and extract a complete attempt before running any gate.
  for solar_copy_attempt in 1 2 3; do
    printf 'Runtime evidence attempt %s/3 (solar-evidence)\n' "$solar_copy_attempt" \
      >> "$solar_evidence/evidence-pull.log"
    solar_extract="$(mktemp -d "$solar_evidence/runtime-extract.XXXXXX")" || return 1
    solar_partial="$solar_evidence/runtime-evidence-attempt-$solar_copy_attempt.tar.partial"
    if timeout 60 "$solar_adb" -s "$solar_serial" exec-out run-as "$solar_package" \
        tar -C files -cf - solar-evidence > "$solar_partial" \
        2>> "$solar_evidence/evidence-pull.log" \
        && tar -tf "$solar_partial" >/dev/null \
          2>> "$solar_evidence/evidence-pull.log" \
        && tar -xf "$solar_partial" -C "$solar_extract" --strip-components=1 --no-same-owner \
          2>> "$solar_evidence/evidence-pull.log" \
        && [[ -s "$solar_extract/report.json" ]] \
        && cp -a "$solar_extract/." "$solar_evidence/" \
        && mv "$solar_partial" "$solar_evidence/runtime-evidence.tar"; then
      rm -rf -- "$solar_extract"
      solar_collected=true
      return 0
    fi
    rm -rf -- "$solar_extract"
    printf 'Incomplete runtime evidence; report.json and a valid archive are required.\n' \
      >> "$solar_evidence/evidence-pull.log"
    if [[ "$solar_copy_attempt" != 3 ]]; then sleep 2; fi
  done
  echo 'Could not collect complete runtime evidence after three attempts.' >&2
  return 1
}
solar_cleanup() {
  local solar_exit_status=$?
  trap - EXIT
  solar_collect || true
  timeout 10 "$solar_adb" -s "$solar_serial" emu kill >/dev/null 2>&1 \
    || kill "$solar_emulator_pid" 2>/dev/null || true
  exit "$solar_exit_status"
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
if [[ "$solar_profile" != normal ]]; then
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
# The disposable Google APIs image starts Digital Wellbeing during first boot.
# Its unrelated ANR dialog obscured Solar in run 39526904153. Isolate that
# optional service, while leaving Solar lifecycle and window-focus checks intact.
solar_wellbeing=com.google.android.apps.wellbeing
timeout 30 "$solar_adb" -s "$solar_serial" shell pm list packages --user 0 "$solar_wellbeing" \
  | tr -d '\r' > "$solar_evidence/wellbeing-installed.txt"
if [[ $(cat "$solar_evidence/wellbeing-installed.txt") == "package:$solar_wellbeing" ]]; then
  timeout 30 "$solar_adb" -s "$solar_serial" shell pm disable-user --user 0 "$solar_wellbeing" \
    > "$solar_evidence/wellbeing-disable.txt"
  timeout 30 "$solar_adb" -s "$solar_serial" shell pm list packages -d --user 0 "$solar_wellbeing" \
    | tr -d '\r' > "$solar_evidence/wellbeing-disabled.txt"
  [[ $(cat "$solar_evidence/wellbeing-disabled.txt") == "package:$solar_wellbeing" ]] \
    || { echo 'Could not isolate the emulator Digital Wellbeing service.' >&2; exit 1; }
fi
if [[ "$solar_profile" != normal ]]; then
  "$solar_adb" -s "$solar_serial" install -r "$solar_apks/COSMIC-X-Solar-coexist.apk"
fi
"$solar_adb" -s "$solar_serial" install -r "$solar_apks/$solar_apk"
"$solar_adb" -s "$solar_serial" install -r "$solar_apks/$solar_tests"
if [[ "$solar_profile" != normal ]]; then
  "$solar_adb" -s "$solar_serial" shell pm list packages --user 0 com.cosmicx.solar \
    > "$solar_evidence/coinstallation.txt"
fi
"$solar_adb" -s "$solar_serial" shell svc wifi disable
"$solar_adb" -s "$solar_serial" shell svc data disable
# Record actual resources before launching Solar. Recent emulator versions may
# raise -memory 2048 to 2560 MiB; the requested value is not a measured guest cap.
cat /proc/meminfo > "$solar_evidence/host-meminfo-before.txt"
if [[ -r /proc/pressure/memory ]]; then
  cat /proc/pressure/memory > "$solar_evidence/host-memory-pressure-before.txt"
fi
cat "/proc/$solar_emulator_pid/status" > "$solar_evidence/emulator-process-before.txt"
tr '\0' ' ' < "/proc/$solar_emulator_pid/cmdline" > "$solar_evidence/emulator-command-line.txt"
cp "$ANDROID_AVD_HOME/cosmic-solar.avd/config.ini" "$solar_evidence/avd-config.ini"
if [[ -f "$ANDROID_AVD_HOME/cosmic-solar.avd/hardware-qemu.ini" ]]; then
  cp "$ANDROID_AVD_HOME/cosmic-solar.avd/hardware-qemu.ini" "$solar_evidence/avd-effective-hardware.ini"
fi
timeout 15 "$solar_adb" -s "$solar_serial" shell cat /proc/meminfo \
  > "$solar_evidence/guest-meminfo-before.txt"
timeout 15 "$solar_adb" -s "$solar_serial" shell getprop ro.build.fingerprint \
  > "$solar_evidence/build-fingerprint.txt"
timeout 15 "$solar_adb" -s "$solar_serial" shell getprop ro.build.version.sdk \
  > "$solar_evidence/android-sdk.txt"
python3 - "$solar_evidence" "$solar_requested_ram_mib" "$solar_requested_cores" <<'PY'
import json
import re
import sys
from pathlib import Path
directory = Path(sys.argv[1])
def memory(name):
    return {key: int(value) for key, value in re.findall(
        r'^(MemTotal|MemAvailable|SwapTotal|SwapFree):\s+(\d+)\s+kB',
        (directory / name).read_text(), re.MULTILINE)}
guest = memory('guest-meminfo-before.txt')
if guest.get('MemTotal', 0) <= 0:
    raise SystemExit('Guest MemTotal was not observed before instrumentation')
sdk = int((directory / 'android-sdk.txt').read_text().strip())
if sdk != 35:
    raise SystemExit('Expected the Android 15 (API 35) test image')
emulator_log = (directory / 'emulator.log').read_text(errors='replace')
ram_changes = re.findall(r'(?:Increasing RAM size to |Physical RAM size: )(\d+)\s*MB', emulator_log)
process = (directory / 'emulator-process-before.txt').read_text()
result = {
    'systemImage': (directory / 'system-image.txt').read_text().strip(),
    'androidSdk': sdk,
    'buildFingerprint': (directory / 'build-fingerprint.txt').read_text().strip(),
    'requestedRamMiB': int(sys.argv[2]),
    'requestedCores': int(sys.argv[3]),
    'emulatorRamMiBFromLog': int(ram_changes[-1]) if ram_changes else None,
    'guestBeforeKiB': guest,
    'hostBeforeKiB': memory('host-meminfo-before.txt'),
    'emulatorBefore': {key: int(value) for key, value in re.findall(
        r'^(VmPeak|VmHWM|VmRSS|Threads):\s+(\d+)', process, re.MULTILINE)},
    'scope': 'Pre-instrumentation snapshots; requested RAM is not an effective memory cap or a process peak.'
}
(directory / 'resource-provenance.json').write_text(json.dumps(result, indent=2) + '\n')
print('Recorded actual host/guest resources before Solar instrumentation.')
PY
timeout 15 "$solar_adb" -s "$solar_serial" logcat -d \
  > "$solar_evidence/logcat-before-instrumentation.txt" 2>&1
"$solar_adb" -s "$solar_serial" logcat -c

set +e
timeout "$solar_instrument_timeout" "$solar_adb" -s "$solar_serial" shell am instrument -w -r "${solar_instrument_args[@]}" \
  "$solar_package.test/com.mundox.solar.SolarInstrumentation" \
  | tee "$solar_evidence/instrumentation.txt"
solar_status=${PIPESTATUS[0]}
set -e
solar_collection_status=0
solar_collect || solar_collection_status=$?
[[ "$solar_status" == 0 ]] || { echo "Instrumentation process failed: $solar_status" >&2; exit "$solar_status"; }
[[ "$solar_collection_status" == 0 ]] || { echo 'Runtime evidence collection failed.' >&2; exit "$solar_collection_status"; }
python3 tools/solar-verify-evidence.py "$solar_evidence" "${solar_verify_args[@]}"
