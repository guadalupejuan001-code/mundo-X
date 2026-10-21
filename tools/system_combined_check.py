#!/usr/bin/env python3
"""Finite combined load, independent worlds plus live Ether-driven rendering."""
import argparse,ctypes as C,hashlib,io,json,os,resource,statistics,subprocess,sys,tarfile,tempfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE='c13e78f91887b45d735c5fe3d8a07a000bd03d34'
PHYSICS='c7e78948ea95080d0603608696da221f02bf4584'
def child(root,library):
    sys.path.insert(0,str(root/'graphics-desktop'))
    import numpy as np
    from engine import QuantumScene,QuantumGraphicsEngine,street
    from infinite_runtime import InfiniteCore
    lib=C.CDLL(str(library));lib.mxw_create.restype=C.c_void_p;lib.mxw_destroy.argtypes=[C.c_void_p]
    lib.mxw_step.argtypes=[C.c_void_p,C.POINTER(C.c_uint64)];lib.mxw_error.restype=C.c_char_p
    work=lib.mxw_create();assert work,lib.mxw_error();records=[]
    with InfiniteCore() as core:
        core.apply([('H','0')]+[('CX','0',str(q)) for q in range(1,256)])
        core.h_all()
        observables=[' '.join('Z:'+str(q) for q in range(256))]
        for k in range(511):observables.append(' '.join('XYZ'[(k+j)%3]+':'+str((k*13+j*7)%256) for j in range(1+k%8)))
        scene=QuantumScene.from_infinite_core(core,count=1_048_576,observables=observables);engine=QuantumGraphicsEngine(scene,320,180)
        try:
            for k in range(18):
                mode=k//6;engine.renderer.set_reflections(mode,budget=64)
                cam=street(320,180,k*.13)
                # Exclude one warmup per specialization from sustained medians.
                start=time.perf_counter_ns();h=C.c_uint64();assert lib.mxw_step(work,C.byref(h))==0,lib.mxw_error()
                physical_ms=(time.perf_counter_ns()-start)/1e6
                core.apply([('X','0')]);scene.edit_many({j:(j+k)%8 for j in range(64)})
                frame,metrics=engine.observe(cam,t=k*.21,force=True,presentation=k%2)
                total=(time.perf_counter_ns()-start)/1e6
                records.append({'tick':k,'mirror':mode,'warmup':k%6==0,'physical_checksum':str(h.value),
                    'image_sha256':hashlib.sha256(frame.tobytes()).hexdigest(),'physics_and_micro_ms':physical_ms,
                    'total_ms':total,'sync_ms':metrics['sync_ms'],'draw_ms':metrics['wall_draw_finish_ms']})
            saved=core.snapshot();epoch=core.epoch;core.restore(saved)
            image,_=engine.observe(cam,t=17*.21,force=True,presentation=1)
            assert hashlib.sha256(image.tobytes()).hexdigest()==records[-1]['image_sha256'] and core.epoch>epoch
            return {'records':records,'final_core_sha256':hashlib.sha256(saved.encode()).hexdigest(),'driver':engine.renderer.gl.info,
                    'median_ms':statistics.median(x['total_ms'] for x in records if not x['warmup']),
                    'rss_kib':resource.getrusage(resource.RUSAGE_SELF).ru_maxrss}
        finally:engine.close();scene.close();lib.mxw_destroy(work)
def main():
    p=argparse.ArgumentParser();p.add_argument('--child',type=Path);p.add_argument('--library',type=Path);p.add_argument('--baseline',default=BASE);p.add_argument('--output',type=Path,default=ROOT/'results/ether-system-combined-2026-10-20.json');a=p.parse_args()
    if a.child:
        resource.setrlimit(resource.RLIMIT_AS,(2*1024**3,2*1024**3));resource.setrlimit(resource.RLIMIT_CPU,(55,56))
        os.sched_setaffinity(0,{min(os.sched_getaffinity(0))});print(json.dumps(child(a.child,a.library)));return
    env=dict(os.environ,OPENBLAS_NUM_THREADS='1',LP_NUM_THREADS='1',OMP_NUM_THREADS='1')
    baseline=subprocess.check_output(['git','rev-parse',a.baseline+'^{commit}'],cwd=ROOT,text=True).strip()
    result={'baseline':baseline,'physics_baseline':PHYSICS,'ram_limit_bytes':2*1024**3,'wall_seconds':60,
      'scope':'Synthetic co-scheduled workload: PhysicsWorld 4096 + MicroLab 16384 + live Ether 256 sites/512 observables + 1M procedural boxes. PhysicsWorld is not yet wired to the 3D scene.',
      'repetitions':[]}
    with tempfile.TemporaryDirectory(prefix='mx-combined-') as tmp:
        before=Path(tmp)/'before';before.mkdir()
        archive=subprocess.check_output(['git','archive',baseline,'native','graphics-desktop'],cwd=ROOT)
        with tarfile.open(fileobj=io.BytesIO(archive)) as tar:tar.extractall(before,filter='data')
        physics=before/'native/include/mundo_x/physics_world.hpp'
        if physics.exists():result['physics_baseline']=baseline
        else:physics.write_bytes(subprocess.check_output(['git','show',PHYSICS+':native/include/mundo_x/physics_world.hpp'],cwd=ROOT))
        libraries={}
        paths=['native/include/mundo_x/'+x+'.hpp' for x in ('physics_world','micro_lab','pauli_observer')]+['graphics-desktop/engine.py','graphics-desktop/renderer.py','graphics-desktop/edit_pages.py','graphics-desktop/shaders/eye.frag','graphics-desktop/shaders/common.glsl','tools/system_workload.cpp','tools/system_combined_check.py']
        measured_hashes={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in paths}
        for label,root in [('before',before),('after',ROOT)]:
            libraries[label]=Path(tmp)/(label+'.so')
            subprocess.run(['g++','-std=c++17','-O3','-shared','-fPIC','-I'+str(root/'native/include'),str(ROOT/'tools/system_workload.cpp'),'-o',str(libraries[label])],check=True,timeout=60)
            subprocess.run([sys.executable,'-c','import sys;sys.path.insert(0,sys.argv[1]);from engine import build_native;build_native()',str(root/'graphics-desktop')],check=True,env=env,timeout=60)
        for repeat in range(3):
            pair={}
            for label in (['before','after'] if repeat%2==0 else ['after','before']):
                root=before if label=='before' else ROOT
                p=subprocess.run([sys.executable,__file__,'--child',str(root),'--library',str(libraries[label])],text=True,capture_output=True,env=env,timeout=60)
                if p.returncode:raise RuntimeError(p.stdout+p.stderr)
                pair[label]=json.loads(p.stdout)
            assert pair['before']['final_core_sha256']==pair['after']['final_core_sha256']
            for x,y in zip(pair['before']['records'],pair['after']['records']):
                assert x['physical_checksum']==y['physical_checksum'] and x['image_sha256']==y['image_sha256'],(repeat,x,y)
            result['repetitions'].append(pair);print(json.dumps({'repeat':repeat,**{label:pair[label]['median_ms'] for label in pair}}),flush=True)
        result['sha256']={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in paths}
        assert result['sha256']==measured_hashes,'Sources changed during campaign'
        a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
if __name__=='__main__':main()
