#!/usr/bin/env python3
"""Exact shader and live Ether-to-image comparisons, bounded to 2 GiB per child."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import resource
import statistics
import subprocess
import sys
import tarfile
import tempfile
import time

BASE = 'f096930749a8945ebb85678ed61f8273e01a0ad7'


def shader_check(root, baseline, width, height, count):
    import numpy as np
    sys.path.insert(0, str(root/'graphics-desktop'))
    from renderer import Renderer, source, street, overview, camera
    r = Renderer(width, height)
    old = baseline/'graphics-desktop/shaders'
    previous = r.gl.program((old/'eye.vert').read_text(),
        (old/'eye.frag').read_text().replace('// @COMMON@', (old/'common.glsl').read_text()))
    programs = {'before': previous, 'after': r.programs['eye']}
    r.locations[previous] = {}
    # The reference shader requires its original signed RG32I search list.
    # New paged shaders use R32UI: comparing both against one texture is invalid.
    from render_test_support import legacy_edit_texture
    legacy_layout='uniform highp isampler2D uEdits;' in (old/'common.glsl').read_text()
    current_texture=r.edits_texture
    old_texture=r.gl.object('glGenTextures') if legacy_layout else current_texture
    samples = []
    try:
        for k in range(12):
            # Fresh seeds, exceptions, observations and camera rays. No frame cache.
            edits = {} if k < 6 else {int(i): int((i+k)%8) for i in np.linspace(0,count-1,min(300,count))}
            r.edits_texture=current_texture
            r.scene(count, seed=(2026+k*1234567)&0xffffffff, edits=edits, prepare_explicit=False)
            if legacy_layout:legacy_edit_texture(r,edits,old_texture)
            r.set_quantum([1,0,-1,.5], 0. if k%3==0 else .25)
            cam = street(width,height,k*.19) if k%3 else overview(r.side,width,height,k*.17)
            if k==4:
                cam=camera([0,2,0],[1,2,1],width,height,near=.01,far=5000,fov=70)
            pair = {}
            for label in (['before','after'] if k%2==0 else ['after','before']):
                r.programs['eye'] = programs[label]
                r.edits_texture=old_texture if label=='before' else current_texture
                metrics, frame = r.draw('eye',cam,t=k*.71,debug=int(k%4==2),
                                        presentation=k%2,read=True)
                pair[label] = frame
                samples.append({'view':k,'version':label,'edits':len(edits),
                                'cold_shader':k==0, **metrics})
            if not np.array_equal(pair['before'],pair['after']):
                mismatch=int(np.any(pair['before']!=pair['after'],axis=2).sum())
                raise AssertionError(f'{width}x{height}, view {k}: {mismatch} differing pixels')
        return {'status':'passed','driver':r.gl.info,'views':12,'compared_pixels':width*height*12,
                'different_pixels':0,'samples':samples,
                'median_ms':{label:statistics.median(s['wall_draw_finish_ms'] for s in samples
                              if s['version']==label and not s['cold_shader']) for label in programs}}
    finally:
        r.close()


def pipeline_check(root, width, height, count):
    import numpy as np
    sys.path.insert(0,str(root/'graphics-desktop'))
    from engine import QuantumScene,QuantumGraphicsEngine,street
    from infinite_runtime import InfiniteCore
    records=[]
    with InfiniteCore() as core:
        # Odd alternating word is self-inverse: U Z0 U = X63 Z62.
        core.execute('h_all\ncz_neighbors\n'*63+'h_all\n')
        observables=['X:63 Z:62']
        for k in range(64):
            observables.append(' '.join(f'{"XYZ"[(j+k)%3]}:{100+k+j}' for j in range(32)))
        scene=QuantumScene.from_infinite_core(core,count=count,observables=observables)
        engine=QuantumGraphicsEngine(scene,width,height)
        try:
            cam=street(width,height);saved=core.snapshot()
            first,metrics=engine.observe(cam)
            assert engine.renderer.quantum[0]==1
            repeated,reuse=engine.observe(cam)
            forced,_=engine.observe(cam,force=True)
            assert reuse['reused'] and np.array_equal(first,repeated) and np.array_equal(first,forced)
            assert core.snapshot()==saved
            def record(frame, metrics, **extra):
                records.append({'sha256':hashlib.sha256(frame.tobytes()).hexdigest(),
                                'coefficients':engine.renderer.quantum.tolist(),
                                'metrics':metrics,**extra})
            record(first,metrics,kind='initial')
            # State change alone must change the coupled image at fixed camera/time.
            core.apply([('Z','63')]);changed,metrics=engine.observe(cam)
            assert engine.renderer.quantum[0]==-1
            changed_pixels=int(np.any(first!=changed,axis=2).sum());assert changed_pixels>0
            record(changed,metrics,kind='causal_gate',changed_pixels=changed_pixels)
            for k in range(8):
                started=time.perf_counter_ns()
                core.apply([('Z','63')])
                scene.edit_many({int(j):(k+j)%8 for j in range(min(count,32*(k+1)))})
                frame,metrics=engine.observe(street(width,height,(k+1)*.17),t=(k+1)*.61)
                assert not metrics['reused']
                metrics['gate_to_readback_ms']=(time.perf_counter_ns()-started)/1e6
                record(frame,metrics,kind='moving')
            saved=core.snapshot();core.restore(saved)
            frame,metrics=engine.observe(street(width,height,8*.17),t=8*.61)
            assert not metrics['reused'] and records[-1]['sha256']==hashlib.sha256(frame.tobytes()).hexdigest()
            record(frame,metrics,kind='restored')
            return {'status':'passed','driver':engine.renderer.gl.info,'records':records,
                    'same_frame_reused':True,'forced_same_pixels':True,'observations_read_only':True,
                    'restore_invalidated':True,'final_state_sha256':hashlib.sha256(saved.encode()).hexdigest(),
                    'observable_batches':scene.observable_batches,
                    'median_moving_total_ms':statistics.median(x['metrics']['gate_to_readback_ms'] for x in records if x['kind']=='moving')}
        finally:
            engine.close();scene.close()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path)
    p.add_argument('--child',choices=['shader','pipeline'])
    p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[1])
    p.add_argument('--baseline',type=Path)
    p.add_argument('--case',nargs=3,type=int)
    args=p.parse_args()
    if args.child:
        resource.setrlimit(resource.RLIMIT_AS,(2*1024**3,2*1024**3))
        resource.setrlimit(resource.RLIMIT_CPU,(43,44))
        os.sched_setaffinity(0,{min(os.sched_getaffinity(0))})
        result=(shader_check(args.source,args.baseline,*args.case) if args.child=='shader'
                else pipeline_check(args.source,*args.case))
        # This process does not compile C++: RSS belongs to the actual render workload.
        result['process_peak_rss_kib']=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        print(json.dumps(result),flush=True);return
    if args.output is None:p.error('--output required')
    root=args.source.resolve()
    if (root/'graphics-desktop/shaders/eye.frag').read_bytes() != (root/'graphics-android/app/src/main/assets/shaders/eye.frag').read_bytes():
        raise AssertionError('desktop and Android eye shaders differ')
    evidence={'baseline':BASE,'memory_budget':'2 GiB RLIMIT_AS per child',
              'time_budget':'45 wall seconds, 43 CPU seconds per child',
              'cpu_affinity':[min(os.sched_getaffinity(0))],
              'scope':'Mesa GLES on host; driver reported; no phone FPS claims',
              'shader_cases':[],'pipeline_cases':[],
              'source_sha256':{name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in
                  ['graphics-desktop/shaders/eye.frag','graphics-android/app/src/main/assets/shaders/eye.frag',
                   'native/include/mundo_x/ether_core.hpp','native/include/mundo_x/infinite_core.hpp']}}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    def save():args.output.write_text(json.dumps(evidence,indent=2)+'\n')
    env=dict(os.environ,OPENBLAS_NUM_THREADS='1',OMP_NUM_THREADS='1',LP_NUM_THREADS='2')
    with tempfile.TemporaryDirectory(prefix='ether-render-eval-') as directory:
        before=Path(directory)
        archive=subprocess.check_output(['git','archive',BASE,'native','graphics-desktop'],cwd=root)
        with tarfile.open(fileobj=io.BytesIO(archive)) as tar:tar.extractall(before,filter='data')
        # Build outside the measured, RAM-limited process for each exact source tree.
        for source_root in [before,root]:
            code='import sys;sys.path.insert(0,sys.argv[1]);from engine import build_native;build_native()'
            subprocess.run([sys.executable,'-c',code,str(source_root/'graphics-desktop')],env=env,check=True)
        def child(mode,source,case):
            command=[sys.executable,str(Path(__file__).resolve()),'--child',mode,'--source',str(source),
                     '--baseline',str(before),'--case',*map(str,case)]
            try:
                out=subprocess.run(command,text=True,capture_output=True,env=env,timeout=45)
                if out.returncode:return {'status':'failure','returncode':out.returncode,'stderr':out.stderr[-4000:]}
                return json.loads(out.stdout)
            except subprocess.TimeoutExpired:return {'status':'wall_budget_reached'}
        for case in [(320,180,1024),(960,540,1048576),(1920,1080,1048576)]:
            result=child('shader',root,case);evidence['shader_cases'].append({'case':case,**result});save()
            print(json.dumps({'shader':case,'status':result['status'],'median_ms':result.get('median_ms')}),flush=True)
        for i,case in enumerate([(320,180,1024),(960,540,1048576)]):
            pair={}
            for label in (['before','after'] if i%2==0 else ['after','before']):
                pair[label]=child('pipeline',before if label=='before' else root,case)
            if all(v['status']=='passed' for v in pair.values()):
                for field in ['final_state_sha256','observable_batches']:
                    if pair['before'][field]!=pair['after'][field]:raise AssertionError(f'{case}: {field} differs')
                if len(pair['before']['records'])!=len(pair['after']['records']):raise AssertionError('missing frames')
                for a,b in zip(pair['before']['records'],pair['after']['records']):
                    for field in ['sha256','coefficients']:
                        if a[field]!=b[field]:raise AssertionError(f'{case}: {field} differs')
            evidence['pipeline_cases'].append({'case':case,**pair});save()
            print(json.dumps({'pipeline':case,**{label:{'status':v['status'],'median_ms':v.get('median_moving_total_ms')}
                                               for label,v in pair.items()}}),flush=True)
    save()
    if any(c['status']!='passed' for c in evidence['shader_cases']):raise SystemExit('shader validation failed/budgeted')
    if any(c[l]['status']!='passed' for c in evidence['pipeline_cases'] for l in ['before','after']):
        raise SystemExit('pipeline validation failed/budgeted')


if __name__=='__main__':main()
