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
pc_app_package="${PC_APP_PACKAGE:-com.cosmicx.pc}"
pc_test_package="${PC_TEST_PACKAGE:-${pc_app_package}.test}"
pc_evidence_collected=false
collect_evidence() {
  timeout 20 "$adb_bin" logcat -d > "$evidence_dir/logcat.txt" 2>/dev/null || true
  for pc_file in pc-screen.png pc-tests.json pc-x64-state.json pc-enter-state.json pc-video-state.json pc-video-screen.png; do
    if timeout 20 "$adb_bin" exec-out run-as "$pc_app_package" cat "files/$pc_file" > "$evidence_dir/$pc_file.tmp" 2>/dev/null; then
      mv "$evidence_dir/$pc_file.tmp" "$evidence_dir/$pc_file"
    else
      rm -f "$evidence_dir/$pc_file.tmp"
    fi
  done
  pc_evidence_collected=true
}
finish() {
  pc_exit_status=$?
  trap - EXIT
  set +e
  if [[ "$pc_evidence_collected" != true ]]; then collect_evidence; fi
  kill "$pc_emulator_pid" 2>/dev/null || true
  exit "$pc_exit_status"
}
trap finish EXIT
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
timeout --kill-after=15 600 "$adb_bin" shell am instrument -w "$pc_test_package"/com.cosmicx.pc.PcInstrumentation | tee "$evidence_dir/instrumentation.txt"
collect_evidence
python3 - <<'PY'
import json
from pathlib import Path
assert 'COSMIC_PC_TESTS_PASSED tests=30 failures=0' in Path('pc-evidence/instrumentation.txt').read_text()
v=json.loads(Path('pc-evidence/pc-tests.json').read_text())
assert v['tests']==30 and v['failures']==0 and not v['windows_booted'] and v['x64_guest_verified']
assert v['infinite_guest_verified'] and v['shared_session_graphics_verified']
assert v['graphics_bridge_read_only'] and v['graphics_open_read_only']
ether=v['ether_fusion']
assert ether['rules']==[0,2,0] and ether['same_owner'] and ether['render_read_only']
assert ether['before']==[1,0,1,-1,1] and ether['framed']==[0,1,0,0,1] and ether['restored']==ether['before']
assert ether['framed_revision']==ether['before_revision']+2 and ether['restored_revision']==ether['before_revision']+4
assert ether['framed_epoch']==ether['before_epoch']+1 and ether['restored_epoch']==ether['before_epoch']+2
assert ether['global_changed_pixels']>10 and ether['roundtrip_changed_pixels']==0
assert v['coupled_changed_pixels']>10 and v['uncoupled_changed_pixels']==0
assert v['iso_shrd_verified'] and v['iso_enter_verified'] and v['guest_8k_verified'] and v['output_8k_verified'] and v['video_tests']==8
assert v['logical_ram_bytes']==2**31 and v['guest_payload_cap_bytes']==2**30
assert 0<v['native_test_process_pss_bytes']<2**31
assert 0<v['activity_process_pss_bytes']<2**31
x=json.loads(Path('pc-evidence/pc-x64-state.json').read_text())
assert x['cpu_mode']=='long64' and x['status']=='halted' and x['regs_hex'][0]=='2345678922222223'
enter=json.loads(Path('pc-evidence/pc-enter-state.json').read_text())
assert enter['status']=='passed' and enter['fixture']=='synthetic_el_torito_enter_leave'
assert enter['source_commit']=='57a8b339f9580ff7a41d3da25ca4636aef5e2388'
assert enter['enter_address']=='2000:056c' and enter['enter_bytes']=='C8 10 00 02'
mounted,before,after,finished=(enter[key] for key in ['mounted','before_enter','after_enter','state'])
assert mounted['boot_kind']=='iso' and mounted['status']=='ready' and mounted['cs']==0x2000 and mounted['ip']==0
assert before['status']==after['status']=='running' and before['cpu_mode']==after['cpu_mode']=='real16'
assert before['cs']==after['cs']==finished['cs']==0x2000 and before['ip']==0x056c and after['ip']==0x0570
assert before['regs'][4]&65535==0x9000 and before['regs'][5]&65535==0x9100
assert after['regs'][4]&65535==0x8fea and after['regs'][5]&65535==0x8ffe
assert before['regs'][0]&65535==after['regs'][0]&65535==0x1234
assert before['regs'][3]&65535==after['regs'][3]&65535==0x5678
assert before['flags']==after['flags'] and after['instructions']==before['instructions']+1
assert finished['boot_kind']=='iso' and finished['status']=='halted' and finished['cpu_mode']=='real16' and not finished['windows_ready']
for register,expected in [(0,0x9100),(3,0xa55a),(1,0x8ffe),(2,0x8fea),(6,0xcafe),(5,0x9100),(4,0x9000),(7,0xbeef)]:
    assert finished['regs'][register]&65535==expected,(register,finished['regs'])
assert enter['guest_text']=='ENTER/LEAVE: OK' and finished['instructions']>after['instructions']
video=json.loads(Path('pc-evidence/pc-video-state.json').read_text())
assert video['status']=='passed' and video['windows_booted'] is False and video['virtual_3d_gpu'] is False
iso=video['iso_shrd']
assert iso['status']=='halted' and iso['boot_kind']=='iso' and iso['regs'][0]&65535==0x8123
rgba=['FF4040FF','32E875FF','477BFFFF','F5F7FAFF','37D5FFFF']
for key,width,height in [('native_720',1280,720),('native_8k',7680,4320)]:
    sample=video[key]
    state=sample['state']
    assert state['video_kind']=='vbe32' and state['video_width']==width and state['video_height']==height,(key,state)
    assert sample['rgba']==rgba,(key,sample['rgba'])
assert video['native_720']['cache_reused'] is True
native8k=video['native_8k']
assert native8k['snapshot_bytes']==132710400 and native8k['snapshot_budget_bytes']==256*2**20
assert native8k['stable_after_reset'] is True and native8k['stable_after_destroy'] is True
assert native8k['state']['ram_allocated_bytes']<12*2**20
assert video['native_snapshot_bytes_after_cleanup']==0
assert video['jni_invalid_requests_rejected']==11 and video['jni_direct_slice_guards_preserved'] is True
for key,width,height,mode,output in [('ui_4k',1280,720,1,[2160,3840]),('ui_8k',7680,4320,2,[4320,7680]),('pause_resume',7680,4320,2,[4320,7680])]:
    sample=video[key]
    assert sample['guest_width']==width and sample['guest_height']==height,(key,sample)
    assert sorted([sample['output_width'],sample['output_height']])==output,(key,sample)
    assert sample['active_mode']==mode and sample['requested_mode']==mode and sample['error']=='',(key,sample)
    assert sample['rgba']==rgba and sample['frames']>0 and sample['revision']>0,(key,sample)
    assert sample['uploaded_bytes']>=width*height*4 and sample['texture_limit']>=max(width,height,max(output)),(key,sample)
    assert sample['renderer'] and sample['gl_texture_budget_bytes']==512*2**20
    assert 0<sample['gl_texture_bytes']<=sample['gl_texture_budget_bytes']
    assert 0<sample['process_pss_bytes']<2**31
restored=video['pause_resume']
assert restored['forced_context_loss'] is True and restored['same_guest_revision'] is True
assert restored['revision']==video['ui_8k']['revision']
assert restored['uploaded_bytes']>=video['ui_8k']['uploaded_bytes']+132710400
assert restored['frames']>video['ui_8k']['frames']
rollback=video['gpu_limit_rollback']
assert rollback['injected_texture_limit']==4096 and rollback['requested_mode']==2 and rollback['active_mode']==1
assert sorted([rollback['output_width'],rollback['output_height']])==[2160,3840]
assert rollback['pixels_preserved'] is True and 'GL_MAX_TEXTURE_SIZE=4096' in rollback['error']
for name in ['pc-screen.png','pc-video-screen.png']:
    screenshot=Path('pc-evidence',name).read_bytes()
    assert len(screenshot)>1000 and screenshot.startswith(b'\x89PNG\r\n\x1a\n'),name
PY
