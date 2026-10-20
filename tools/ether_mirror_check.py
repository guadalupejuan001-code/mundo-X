#!/usr/bin/env python3
"""Independent optical oracle, live-Ether checks and bounded-RAM mirror campaign.

Run from the repository root. No screenshot cache is a correctness oracle.
The brute-force oracle tests every explicit box, never the shader's grid DDA
or its folding routine. It reflects one surface at a time in float64.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import resource
import statistics
import subprocess
import sys
import tempfile
import time

ROOT=Path(__file__).resolve().parents[1]
BASE='d7592bb87ae4285161b958939efa292850158726'
sys.path.insert(0,str(ROOT/'graphics-desktop'))


def decode(image):
    import numpy as np
    a=image.astype(np.int64)
    return a[:,:,0]+(a[:,:,1]<<8)+(a[:,:,2]<<16)-1


def raw_frame(renderer,cam,**kwargs):
    """Test-only readback: preserve the visible failure marker for inspection."""
    import numpy as np
    renderer.draw('eye',cam,read=False,**kwargs)
    image=np.empty((renderer.height,renderer.width,4),dtype=np.uint8)
    renderer.gl.glReadPixels(0,0,renderer.width,renderer.height,0x1908,0x1401,image.ctypes.data)
    return np.flipud(image).copy()


def explicit_boxes(count,seed,edits,t,quantum,strength):
    """Independent integer hash + explicit geometry; used ONLY as an oracle."""
    import numpy as np
    side=math.ceil(math.sqrt(count));rows=[]
    for i in range(count):
        h=(i^seed^0x9e3779b9)&0xffffffff
        h^=h>>16;h=h*0x7feb352d&0xffffffff
        h^=h>>15;h=h*0x846ca68b&0xffffffff;h^=h>>16
        x=float(np.float32(np.float32(i%side-(side-1)*.5)*np.float32(2.2)))
        z=float(np.float32(np.float32(i//side-(side-1)*.5)*np.float32(2.2)))
        state=edits.get(i,0)
        height=0 if state==7 else .7+(h&255)/128+state*.35
        height*=1+.08*math.sin(t*.8+x*.07+z*.06)
        height*=1+strength*quantum[i%len(quantum)]
        rows.append((x,z,height))
    data=np.asarray(rows)
    lower=np.column_stack((data[:,0]-.38,np.zeros(count),data[:,1]-.38))
    upper=np.column_stack((data[:,0]+.38,data[:,2],data[:,1]+.38))
    return lower,upper


def ray(cam,width,height,x,y,t):
    import numpy as np
    direction=(cam['forward']+(2*(x+.5)/width-1)*cam['aspect']*cam['tan_half_fov']*cam['right']
               +(1-2*(y+.5)/height)*cam['tan_half_fov']*cam['up'])
    direction=direction/np.linalg.norm(direction)
    cosine=direction@cam['forward']
    origin=cam['eye']+direction*(cam['near']/cosine)
    c,s=math.cos(t*.12),math.sin(t*.12)
    rot=np.array([[c,0,s],[0,1,0],[-s,0,c]])
    return rot.T@origin,rot.T@direction,(cam['far']-cam['near'])/cosine


def brute_ray(ro,rd,remaining,lower,upper,mode,extent,top,limit=4096):
    import numpy as np
    ro=ro.copy();rd=rd.copy();bounces=0
    for query in range(limit+1):
        enter=np.full(len(lower),-np.inf);leave=np.full(len(lower),np.inf)
        valid=upper[:,1]>0
        for axis in range(3):
            if abs(rd[axis])<1e-12:
                valid&=(ro[axis]>=lower[:,axis])&(ro[axis]<=upper[:,axis])
            else:
                a=(lower[:,axis]-ro[axis])/rd[axis];b=(upper[:,axis]-ro[axis])/rd[axis]
                enter=np.maximum(enter,np.minimum(a,b));leave=np.minimum(leave,np.maximum(a,b))
        valid&=(enter>=0)&(leave>=enter)&(enter<=remaining)
        distances=np.where(valid,enter,np.inf)
        object_id=int(np.argmin(distances));best=distances[object_id];kind='object'
        if mode and abs(rd[1])>1e-12:
            value=(-.025-ro[1])/rd[1];p=ro+rd*value
            if 1e-5<value<=remaining and value<best and max(abs(p[0]),abs(p[2]))<=extent:
                best=value;kind='floor'
        if mode==2 and abs(rd[0])>1e-12:
            for sign in (-1,1):
                value=(sign*extent-ro[0])/rd[0];p=ro+rd*value
                if 1e-5<value<=remaining and value<best and -.025<=p[1]<=top and abs(p[2])<=extent:
                    best=value;kind='mirror';wall=sign*extent
        if not np.isfinite(best):return -1,bounces,'sky'
        if kind=='object':return object_id,bounces,'object'
        if kind=='floor' and mode==2:return -1,bounces,'ground'
        ro+=rd*best;remaining-=best;bounces+=1
        if kind=='floor':ro[1]=-.025;rd[1]*=-1
        else:ro[0]=wall;rd[0]*=-1
        if mode==2 and query>=1 and rd[1]==0 and rd[2]==0:return -1,bounces,'closed'
    raise RuntimeError('Independent oracle exceeded its declared work budget')


def geometry_check(output):
    import numpy as np
    from renderer import Renderer,camera
    from PIL import Image
    rng=np.random.default_rng(42);r=Renderer(144,96);cases=[];all_reflected=0;all_ids=0
    try:
        for k in range(16):
            count=(1,9,64,257)[k%4];side=math.ceil(math.sqrt(count));extent=side*1.1+2.2
            edits={} if k<8 else {int(j):int((j+k)%8) for j in range(min(count,32))}
            seed=2026+k*181;moment=(0,1.1,3.7,12.3)[k%4]
            quantum=np.array([1,-1,.25,0],dtype=np.float32);strength=0 if k<4 else .25
            mode=1 if k%2==0 else 2
            r.scene(count,seed,edits,prepare_explicit=False);r.set_quantum(quantum,strength)
            r.set_reflections(mode,budget=64,fold=True)
            if mode==1:
                cam=camera([extent*.3,3,extent*.9],[0,-.025,0],r.width,r.height,near=.01,far=10000,fov=70)
            else:
                cam=camera([0,4.2,extent*.7],[extent,2.5,extent*.3],r.width,r.height,near=.01,far=10000,fov=80)
            image=raw_frame(r,cam,t=moment,debug=1)
            bounce_image=raw_frame(r,cam,t=moment,debug=2)
            incomplete=np.all(image==[255,0,255,255],axis=2)
            actual=decode(image);bounce_counts=decode(bounce_image)
            points={(int(y),int(x)) for y,x in rng.integers([0,0],[r.height,r.width],size=(350,2))}
            reflected_points=np.argwhere((bounce_counts>0)&(actual>=0)&~incomplete)
            points.update(map(tuple,reflected_points[::max(1,len(reflected_points)//150)]))
            lower,upper=explicit_boxes(count,seed,edits,moment,quantum,strength)
            errors=[];bounce_errors=[];reflected=0;checked_limits=0
            top=8.0
            for y,x in sorted(points):
                ro,rd,far=ray(cam,r.width,r.height,x,y,moment)
                expected,bounces,termination=brute_ray(ro,rd,far,lower,upper,mode,extent,top)
                if incomplete[y,x]:
                    assert bounces>64,(k,x,y,'unexpected incomplete ray',bounces)
                    checked_limits+=1;continue
                if actual[y,x]!=expected:errors.append({'pixel':[int(x),int(y)],'actual':int(actual[y,x]),'expected':expected})
                if bounce_counts[y,x]!=bounces:bounce_errors.append([int(x),int(y),int(bounce_counts[y,x]),bounces])
                reflected+=int(expected>=0 and bounces>0)
            assert not errors,(k,errors[:10])
            assert not bounce_errors,(k,bounce_errors[:10])
            all_reflected+=reflected;all_ids+=len(points)
            # Same optical problem: folding must preserve image and IDs.
            folded=raw_frame(r,cam,t=moment,presentation=1)
            r.set_reflections(mode,budget=64,fold=False)
            iterated=raw_frame(r,cam,t=moment,presentation=1)
            exhausted=np.all(iterated==[255,0,255,255],axis=2)
            assert not np.any(incomplete&~exhausted),'Folding introduced an unfinished ray'
            delta=np.abs(folded.astype(int)-iterated.astype(int))
            delta[exhausted]=0
            assert delta.max()<=1,(k,int(delta.max()))
            cases.append({'case':k,'objects':count,'mode':mode,'seed':seed,'time':moment,
                          'oracle_rays':len(points),'reflected_object_rays':reflected,'id_mismatches':errors,
                          'bounce_mismatches':bounce_errors,'fold_max_channel_delta':int(delta.max()),
                          'folded_budget_exhausted_pixels':int(incomplete.sum()),
                          'oracle_verified_work_limits':checked_limits,
                          'iterated_budget_exhausted_pixels':int(exhausted.sum()),
                          'fold_changed_pixels':int(np.any(delta,axis=2).sum())})
        assert all_reflected>300
        # Clear visual example: one rule-defined city, observed directly or through mirrors.
        r.resize(960,540);r.scene(64,seed=2026,prepare_explicit=False);r.set_quantum([1,-1,0,.5],.25)
        cam=camera([0,3.8,7],[11,1.2,-2],960,540,near=.02,far=10000,fov=72)
        output.mkdir(parents=True,exist_ok=True)
        for mode,label in ((0,'sin-espejos'),(2,'espejos-enfrentados'),(1,'suelo-reflectante')):
            r.set_reflections(mode,budget=64)
            _,frame=r.draw('eye',cam,read=True,presentation=1)
            Image.fromarray(frame).save(output/(label+'.png'))
        return {'status':'passed','driver':r.gl.info,'cases':cases,'oracle_rays':all_ids,
                'reflected_object_rays':all_reflected,'explicit_object_arrays':'oracle only'}
    finally:r.close()


def limits_check():
    import numpy as np
    from renderer import Renderer,camera
    r=Renderer(1,1);cases=[]
    try:
        r.scene(1,prepare_explicit=False);r.set_quantum([0],0)
        for dy in (.004,.0004,.00004,.000004,.0000016):
            # A shallow ascending ray remains ABOVE the complete scene envelope.
            # Float32 basis values are the physical inputs of the GPU oracle.
            cam=camera([0,6.9,0],[1,6.9+dy,0],1,1,near=.001,far=1_000_000,fov=48)
            ro,rd,far=ray(cam,1,1,0,0,0)
            ro=ro.astype(np.float32).astype(float);rd=rd.astype(np.float32).astype(float)
            extent=float(np.float32(3.3));top=8.0
            x,y,z=map(float,ro);dx,up,_=map(float,rd);count=0
            # Independent scalar sequential reflection: no fold formula.
            started=time.perf_counter_ns()
            for j in range(200000):
                wall=extent if dx>0 else -extent
                step=(wall-x)/dx
                if step>far or y+up*step>top:break
                x=wall;y+=up*step;far-=step;dx=-dx;count+=1
            else:raise AssertionError('Scalar reference budget')
            oracle_ms=(time.perf_counter_ns()-started)/1e6
            r.set_reflections(2,budget=16,fold=True)
            timing,bounce_img=r.draw('eye',cam,debug=2,read=True)
            _,skip_img=r.draw('eye',cam,debug=3,read=True)
            _,query_img=r.draw('eye',cam,debug=4,read=True)
            observed=int(decode(bounce_img)[0,0]);skipped=int(decode(skip_img)[0,0]);queries=int(decode(query_img)[0,0])
            assert observed==count,(dy,observed,count)
            assert queries<=5,(dy,queries)
            _,folded=r.draw('eye',cam,read=True,presentation=1)
            ref='budget_exceeded';iteration_ms=None;max_delta=None
            if count<=64:
                r.set_reflections(2,budget=64,fold=False)
                baseline,full=r.draw('eye',cam,read=True,presentation=1)
                ref='completed';iteration_ms=baseline['wall_draw_finish_ms']
                max_delta=int(np.abs(folded.astype(int)-full.astype(int)).max());assert max_delta<=1
            else:
                r.set_reflections(2,budget=64,fold=False)
                try:r.draw('eye',cam,read=True)
                except RuntimeError as e:assert 'Incomplete ray' in str(e)
                else:raise AssertionError('Unfinished reference ray was hidden')
            cases.append({'dy':dy,'reflections':count,'compacted':skipped,'scene_queries':queries,
                          'fold_draw_ms':timing['wall_draw_finish_ms'],'sequential_reference_ms':oracle_ms,
                          'iterated_gpu':ref,'iterated_gpu_ms':iteration_ms,'max_channel_delta':max_delta})
        # The on-axis orbit has no exit and no source; solve the certified cycle.
        cam=camera([0,4,0],[1,4,0],1,1,near=.001,far=10000)
        r.set_reflections(2,budget=16,fold=True)
        _,frame=r.draw('eye',cam,read=True,presentation=1);assert list(frame[0,0])==[0,0,0,255]
        # Adversarial configurations rejected before modifying the observer.
        previous=r.mirror_settings;rejected=0
        for kwargs in ({'mode':3},{'mode':-1},{'mode':True},{'budget':0},{'budget':65},{'budget':1.5},
                       {'fold':1},{'reflectance':float('nan')},{'reflectance':float('inf')},
                       {'reflectance':-1},{'reflectance':1.01}):
            try:r.set_reflections(**kwargs)
            except ValueError:rejected+=1
            else:raise AssertionError(kwargs)
            assert r.mirror_settings==previous
        return {'status':'passed','cases':cases,'closed_orbit':'black least-radiance solution',
                'invalid_settings_rejected_atomically':rejected,'work_limit_visible':True}
    finally:r.close()


def pipeline_check():
    import numpy as np
    from engine import QuantumScene,QuantumGraphicsEngine
    from infinite_runtime import InfiniteCore
    from renderer import camera
    with InfiniteCore() as core:
        core.execute('h_all\ncz_neighbors\n'*63+'h_all\n')
        scene=QuantumScene.from_infinite_core(core,count=64,observables=['X:63 Z:62'])
        engine=QuantumGraphicsEngine(scene,192,128)
        try:
            cam=camera([0,3.8,7],[11,1.2,-2],192,128,near=.02,far=10000,fov=72)
            engine.renderer.set_reflections(2,budget=64)
            before=core.snapshot();first,m=engine.observe(cam,presentation=1)
            again,reuse=engine.observe(cam,presentation=1)
            assert reuse['reused'] and np.array_equal(first,again)
            forced,m=engine.observe(cam,presentation=1,force=True);assert np.array_equal(first,forced)
            assert core.snapshot()==before
            batches=scene.observable_batches
            _,image=engine.renderer.draw('eye',cam,debug=1,read=True)
            _,bounce=engine.renderer.draw('eye',cam,debug=2,read=True)
            targets=np.argwhere((decode(image)>=0)&(decode(bounce)>0));assert len(targets)>0
            y,x=map(int,targets[len(targets)//2]);chosen=int(decode(image)[y,x])
            assert engine.pick(cam,x,y)==chosen
            scene.edit(chosen,7);hidden,m=engine.observe(cam,presentation=1)
            assert not m['reused'] and engine.pick(cam,x,y)!=chosen and core.snapshot()==before
            scene.edit(chosen,0);restored,m=engine.observe(cam,presentation=1)
            assert np.array_equal(first,restored) and engine.pick(cam,x,y)==chosen
            assert scene.observable_batches==batches
            core.apply([('Z','63')]);changed,m=engine.observe(cam,presentation=1)
            assert not m['reused'] and engine.renderer.quantum[0]==-1
            changed_pixels=int(np.any(changed!=first,axis=2).sum());assert changed_pixels>0
            saved=core.snapshot();core.restore(saved)
            same,m=engine.observe(cam,presentation=1);assert not m['reused'] and np.array_equal(same,changed)
            for mode,budget,fold,reflectance in ((0,64,True,1.),(1,64,True,1.),(2,32,True,1.),(2,32,False,1.),(2,32,False,.7)):
                engine.renderer.set_reflections(mode,budget=budget,fold=fold,reflectance=reflectance)
                # This checks invalidation, independently of whether the
                # deliberately smaller iterative budget completes every pixel.
                _,m=engine.observe(cam,presentation=1,read=False);assert not m['reused']
            _,m=engine.observe(cam,presentation=0,read=False);assert not m['reused']
            return {'status':'passed','read_only':True,'exact_reuse':True,'forced_pixels_identical':True,
                    'reflected_pick_hide_restore':True,'picked_id':chosen,'reflected_pixels':len(targets),
                    'gate_changed_pixels':changed_pixels,'restore_invalidates':True,
                    'optics_and_presentation_in_cache_key':True,'same_core_snapshot_for_all_bounces':True}
        finally:engine.close();scene.close()


def periodic_hits_check():
    """Long paths that actually find an object; a sky-only test is insufficient."""
    import numpy as np
    from renderer import Renderer,camera
    r=Renderer(1,1);cases=[]
    try:
        for dy,hidden in ((.001,False),(.0001,False),(.00001,False),(.000004,False),(.000004,True)):
            edits={0:7} if hidden else {}
            r.scene(1,seed=2026,edits=edits,prepare_explicit=False);r.set_quantum([0],0)
            lower,upper=explicit_boxes(1,2026,edits,0,[0],0)
            cam=camera([0,3,0],[1,3-dy,0],1,1,near=.001,far=1_000_000)
            ro,rd,far=ray(cam,1,1,0,0,0)
            started=time.perf_counter_ns()
            object_id,count,end=brute_ray(ro,rd,far,lower,upper,2,3.3,8.0,limit=160000)
            oracle_ms=(time.perf_counter_ns()-started)/1e6
            r.set_reflections(2,budget=16,fold=True)
            _,ids=r.draw('eye',cam,debug=1,read=True)
            _,bounces=r.draw('eye',cam,debug=2,read=True)
            _,queries=r.draw('eye',cam,debug=4,read=True)
            assert int(decode(ids)[0,0])==object_id
            assert int(decode(bounces)[0,0])==count,(dy,hidden,count,int(decode(bounces)[0,0]))
            assert object_id==(-1 if hidden else 0)
            cases.append({'dy':dy,'hidden':hidden,'hit_id':object_id,'reflections':count,
                          'termination':end,'scene_queries':int(decode(queries)[0,0]),'oracle_ms':oracle_ms})
        return {'status':'passed','cases':cases,'oracle':'sequential float64 slab intersections, all physical boxes'}
    finally:r.close()


def virtual_camera_check():
    """A floor mirror must agree with the original renderer at a virtual eye."""
    import numpy as np
    from renderer import Renderer,street
    r=Renderer(320,180);records=[]
    try:
        # Original primary-ray traversal, using the same surface material.
        # The material snaps the mathematical y=0 bottom face before a floor()
        # pattern lookup; the old material could disagree at signed roundoff.
        shaders=ROOT/'graphics-desktop/shaders'
        material='#define MX_MIRRORS 1\n'+(shaders/'common.glsl').read_text()+'\n#undef MX_MIRRORS\n'
        fragment=(shaders/'eye.frag').read_text().replace('// @COMMON@',material)
        fragment=fragment.replace('#version 300 es','#version 300 es\n#define MX_STABLE_NORMALS 1',1)
        reference_program=r.gl.program((shaders/'eye.vert').read_text(),fragment)
        r.programs['eye']=reference_program;r.locations[reference_program]={}
        transform=np.diag([1.,-1.,1.,1.]);transform[1,3]=-.05
        for k in range(4):
            r.scene(64 if k<2 else 1024,seed=2026+k*17,edits={0:6,11:7},prepare_explicit=False)
            r.set_quantum([1,-1,0,.5],.25)
            cam=street(320,180,k*.13);moment=k*.71
            r.set_reflections(1,budget=16)
            _,actual=r.draw('eye',cam,t=moment,read=True,presentation=1)
            _,ids=r.draw('eye',cam,t=moment,debug=1,read=True)
            _,counts=r.draw('eye',cam,t=moment,debug=2,read=True)
            virtual=dict(cam)
            for name in ('eye','target'):virtual[name]=(transform@np.r_[cam[name],1.])[:3]
            for name in ('forward','right','up'):virtual[name]=transform[:3,:3]@cam[name]
            virtual['vp']=cam['vp']@transform;virtual['inverse']=transform@cam['inverse']
            r.set_reflections(0)
            _,reference=r.draw('eye',virtual,t=moment,read=True,presentation=1)
            _,reference_ids=r.draw('eye',virtual,t=moment,debug=1,read=True)
            mask=decode(counts)==1
            assert mask.sum()>100
            wrong_ids=int(np.sum(decode(ids)[mask]!=decode(reference_ids)[mask]))
            delta=np.abs(actual.astype(int)-reference.astype(int))[mask]
            assert wrong_ids==0,(k,wrong_ids)
            assert delta.max()<=1,(k,int(delta.max()))
            records.append({'case':k,'compared_reflection_pixels':int(mask.sum()),'different_ids':wrong_ids,
                            'max_channel_delta':int(delta.max())})
        return {'status':'passed','cases':records,'reference':'primary-only traversal with a reflected camera and the same surface-consistent material; no bounce code'}
    finally:r.close()


def tiling_check():
    import ctypes as C
    import numpy as np
    from renderer import Renderer,camera
    r=Renderer(130,98)
    try:
        r.scene(64,edits={0:6,10:7},prepare_explicit=False);r.set_quantum([1,-1],.25)
        cam=camera([0,3.8,7],[11,1.2,-2],130,98,near=.02,far=10000,fov=72)
        comparisons=[]
        for mode in (1,2):
            r.set_reflections(mode,budget=64)
            for debug in (0,1):
                _,full=r.draw('eye',cam,read=True,debug=debug,presentation=1)
                assembled=np.empty_like(full);g=r.gl;p=r.programs['mirrors']
                # Reuse the existing target and perform independent tile draws.
                for y in range(0,r.height,18):
                    for x in range(0,r.width,26):
                        width=min(26,r.width-x);height=min(18,r.height-y)
                        g.glViewport(0,0,width,height)
                        g.glUniform2f(r.loc(p,'uPixelOrigin'),x,y)
                        g.glClear(0x4000|0x0100);g.glDrawArrays(4,0,3)
                        data=np.empty((height,width,4),dtype=np.uint8)
                        g.glReadPixels(0,0,width,height,0x1908,0x1401,data.ctypes.data)
                        assembled[r.height-y-height:r.height-y,x:x+width]=np.flipud(data)
                g.glUniform2f(r.loc(p,'uPixelOrigin'),0,0);g.check('mirror tiles')
                assert np.array_equal(full,assembled),(mode,debug,int(np.any(full!=assembled,axis=2).sum()))
                comparisons.append({'mode':mode,'debug':debug,'pixels':r.width*r.height,'different_pixels':0})
        return {'status':'passed','images':comparisons,'derivatives_inside_bounce_loop':False}
    finally:r.close()


def benchmark_check():
    import numpy as np
    from renderer import Renderer,camera,street
    r=Renderer(320,180);records=[]
    try:
        for count,width,height in ((1024,320,180),(1_048_576,640,360)):
            r.resize(width,height);r.scene(count,prepare_explicit=False)
            side=math.ceil(math.sqrt(count));extent=side*1.1+2.2
            for mode in (0,1,2):
                r.set_reflections(mode,budget=64)
                samples=[]
                for k in range(7):
                    cam=camera([0,4.2,extent*.7],[extent,2.5,extent*(.3+k*.01)],width,height,near=.02,far=10000,fov=72)
                    timing,image=r.draw('eye',cam,t=k*.17,read=True,presentation=1)
                    samples.append(timing['wall_draw_finish_ms'])
                records.append({'objects':count,'width':width,'height':height,'mode':mode,
                                'draw_ms':samples,'median_warm_ms':statistics.median(samples[1:]),
                                'explicit_object_bytes':0,'mirror_framebuffer_bytes':0,
                                'rss_peak_kib':resource.getrusage(resource.RUSAGE_SELF).ru_maxrss})
        # Same image, seed, camera, precision and mirror material. All iterative
        # reference rays finish, so elapsed times compare equivalent work.
        r.resize(320,180);r.scene(1,prepare_explicit=False)
        comparison=[];compared_pixels=0
        for k in range(9):
            cam=camera([0,6.9+k*.001,0],[1,6.9126+k*.0011,0],320,180,near=.001,far=10000,fov=1)
            pair={}
            for fold in ((False,True) if k%2==0 else (True,False)):
                r.set_reflections(2,budget=64,fold=fold)
                timing,frame=r.draw('eye',cam,read=True,presentation=1)
                pair[fold]=frame
                comparison.append({'view':k,'fold':fold,**timing})
            assert np.array_equal(pair[False],pair[True])
            compared_pixels+=320*180
        return {'status':'passed','driver':r.gl.info,'records':records,'new_camera_and_time_each_frame':True,
                'same_image_comparison':{'different_pixels':0,'compared_pixels':compared_pixels,'samples':comparison,
                  'median_ms':{str(fold):statistics.median(s['wall_draw_finish_ms'] for s in comparison
                                    if s['fold']==fold and s['view']>0) for fold in (False,True)}}}
    finally:r.close()


def legacy_check():
    from renderer import source
    from ether_render_evaluation_check import shader_check
    paths=('eye.vert','eye.frag','common.glsl');tokens=[]
    with tempfile.TemporaryDirectory(prefix='mx-mirror-baseline-') as directory:
        baseline=Path(directory);shaders=baseline/'graphics-desktop/shaders';shaders.mkdir(parents=True)
        for name in paths:
            (shaders/name).write_bytes(subprocess.check_output(['git','show',BASE+':graphics-desktop/shaders/'+name],cwd=ROOT))
        old=(shaders/'eye.frag').read_text().replace('// @COMMON@',(shaders/'common.glsl').read_text())
        for text in (old,source('eye.frag')):
            parsed=subprocess.run(['cpp','-P','-undef','-nostdinc'],input=text.replace('#version 300 es',''),
                                  text=True,capture_output=True,check=True).stdout
            tokens.append(''.join(parsed.split()))
        # The edit-page optimization changes instructions and the GPU texture
        # layout. Retain the exact old shader AND its old texture as the oracle.
        # shader_check requires every output pixel to remain byte-identical.
        return {'status':'passed','preprocessed_shader_identical':tokens[0]==tokens[1],
                'comparisons':[shader_check(ROOT,baseline,*shape) for shape in ((320,180,1024),(960,540,1048576))]}


def lifetime_check():
    from engine import QuantumScene,QuantumGraphicsEngine
    from infinite_runtime import InfiniteCore
    from renderer import camera
    records=[]
    for repetition in range(8):
        with InfiniteCore() as core:
            scene=QuantumScene.from_infinite_core(core,count=64,observables=['Z:0'])
            engine=QuantumGraphicsEngine(scene,192,128)
            try:
                engine.renderer.set_reflections(2,budget=64)
                cam=camera([0,3.8,7],[11,1.2,-2],192,128,near=.02,far=10000,fov=72)
                before=core.snapshot();frame,_=engine.observe(cam,presentation=1)
                assert core.snapshot()==before
                records.append({'iteration':repetition,'sha256':hashlib.sha256(frame.tobytes()).hexdigest(),
                                'rss_peak_kib':resource.getrusage(resource.RUSAGE_SELF).ru_maxrss})
            finally:engine.close();scene.close()
    assert len({v['sha256'] for v in records})==1
    return {'status':'passed','core_and_gl_lifetimes':8,'records':records}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,default=ROOT/'results/ether-mirrors-2026-10-20.json')
    p.add_argument('--images',type=Path,default=ROOT/'results/ether-mirror-images')
    p.add_argument('--child',choices=['geometry','limits','periodic_hits','virtual','pipeline','tiles','benchmark','legacy','lifetime'])
    args=p.parse_args()
    if args.child:
        # Limits are imposed before importing numpy or opening the driver.
        resource.setrlimit(resource.RLIMIT_AS,(2*1024**3,2*1024**3))
        resource.setrlimit(resource.RLIMIT_CPU,(55,56))
        os.sched_setaffinity(0,{min(os.sched_getaffinity(0))})
        functions={'geometry':lambda:geometry_check(args.images),'limits':limits_check,'periodic_hits':periodic_hits_check,
                   'virtual':virtual_camera_check,'pipeline':pipeline_check,
                   'tiles':tiling_check,'benchmark':benchmark_check,'legacy':legacy_check,'lifetime':lifetime_check}
        started=time.perf_counter_ns();report=functions[args.child]()
        report.update(wall_ms=(time.perf_counter_ns()-started)/1e6,
                      rss_peak_kib=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
                      address_space_limit_bytes=2*1024**3)
        print(json.dumps(report));return
    env=os.environ.copy();env.update(OPENBLAS_NUM_THREADS='1',LP_NUM_THREADS='1',OMP_NUM_THREADS='1',LIBGL_ALWAYS_SOFTWARE='1')
    report={'base':BASE,'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
            'seed':42,'status':'running','measurements':'Host EGL software; no Android or phone FPS claim','checks':{}}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    for name in ('geometry','limits','periodic_hits','virtual','pipeline','tiles','benchmark','legacy','lifetime'):
        child=subprocess.run([sys.executable,__file__,'--child',name,'--images',str(args.images)],
                             cwd=ROOT,env=env,text=True,capture_output=True,timeout=60)
        if child.returncode:
            report['status']='failed';report['failure']={'case':name,'stdout':child.stdout,'stderr':child.stderr}
            args.output.write_text(json.dumps(report,indent=2)+'\n')
            raise RuntimeError(name+'\n'+child.stdout+'\n'+child.stderr)
        report['checks'][name]=json.loads(child.stdout);print(name+': passed',flush=True)
        args.output.write_text(json.dumps(report,indent=2)+'\n')
    paths=['graphics-desktop/shaders/eye.frag','graphics-desktop/shaders/common.glsl',
           'graphics-desktop/renderer.py','graphics-desktop/engine.py','tools/ether_mirror_check.py']
    report['source_sha256']={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in paths}
    report['status']='passed'
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(args.output)

if __name__=='__main__':main()
