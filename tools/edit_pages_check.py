#!/usr/bin/env python3
"""Exhaustive CPU/GLES decoding, plus optional byte-exact Java encoder fixtures."""
import argparse,hashlib,json,os,resource,statistics,sys,time,tracemalloc
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'graphics-desktop'))

def fixtures():
    yield 1,{},4096
    yield 257,{0:0,256:0},4096
    yield 257,{i:1+i%7 for i in [0,7,8,255,256]},4096
    yield 1048576,{i:i%8 for i in range(64)},4096
    yield 1000003,{(j*7919)%1000003:j%8 for j in range(4096)},4096
    yield 65536,{i:(i*13+5)%8 for i in range(65536)},4096
    yield 1048576,{i:(i*13+5)%8 for i in range(1048576)},4096

def check(java=None):
    import numpy as np
    from edit_pages import pack_edits
    from renderer import Renderer,source,street
    from render_test_support import legacy_edit_data
    r=Renderer(1024,1024);g=r.gl;rows=[];checked=0
    # This runs the production GLSL decoder, without geometric traversal hiding IDs.
    shader=source('eye.frag').replace('void main(){','void unusedMain(){')+'''
void main(){int id=int(gl_FragCoord.y)*1024+int(gl_FragCoord.x);
 int state=id<uCount&&uEditCount>0?editState(id):0;
 outColor=vec4(float(state)/255.0,0,0,1);gl_FragDepth=0.0;}
'''
    p=g.program(source('eye.vert'),shader);r.programs['eye']=p;r.locations[p]={}
    try:
        for k,(count,edits,maximum) in enumerate(fixtures()):
            packed=pack_edits(count,edits,maximum);flat=packed.ravel()
            expected=np.zeros(count,dtype=np.uint32)
            for i,state in edits.items():expected[i]=state
            ids=np.arange(count,dtype=np.uint32)
            if edits:
                base=flat[ids>>8];word=np.where(base!=0,base+((ids&255)>>3),0)
                actual=np.where(base!=0,(flat[word]>>((ids&7)*4))&7,0)
            else:actual=np.zeros(count,dtype=np.uint32)
            assert np.array_equal(actual,expected);checked+=count
            if java:assert (java/f'{k}.bin').read_bytes()==packed.tobytes(),f'Java/Python case {k}'
            r.scene(count,edits=edits,prepare_explicit=False)
            # Exercise the encoded offsets across texture rows.
            h,w=packed.shape;g.glActiveTexture(0x84C0);g.glBindTexture(0x0DE1,r.edits_texture)
            g.glTexImage2D(0x0DE1,0,0x8236,w,h,0,0x8D94,0x1405,packed.ctypes.data)
            _,image=r.draw('eye',street(1024,1024),read=True)
            output=np.flipud(image)[:,:,0].ravel()[:count]
            assert np.array_equal(output,expected),f'GPU decode case {k}'
            timings={'before':[],'after':[]};trace={}
            for repeat in range(3):
                for label in (['before','after'] if repeat%2 else ['after','before']):
                    start=time.perf_counter_ns()
                    buffer=legacy_edit_data(edits) if label=='before' else pack_edits(count,edits,maximum)
                    timings[label].append((time.perf_counter_ns()-start)/1e6)
            if k==6:
                # Trace separately from timings. This covers Python/NumPy
                # tracked allocations, excluding the preexisting edits and GL.
                for label in ['before','after']:
                    tracemalloc.start();tracemalloc.reset_peak()
                    buffer=legacy_edit_data(edits) if label=='before' else pack_edits(count,edits,maximum)
                    current,peak=tracemalloc.get_traced_memory();tracemalloc.stop()
                    trace[label]={'current_bytes':current,'peak_bytes':peak,'buffer_bytes':buffer.nbytes}
            rows.append({'case':k,'count':count,'edits':len(edits),'bytes':packed.nbytes,
                'shape':list(packed.shape),'sha256':hashlib.sha256(packed.tobytes()).hexdigest(),
                'packing_samples_ms':timings,'packing_median_ms':{label:statistics.median(v) for label,v in timings.items()},
                'tracked_packing_allocations':trace})
        for count,edits,maximum in [(0,{},1),(1048577,{},4096),(1,{-1:0},1),(1,{1:0},1),
                                     (1,{0:-1},1),(1,{0:8},1),(1,{},0),(1048576,{0:1},1)]:
            try:pack_edits(count,edits,maximum)
            except ValueError:pass
            else:raise AssertionError('Invalid input accepted')
        return {'status':'passed','cpu_and_gpu_ids_each':checked,'java_byte_exact':java is not None,
            'fixtures':rows,'driver':g.info,'rss_peak_kib':resource.getrusage(resource.RUSAGE_SELF).ru_maxrss}
    finally:r.close()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--java-fixtures',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
    os.sched_setaffinity(0,{min(os.sched_getaffinity(0))});resource.setrlimit(resource.RLIMIT_AS,(2*1024**3,2*1024**3))
    resource.setrlimit(resource.RLIMIT_CPU,(55,56));result=check(a.java_fixtures);text=json.dumps(result,indent=2)+'\n'
    if a.output:a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(text)
    print(text)
