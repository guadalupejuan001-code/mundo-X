#!/usr/bin/env bash
# Every suite installs the same new APK; the graphics companion is validation-only.
set -euo pipefail

cosmic_sdk="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
[[ -n "$cosmic_sdk" ]] || { echo 'ANDROID_HOME or ANDROID_SDK_ROOT is required.' >&2; exit 1; }
cosmic_adb="$cosmic_sdk/platform-tools/adb"
cosmic_suite="${COSMIC_SUITE:-cosmic}"
cosmic_apks="${COSMIC_APK_DIR:-cosmic-apks}"
cosmic_evidence="${COSMIC_EVIDENCE_DIR:-cosmic-evidence}"
cosmic_serial=emulator-5554
case "$cosmic_suite" in
  cosmic) cosmic_runner=com.mundox.cosmic.CosmicInstrumentation; cosmic_runtime=cosmic-evidence; cosmic_timeout=900 ;;
  graphics) cosmic_runner=com.mundox.graphics.GraphicsInstrumentation; cosmic_runtime=graphics-evidence; cosmic_timeout=650 ;;
  micro) cosmic_runner=com.mundox.lab.MicroLabInstrumentation; cosmic_runtime=; cosmic_timeout=400 ;;
  *) echo "Unsupported validation suite: $cosmic_suite" >&2; exit 1 ;;
esac
mkdir -p "$cosmic_evidence"
cosmic_evidence="$(realpath "$cosmic_evidence")"

python3 - "$cosmic_apks" <<'PY'
import hashlib
import sys
from pathlib import Path
directory = Path(sys.argv[1])
expected_names = {'COSMIC-X-Engine-0.3.1.apk', 'COSMIC-X-Engine-0.3.1-tests.apk', 'Mundo-X-Graphics-coexist.apk'}
found = set()
for row in (directory / 'SHA256SUMS.txt').read_text().splitlines():
    expected, name = row.split()
    if name not in expected_names or name in found:
        raise SystemExit('Unexpected APK checksum entry: ' + name)
    if hashlib.sha256((directory / name).read_bytes()).hexdigest() != expected:
        raise SystemExit('APK checksum mismatch: ' + name)
    found.add(name)
if found != expected_names:
    raise SystemExit('Missing build APKs')
print('All APKs match the exact build checksums.')
PY
cp "$cosmic_apks/SHA256SUMS.txt" "$cosmic_evidence/apk-SHA256SUMS.txt"
cp "$cosmic_apks/build-commit.txt" "$cosmic_evidence/build-commit.txt"
cp "$cosmic_apks/apk-audit.json" "$cosmic_evidence/apk-audit.json"
printf '%s\n' "$cosmic_suite" > "$cosmic_evidence/suite.txt"

export ANDROID_USER_HOME="${RUNNER_TEMP:-/tmp}/cosmic-unified-$cosmic_suite"
export ANDROID_AVD_HOME="$ANDROID_USER_HOME/avd"
mkdir -p "$ANDROID_AVD_HOME"
printf 'no\n' | "$cosmic_sdk/cmdline-tools/latest/bin/avdmanager" create avd --force --name cosmic-unified \
  --package 'system-images;android-35;google_apis;x86_64' --device pixel_2 \
  --path "$ANDROID_AVD_HOME/cosmic-unified.avd"
"$cosmic_sdk/emulator/emulator" -avd cosmic-unified -port 5554 -no-window -no-audio -no-boot-anim \
  -gpu swiftshader -no-snapshot -memory 2048 -cores 2 \
  > "$cosmic_evidence/emulator.log" 2>&1 &
cosmic_emulator_pid=$!
cosmic_runtime_collected=false

cosmic_collect() {
  timeout 15 "$cosmic_adb" -s "$cosmic_serial" logcat -d > "$cosmic_evidence/logcat.txt" 2>&1 || true
  timeout 15 "$cosmic_adb" -s "$cosmic_serial" shell dumpsys meminfo com.mundox.cosmic \
    > "$cosmic_evidence/app-memory.txt" 2>&1 || true
  if [[ -n "$cosmic_runtime" ]]; then
    [[ "$cosmic_runtime_collected" == true ]] && return 0
    local cosmic_copy_attempt cosmic_extract
    # adb may exit successfully after a truncated stream. Validate and extract
    # a complete attempt before exposing it to the evidence verifier.
    for cosmic_copy_attempt in 1 2 3; do
      printf 'Runtime evidence attempt %s/3 (%s)\n' "$cosmic_copy_attempt" "$cosmic_runtime" \
        >> "$cosmic_evidence/evidence-pull.log"
      cosmic_extract="$(mktemp -d "$cosmic_evidence/runtime-extract.XXXXXX")"
      if timeout 20 "$cosmic_adb" -s "$cosmic_serial" exec-out run-as com.mundox.cosmic \
          tar -C files -cf - "$cosmic_runtime" > "$cosmic_evidence/runtime-evidence.tar.partial" \
          2>> "$cosmic_evidence/evidence-pull.log" \
          && tar -tf "$cosmic_evidence/runtime-evidence.tar.partial" >/dev/null \
            2>> "$cosmic_evidence/evidence-pull.log" \
          && tar -xf "$cosmic_evidence/runtime-evidence.tar.partial" -C "$cosmic_extract" \
            --strip-components=1 --no-same-owner \
            2>> "$cosmic_evidence/evidence-pull.log" \
          && [[ -s "$cosmic_extract/report.json" ]] \
          && cp -a "$cosmic_extract/." "$cosmic_evidence/" \
          && mv "$cosmic_evidence/runtime-evidence.tar.partial" "$cosmic_evidence/runtime-evidence.tar"; then
        rm -rf -- "$cosmic_extract"
        cosmic_runtime_collected=true
        return 0
      fi
      rm -rf -- "$cosmic_extract"
      printf 'Incomplete runtime evidence; report.json and a valid archive are required.\n' \
        >> "$cosmic_evidence/evidence-pull.log"
      if [[ "$cosmic_copy_attempt" != 3 ]]; then sleep 2; fi
    done
    echo 'Could not collect complete runtime evidence after three attempts.' >&2
    return 1
  else
    timeout 30 "$cosmic_adb" -s "$cosmic_serial" pull \
      /sdcard/Android/data/com.mundox.cosmic/files/. "$cosmic_evidence/" \
      > "$cosmic_evidence/evidence-pull.log" 2>&1 || true
  fi
}
cosmic_cleanup() {
  cosmic_collect || true
  timeout 10 "$cosmic_adb" -s "$cosmic_serial" emu kill >/dev/null 2>&1 \
    || kill "$cosmic_emulator_pid" 2>/dev/null || true
}
trap cosmic_cleanup EXIT

"$cosmic_adb" start-server
cosmic_booted=false
for cosmic_attempt in $(seq 1 120); do
  kill -0 "$cosmic_emulator_pid" 2>/dev/null || { tail -80 "$cosmic_evidence/emulator.log"; exit 1; }
  if [[ $(timeout 5 "$cosmic_adb" -s "$cosmic_serial" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r') == 1 ]]; then
    cosmic_booted=true
    break
  fi
  sleep 2
done
[[ "$cosmic_booted" == true ]] || { echo 'Emulator did not boot within the limit.' >&2; exit 1; }
"$cosmic_adb" -s "$cosmic_serial" shell input keyevent 82
"$cosmic_adb" -s "$cosmic_serial" shell wm size 960x1707
"$cosmic_adb" -s "$cosmic_serial" shell wm density 320
"$cosmic_adb" -s "$cosmic_serial" shell settings put global window_animation_scale 0
"$cosmic_adb" -s "$cosmic_serial" shell settings put global transition_animation_scale 0
"$cosmic_adb" -s "$cosmic_serial" shell settings put global animator_duration_scale 0
"$cosmic_adb" -s "$cosmic_serial" shell getprop ro.build.fingerprint > "$cosmic_evidence/build-fingerprint.txt"

if [[ "$cosmic_suite" == cosmic ]]; then
  "$cosmic_adb" -s "$cosmic_serial" install -r "$cosmic_apks/Mundo-X-Graphics-coexist.apk"
  "$cosmic_adb" -s "$cosmic_serial" shell run-as com.mundox.graphics mkdir -p files
  "$cosmic_adb" -s "$cosmic_serial" shell \
    "run-as com.mundox.graphics sh -c 'echo cosmic-unified-coexist > files/cosmic-coexist-sentinel'"
fi
"$cosmic_adb" -s "$cosmic_serial" install -r "$cosmic_apks/COSMIC-X-Engine-0.3.1.apk"
"$cosmic_adb" -s "$cosmic_serial" install -r "$cosmic_apks/COSMIC-X-Engine-0.3.1-tests.apk"
"$cosmic_adb" -s "$cosmic_serial" shell pm list instrumentation > "$cosmic_evidence/instrumentation-inventory.txt"
"$cosmic_adb" -s "$cosmic_serial" logcat -c

if [[ "$cosmic_suite" == cosmic ]]; then
  "$cosmic_adb" -s "$cosmic_serial" shell pm list packages -U com.mundox. > "$cosmic_evidence/coexist-packages.txt"
  "$cosmic_adb" -s "$cosmic_serial" shell run-as com.mundox.graphics cat files/cosmic-coexist-sentinel \
    > "$cosmic_evidence/coexist-sentinel.txt"
  "$cosmic_adb" -s "$cosmic_serial" shell am force-stop com.mundox.graphics
  timeout 45 "$cosmic_adb" -s "$cosmic_serial" shell am start -W \
    -n com.mundox.graphics/com.mundox.graphics.MainActivity > "$cosmic_evidence/coexist-old-launch.txt"
  "$cosmic_adb" -s "$cosmic_serial" shell am force-stop com.mundox.graphics
  "$cosmic_adb" -s "$cosmic_serial" shell am force-stop com.mundox.cosmic
  timeout 45 "$cosmic_adb" -s "$cosmic_serial" shell am start -W \
    -n com.mundox.cosmic/com.mundox.cosmic.CosmicActivity > "$cosmic_evidence/coexist-new-launch.txt"
fi

set +e
timeout "$cosmic_timeout" "$cosmic_adb" -s "$cosmic_serial" shell am instrument -w -r \
  "com.mundox.cosmic.test/$cosmic_runner" | tee "$cosmic_evidence/instrumentation.txt"
cosmic_status=${PIPESTATUS[0]}
set -e
cosmic_collect
[[ "$cosmic_status" == 0 ]] || { echo "Instrumentation process failed: $cosmic_status" >&2; exit 1; }
python3 tools/cosmic-android-verify-evidence.py "$cosmic_evidence" "$cosmic_suite"
