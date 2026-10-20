#!/usr/bin/env python3
"""Full ID framebuffer versus one-pixel picking, identical fresh ray queries."""
import argparse,hashlib,json,os,resource,statistics,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE='c13e78f91887b45d735c5fe3d8a07a000bd03d34'
os.environ.setdefault('OPENBLAS_NUM_THREADS','1');os.environ.setdefault('LP_NUM_THREADS','1')
sys.path.insert(0,str(ROOT/'graphics-desktop'))
def check(width,height,count):
    import numpy as np
    from renderer import Renderer,street,overview
    from ether_mirror_check import raw_frame
    r=Renderer(width,height);samples=[];checked=0;reflected=0
    try:
        for k in range(9):
            edits={} if k<3 else {int(j):int((j+k)%8) for j in np.linspace(0,count-1,min(count,300))}
            r.scene(count,(2026+k*1234567)&0xffffffff,edits,prepare_explicit=False);r.set_quantum([1,0,-1,.5],.25)
            cam=street(width,height,k*.19) if k%3 else overview(r.side,width,height,k*.17)
            for mode in (0,1,2):
                r.set_reflections(mode,budget=64)
                # Warm each specialization separately; compilation is not a gain.
                _,reference=r.draw('eye',cam,t=k*.71,debug=1,read=True)
                _,colors=r.draw('eye',cam,t=k*.71,presentation=1,read=True)
                points=[(0,0),(width-1,height-1),(width//2,height//2)]
                occupied=np.argwhere(np.any(reference[:,:,:3]!=0,axis=2))
                if len(occupied):
                    points+=[(int(x),int(y)) for y,x in occupied[::max(1,len(occupied)//4)][:4]]
                if mode:
                    bounces=raw_frame(r,cam,t=k*.71,debug=2)
                    mask=(bounces[:,:,0].astype(int)+(bounces[:,:,1].astype(int)<<8)+(bounces[:,:,2].astype(int)<<16)>1)&np.any(reference[:,:,:3]!=0,axis=2)
                    hits=np.argwhere(mask)
                    if len(hits):y,x=hits[len(hits)//2];points.append((int(x),int(y)));reflected+=1
                for x,y in points:
                    _,one=r.draw('eye',cam,t=k*.71,debug=1,read=True,pixel=(x,y))
                    assert np.array_equal(one[0,0],reference[y,x]),(k,mode,x,y)
                    assert one.nbytes==4;checked+=1
                # A subsequent full image must not inherit the scissor rectangle.
                _,restored=r.draw('eye',cam,t=k*.71,presentation=1,read=True)
                assert np.array_equal(restored,colors),'scissor leaked into normal draw'
                x,y=points[-1];pair={}
                for label in (['before','after'] if k%2==0 else ['after','before']):
                    begin=time.perf_counter_ns()
                    metrics,_=r.draw('eye',cam,t=k*.71,debug=1,read=True,**({'pixel':(x,y)} if label=='after' else {}))
                    pair[label]=(time.perf_counter_ns()-begin)/1e6
                samples.append({'view':k,'mirror':mode,**pair})
        return {'driver':r.gl.info,'case':[width,height,count],'checked_pixels':checked,'reflected_pick_cases':reflected,
            'id_mismatches':0,'post_pick_image_mismatches':0,'readback_bytes':{'before':width*height*4,'after':4},'samples':samples,
            'median_ms':{str(m):{label:statistics.median(s[label] for s in samples if s['mirror']==m and s['view']>0) for label in ('before','after')} for m in (0,1,2)}}
    finally:r.close()
def main():
    p=argparse.ArgumentParser();p.add_argument('--case',nargs=3,type=int);p.add_argument('--output',type=Path,default=ROOT/'results/ether-system-graphics-2026-10-20.json');a=p.parse_args()
    if a.case:
        resource.setrlimit(resource.RLIMIT_AS,(2*1024**3,2*1024**3));resource.setrlimit(resource.RLIMIT_CPU,(55,56))
        os.sched_setaffinity(0,{min(os.sched_getaffinity(0))});r=check(*a.case);r['rss_kib']=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss;print(json.dumps(r));return
    result={'baseline':BASE,'ram_limit_bytes':2*1024**3,'wall_limit_seconds':60,'cases':[]}
    for case in [(320,180,1024),(960,540,1048576)]:
        out=subprocess.run([sys.executable,__file__,'--case',*map(str,case)],text=True,capture_output=True,timeout=60)
        if out.returncode:raise RuntimeError(str({'case':case,'returncode':out.returncode,'stdout':out.stdout,'stderr':out.stderr}))
        r=json.loads(out.stdout);result['cases'].append(r);print(json.dumps({'case':case,'median_ms':r['median_ms']}),flush=True)
        a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
    result['sha256']={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [ROOT/'graphics-desktop/renderer.py',ROOT/'graphics-desktop/egl_runtime.py',ROOT/'graphics-android/app/src/main/java/com/mundox/graphics/QuantumRenderer.java',Path(__file__)]}
    a.output.write_text(json.dumps(result,indent=2)+'\n')
if __name__=='__main__':main()
