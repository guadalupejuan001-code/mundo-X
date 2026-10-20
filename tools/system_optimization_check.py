#!/usr/bin/env python3
"""Reproduce pinned differential physics, Pauli and MicroLab measurements."""
import argparse,hashlib,json,os,resource,subprocess,tempfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE='c13e78f91887b45d735c5fe3d8a07a000bd03d34'
PHYSICS='c7e78948ea95080d0603608696da221f02bf4584'
def limits():
    resource.setrlimit(resource.RLIMIT_AS,(2*1024**3,2*1024**3))
    resource.setrlimit(resource.RLIMIT_CPU,(55,56))
    os.sched_setaffinity(0,{min(os.sched_getaffinity(0))})
def run(output):
    results={'baseline':BASE,'physics_baseline':PHYSICS,'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
             'ram_limit_bytes':2*1024**3,'cpu_seconds_per_case':55,'wall_seconds_per_case':60,'cases':{}}
    with tempfile.TemporaryDirectory(prefix='mx-system-') as tmp:
        tmp=Path(tmp)
        for name,ref,path,old,new in [
            ('physics',PHYSICS,'native/include/mundo_x/physics_world.hpp','namespace mundo_x {','namespace physics_reference {'),
            ('micro',BASE,'native/include/mundo_x/micro_lab.hpp','namespace mundo_x {','namespace micro_reference {\nusing mundo_x::FactorizedCore;'),
            ('observer',BASE,'native/include/mundo_x/pauli_observer.hpp','namespace mundo_x_bridge {','namespace observer_reference {')]:
            s=subprocess.check_output(['git','show',ref+':'+path],cwd=ROOT,text=True)
            (tmp/('baseline_'+name+'.hpp')).write_text(s.replace(old,new))
        cmd=['g++','-std=c++17','-O3','-Wall','-Wextra','-Wpedantic','-pthread','-I'+str(ROOT/'native/include'),'-I'+str(tmp),
             str(ROOT/'tools/system_optimization_check.cpp'),'-o',str(tmp/'check')]
        subprocess.run(cmd,check=True,timeout=60)
        results['compiler']=subprocess.check_output(['g++','--version'],text=True).splitlines()[0]
        for group in ('physics','observer','micro'):
            started=time.monotonic()
            p=subprocess.run([str(tmp/'check'),group],text=True,capture_output=True,preexec_fn=limits,timeout=60)
            if p.returncode:raise RuntimeError(group+': '+p.stderr+p.stdout)
            result=json.loads(p.stdout);result['rss_kib']=int(p.stderr.split('RSS_KIB ')[1].split()[0]);result['wall_s']=time.monotonic()-started
            results['cases'][group]=result
            print(json.dumps({'case':group,'rss_kib':result['rss_kib'],'wall_s':result['wall_s']}),flush=True)
    paths=['native/include/mundo_x/'+x+'.hpp' for x in ('physics_world','micro_lab','pauli_observer')]
    paths+=['tools/system_optimization_check.cpp','tools/system_optimization_check.py']
    results['sha256']={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
    output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(results,indent=2)+'\n')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=ROOT/'results/ether-system-optimization-2026-10-20.json');run(p.parse_args().output)
