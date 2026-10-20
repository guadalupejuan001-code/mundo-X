#!/usr/bin/env python3
"""Bounded real GLES host render/camera/core/exception campaign, no phone claims."""
import argparse
import json
import os
from pathlib import Path
import resource
import subprocess
import sys
import time

def child(root,w,h,count,frames):
    resource.setrlimit(resource.RLIMIT_AS,(2*1024**3,2*1024**3))
    import numpy as np
    sys.path.insert(0,str(root/'graphics-desktop'))
    from engine import QuantumScene,QuantumGraphicsEngine,build_native,street
    from infinite_runtime import InfiniteCore
    build_native()
    with InfiniteCore('zero') as core:
        core.execute('h_all\ncz_neighbors\n')
        scene=QuantumScene.from_infinite_core(core,count=count,observables=['X:0 Z:1','Z:0 X:1 Z:2'])
        engine=QuantumGraphicsEngine(scene,w,h)
        try:
            source=core.snapshot();cam=street(w,h);timings=[]
            first,metrics=engine.observe(cam);timings.append(metrics)
            again,reuse=engine.observe(cam)
            assert reuse['reused'] and np.array_equal(first,again)
            forced,forced_metrics=engine.observe(cam,force=True)
            assert np.array_equal(first,forced)
            # Same matrix, different ray projection: the old cache was stale.
            changed=dict(cam);changed['tan_half_fov']*=.7
            cached,cached_metrics=engine.observe(changed)
            expected,_=engine.observe(changed,force=True)
            cache_mismatches=int(np.count_nonzero(np.any(cached!=expected,axis=2)))
            assert core.snapshot()==source
            for i in range(frames):
                core.apply([('Z','0')])
                scene.edit_many({j:(j%7)+1 for j in range(min(count,64+i*64))})
                _,m=engine.observe(street(w,h,(i+1)*.13),t=(i+1)*.7)
                assert not m['reused'];timings.append(m)
            snap=core.snapshot();core.restore(snap)
            _,m=engine.observe(street(w,h,(frames)*.13),t=frames*.7)
            assert not m['reused'];timings.append(m)
            out={'status':'ok','width':w,'height':h,'objects':count,'moving_frames':frames,
                 'same_frame_reused':True,'forced_pixels_equal':True,
                 'camera_cache_mismatched_pixels':cache_mismatches,
                 'camera_cache_invalidated':not cached_metrics['reused'],
                 'driver':engine.renderer.gl.info,'timings':timings,
                 'rss_peak_kib':resource.getrusage(resource.RUSAGE_SELF).ru_maxrss}
        finally:engine.close();scene.close()
    print(json.dumps(out),flush=True)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[1])
    p.add_argument('--output',type=Path)
    p.add_argument('--child',nargs=4,type=int)
    args=p.parse_args()
    if args.child:return child(args.source.resolve(),*args.child)
    results={'scope':'host Mesa GLES, 2 GiB virtual address limit per process; no physical phone',
             'cases':[]}
    cases=[(320,180,1024,20),(320,180,1048576,20),(1920,1080,1048576,5),
           (3840,2160,1048576,3),(7680,4320,1048576,2)]
    env=dict(os.environ,OPENBLAS_NUM_THREADS='1',OMP_NUM_THREADS='1',LP_NUM_THREADS='2')
    for dims in cases:
        t=time.monotonic()
        command=[sys.executable,str(Path(__file__).resolve()),'--source',str(args.source.resolve()),'--child',*map(str,dims)]
        try:
            done=subprocess.run(command,env=env,text=True,capture_output=True,timeout=45)
            try:r=json.loads(done.stdout)
            except ValueError:r={'status':'failed','stderr':done.stderr[-4000:],'stdout':done.stdout[-1000:]}
            r['returncode']=done.returncode
        except subprocess.TimeoutExpired:r={'status':'timeout','budget_seconds':45}
        r.update(case=dims,wall_seconds=time.monotonic()-t);results['cases'].append(r)
        args.output.write_text(json.dumps(results,indent=2)+'\n')
        print(dims,r['status'],round(r['wall_seconds'],3),flush=True)

if __name__=='__main__':main()
