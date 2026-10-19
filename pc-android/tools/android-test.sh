#!/usr/bin/env bash
set -euo pipefail
evidence_dir="$(pwd)/pc-evidence"
mkdir -p "$evidence_dir"
adb_bin="$ANDROID_HOME/platform-tools/adb"
avd_bin="$ANDROID_HOME/cmdline-tools/latest/bin/avdmanager"
emulator_bin="$ANDROID_HOME/emulator/emulator"
pc_avd_root="$(mktemp -d "${RUNNER_TEMP:-/tmp}/cosmic-pc-avd.XXXXXX")"
# Give avdmanager and emulator one explicit registry and data directory. Some
# CI images set Android user paths differently for the two SDK executables.
export ANDROID_AVD_HOME="$pc_avd_root"
printf 'no\n' | "$avd_bin" create avd --force -n cosmic-pc-test -p "$pc_avd_root/cosmic-pc-test.avd" -k 'system-images;android-35;default;x86_64' > "$evidence_dir/avd-create.log" 2>&1
"$emulator_bin" -list-avds > "$evidence_dir/avd-list.txt"
python3 - "$evidence_dir/avd-list.txt" <<'PY'
import sys
from pathlib import Path
assert 'cosmic-pc-test' in Path(sys.argv[1]).read_text().splitlines(), 'AVD not registered'
PY
"$adb_bin" start-server
"$emulator_bin" -avd cosmic-pc-test -no-window -no-audio -no-boot-anim -no-snapshot -gpu swiftshader_indirect -memory 2048 > "$evidence_dir/emulator.log" 2>&1 &
pc_emulator_pid=$!
trap 'kill "$pc_emulator_pid" 2>/dev/null || true' EXIT
ready=false
for i in $(seq 1 120); do
  if ! kill -0 "$pc_emulator_pid" 2>/dev/null; then cat "$evidence_dir/emulator.log"; exit 1; fi
  if [[ $("$adb_bin" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r') == 1 ]]; then ready=true; break; fi
  sleep 2
done
[[ "$ready" == true ]]
"$adb_bin" shell input keyevent 82
"$adb_bin" install -r pc-dist/COSMIC-X-PC.apk
"$adb_bin" install -r pc-dist/COSMIC-X-PC-tests.apk
"$adb_bin" logcat -c
timeout 180 "$adb_bin" shell am instrument -w com.cosmicx.pc.test/com.cosmicx.pc.PcInstrumentation | tee "$evidence_dir/instrumentation.txt"
"$adb_bin" logcat -d > "$evidence_dir/logcat.txt"
"$adb_bin" exec-out run-as com.cosmicx.pc cat files/pc-screen.png > "$evidence_dir/pc-screen.png"
"$adb_bin" exec-out run-as com.cosmicx.pc cat files/pc-tests.json > "$evidence_dir/pc-tests.json"
python3 - <<'PY'
import json
from pathlib import Path
assert 'COSMIC_PC_TESTS_PASSED tests=8 failures=0' in Path('pc-evidence/instrumentation.txt').read_text()
v=json.loads(Path('pc-evidence/pc-tests.json').read_text())
assert v['tests']==8 and v['failures']==0 and not v['windows_booted']
assert Path('pc-evidence/pc-screen.png').stat().st_size>1000
PY
