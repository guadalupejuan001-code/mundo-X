#!/usr/bin/env python3
"""Build one frozen Solar snapshot, using isolated offline dependency caches."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import resource
import signal
import subprocess
import sys
import time

ROOT=Path('/workspace/scratch/d0e1d0a0e3a1')
WORK=ROOT/'closure/unification/solar'
SDK=Path('/workspace/scratch/7def14e3f3d9/solar-build/sdk')
JAVA=Path('/workspace/scratch/7def14e3f3d9/solar-build/jdk/jdk-17.0.20.1+1')
GRADLE=Path('/workspace/scratch/7def14e3f3d9/solar-build/gradle-8.9/bin/gradle')
OUT=ROOT/'mundo-X/results/repository-unification-2026-10-21/solar/android-builds'
OUT.mkdir(parents=True,exist_ok=True)
variant=sys.argv[1]
if variant != 'baseline-full-evaluation':
    raise SystemExit('variant must be baseline-full-evaluation')
snapshot=WORK/variant
manifest_path=WORK/(variant+'-source-manifest.json')
manifest=json.loads(manifest_path.read_text())
sha=lambda data:hashlib.sha256(data).hexdigest()
def source_check():
    mismatches=[name for name,digest in manifest['source_sha256'].items()
                if sha((snapshot/name).read_bytes())!=digest]
    if mismatches:raise ValueError('Frozen input changed: '+repr(mismatches))
source_check()
user_home=WORK/'build-user-home'
user_home.mkdir(exist_ok=True)
(user_home/'android').mkdir(exist_ok=True)
java_args='-Xmx1024m -Dfile.encoding=UTF-8 -Duser.home='+str(user_home)
env=dict(os.environ,JAVA_HOME=str(JAVA),ANDROID_HOME=str(SDK),ANDROID_SDK_ROOT=str(SDK),
         ANDROID_USER_HOME=str(user_home/'android'),GRADLE_USER_HOME=str(WORK/'gradle-home'),
         GRADLE_OPTS='-Xmx1024m -Duser.home='+str(user_home),CMAKE_BUILD_PARALLEL_LEVEL='1')
args=[str(GRADLE),'--offline','--no-daemon','--max-workers=1','--console=plain',
      '-Dorg.gradle.jvmargs='+java_args,'assembleDebug','assembleDebugAndroidTest']
report={'variant':variant,'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'source_commit':manifest['source_commit'],'source_manifest_sha256':sha(manifest_path.read_bytes()),
        'source_sha256':manifest['source_sha256'],'overlays':manifest['overlays'],
        'command':args,'cwd':str(snapshot/'solar-android'),'jdk':str(JAVA),'sdk':str(SDK),
        'gradle_user_home':str(WORK/'gradle-home'),'java_heap_limit_bytes':1024**3,'gradle_max_workers':1,
        'scope':'New debug and AndroidTest builds; no emulator execution. Heap limit is not a process RSS cap.'}
log_path=WORK/(variant+'-gradle-build.log')
before=time.monotonic()
def limits():resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with log_path.open('w') as log:
    process=subprocess.Popen(args,cwd=snapshot/'solar-android',env=env,stdout=log,stderr=subprocess.STDOUT,
                             start_new_session=True,preexec_fn=limits)
    timeout=False
    while True:
        pid,status,usage=os.wait4(process.pid,os.WNOHANG)
        if pid:break
        if time.monotonic()-before>600:
            timeout=True;os.killpg(process.pid,signal.SIGKILL)
            pid,status,usage=os.wait4(process.pid,0);break
        time.sleep(.2)
    process.returncode=os.waitstatus_to_exitcode(status)
report.update(returncode=process.returncode,timed_out=timeout,wall_seconds=time.monotonic()-before,
              max_rss_kib=usage.ru_maxrss,user_cpu_seconds=usage.ru_utime,system_cpu_seconds=usage.ru_stime,
              log_path=str(log_path),log_sha256=sha(log_path.read_bytes()),
              finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
source_check();report['source_unchanged_after_build']=True
report['apks']={}
if process.returncode==0:
    for name,path in [('debug',snapshot/'solar-android/app/build/outputs/apk/debug/app-debug.apk'),
                      ('androidTest',snapshot/'solar-android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk')]:
        report['apks'][name]={'path':str(path),'sha256':sha(path.read_bytes()),'bytes':path.stat().st_size}
(OUT/(variant+'-build.json')).write_text(json.dumps(report,indent=2)+'\n')
(OUT/(variant+'-build.log')).write_bytes(log_path.read_bytes())
(WORK/(variant+'-build.json')).write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:report[k] for k in ['variant','returncode','timed_out','wall_seconds','max_rss_kib','source_unchanged_after_build','apks']}),flush=True)
raise SystemExit(process.returncode or int(timeout))
