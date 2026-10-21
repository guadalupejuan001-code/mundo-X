#!/usr/bin/env python3
"""Bounded build and current-tree correctness gates, not a speed benchmark."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import resource
import signal
import subprocess
import sys
import time

ROOT=Path('/workspace/scratch/d0e1d0a0e3a1')
REPO=ROOT/'mundo-X'
BUILD=ROOT/'closure/core/build'
CMAKE_BIN=Path('/workspace/scratch/7def14e3f3d9/solar-build/sdk/cmake/3.22.1/bin')
OUT=REPO/'results/repository-unification-2026-10-21/core'
OUT.mkdir(parents=True,exist_ok=True)
REPORT={'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'base_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=REPO,text=True).strip(),
        'scope':'Fresh build and sequential correctness/resource gates; timings are elapsed diagnostics, not an A/B benchmark.',
        'rlimit_as_bytes':2*1024**3,'phases':[]}
resume_after_build='--resume-after-build' in sys.argv
if resume_after_build:
    REPORT=json.loads((OUT/'gate-status.json').read_text())
    REPORT['resumed_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()

def limits():
    resource.setrlimit(resource.RLIMIT_AS,(2*1024**3,2*1024**3))
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))

def phase(name,args,timeout,cpu_seconds=None):
    before=time.monotonic()
    rss=OUT/(name+'.resources.json')
    def child_limits():
        limits()
        if cpu_seconds is not None:
            resource.setrlimit(resource.RLIMIT_CPU,(cpu_seconds,cpu_seconds))
    with open(OUT/(name+'.log'),'w') as log:
        process=subprocess.Popen(list(map(str,args)),cwd=REPO,stdout=log,stderr=subprocess.STDOUT,preexec_fn=child_limits,start_new_session=True,env=dict(os.environ,OMP_NUM_THREADS='1',OPENBLAS_NUM_THREADS='1'))
        timed_out=False
        while True:
            pid,status,usage=os.wait4(process.pid,os.WNOHANG)
            if pid:
                break
            if time.monotonic()-before>timeout:
                timed_out=True
                os.killpg(process.pid,signal.SIGKILL)
                pid,status,usage=os.wait4(process.pid,0)
                break
            time.sleep(0.1)
        process.returncode=os.waitstatus_to_exitcode(status)
        code='timeout' if timed_out else process.returncode
    rss.write_text(json.dumps({'max_rss_kib':usage.ru_maxrss,'user_cpu_seconds':usage.ru_utime,'system_cpu_seconds':usage.ru_stime,'rlimit_as_bytes':2*1024**3,'rlimit_cpu_seconds':cpu_seconds,'measurement':'Linux wait4 rusage: peak RSS includes child process usage; build and tests run sequentially.'},indent=2)+'\n')
    REPORT['phases'].append({'name':name,'command':list(map(str,args)), 'returncode':code,'wall_seconds':time.monotonic()-before,'output':str(OUT/(name+'.log')),'resources':str(rss)})
    (OUT/'gate-status.json').write_text(json.dumps(REPORT,indent=2)+'\n')
    print(json.dumps(REPORT['phases'][-1]),flush=True)
    if code!=0:raise SystemExit(1)

if not resume_after_build:
    phase('configure',[CMAKE_BIN/'cmake','-S',REPO,'-B',BUILD,'-DCMAKE_BUILD_TYPE=Release'],60)
    phase('build',[CMAKE_BIN/'cmake','--build',BUILD,'--parallel','1'],600)
# This workspace restored one generated ELF's mode to 0644 between separate
# command invocations. Set its intended build-artifact mode in this same
# invocation; the executable bytes and all sources remain unchanged.
(BUILD/'test_infinite_tensor').chmod(0o755)
phase('ctest',[CMAKE_BIN/'ctest','--test-dir',BUILD,'--parallel','1','--output-on-failure','--output-junit',OUT/'ctest.xml'],480)
phase('physics3d-65536',[BUILD/'mundo_x','physics3d','65536','1'],40,30)
phase('infinite-extreme',[BUILD/'test_infinite_extreme'],40,30)
phase('ether-batch-1000-24',[BUILD/'ether_stress','batch','1000','24'],40,30)
REPORT['finished_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
REPORT['passed']=True
REPORT['source_manifest_sha256']=hashlib.sha256((OUT/'source-manifest.json').read_bytes()).hexdigest()
(OUT/'gate-status.json').write_text(json.dumps(REPORT,indent=2)+'\n')
