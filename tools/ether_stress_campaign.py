#!/usr/bin/env python3
"""Host campaign: one fresh process per case, 2 GiB address-space and time caps.

RLIMIT_AS limits virtual memory, not the RAM of a whole Android device. RSS is
reported independently from wait4; C++ allocation counters exclude the driver.
Run unchanged binaries/cases before and after on the same otherwise idle host.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import resource
import signal
import subprocess
import time

CASES = (
    [(m,n,q) for m in ('batch','framed_batch') for n,q in ((64,64),(256,256),(1024,256),(1024,4096))]
    + [('rules',n,d) for d in (8,1000,4096) for n in (16,64,256,1024,4096)]
    + [('support',n,1) for n in (16,256,1024,2048,4096,4097)]
    + [('noop',n,1) for n in (256,1024,1025,4096)]
    + [('addresses',n,d) for n,d in ((64,16),(4096,16),(1024,1000),(2048,4096),(4096,4096),(1,4097))]
    + [('operations',n,1) for n in (1024,16384,65536,65537)]
    + [('lifecycle',n,1) for n in (10,1000)]
    + [('threads',n,1000) for n in (2,4,8)]
    + [('invalid',10000,1)]
)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--seconds',type=int,default=15)
    p.add_argument('--repeat',type=int,default=1)
    p.add_argument('--mode',action='append')
    args=p.parse_args();args.output.parent.mkdir(parents=True,exist_ok=True)
    result={'host':platform.platform(),'memory_profile':{'rlimit_as_bytes':2*1024**3,
        'scope':'host process virtual address space; not Android device RAM'},
        'seconds_per_case':args.seconds,'cases':[]}
    def limits():
        resource.setrlimit(resource.RLIMIT_AS,(2*1024**3,2*1024**3))
        resource.setrlimit(resource.RLIMIT_CORE,(0,0))
        resource.setrlimit(resource.RLIMIT_CPU,(args.seconds,args.seconds+1))
    for iteration in range(args.repeat):
        for mode,n,extra in CASES:
            if args.mode and mode not in args.mode:continue
            stem=args.output.parent/(args.output.stem+'-current')
            with stem.with_suffix('.out').open('w+') as out,stem.with_suffix('.err').open('w+') as err:
                t=time.monotonic()
                child=subprocess.Popen([str(args.binary.resolve()),mode,str(n),str(extra)],
                    stdout=out,stderr=err,preexec_fn=limits)
                killed=False
                while True:
                    pid,status,usage=os.wait4(child.pid,os.WNOHANG)
                    if pid:break
                    if time.monotonic()-t>args.seconds+2 and not killed:
                        child.kill();killed=True
                    time.sleep(.025)
                child.returncode=os.waitstatus_to_exitcode(status)
                out.seek(0);text=out.read();err.seek(0);errors=err.read()
                try:r=json.loads(text)
                except ValueError:r={'mode':mode,'n':n,'extra':extra,'status':'timeout' if killed or child.returncode in (-signal.SIGXCPU,-signal.SIGKILL) else 'crash'}
                r.update(iteration=iteration,returncode=child.returncode,rss_peak_kib=usage.ru_maxrss,
                         user_seconds=usage.ru_utime,wall_seconds=time.monotonic()-t,stderr=errors[-2000:])
                result['cases'].append(r)
                args.output.write_text(json.dumps(result,indent=2)+'\n')
                print(mode,n,extra,r['status'],round(r['wall_seconds'],3),flush=True)
    for suffix in ('.out','.err'):stem.with_suffix(suffix).unlink(missing_ok=True)
    if any(x['status'] in ('error','crash') for x in result['cases']):raise SystemExit(1)

if __name__=='__main__':main()
