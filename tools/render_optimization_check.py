#!/usr/bin/env python3
"""Exact old/new render comparison and instrumented work counts, not cached frames.

The reference uses the original shader AND its signed RG32I edit texture.
Each child has a 2 GiB address-space and 55 CPU-second budget. Times include
completed draws, excluding compilation; fresh camera/time/seed/edits are used.
"""
import argparse,hashlib,json,os,resource,statistics,subprocess,sys,tempfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE='d0c148dae0ef09159ad0b1e18f6cafaf6285a7aa'
PATHS=['graphics-desktop/edit_pages.py','graphics-desktop/renderer.py',
       'graphics-desktop/shaders/common.glsl','graphics-desktop/shaders/eye.frag',
       'graphics-android/app/src/main/java/com/mundox/graphics/EditPages.java',
       'graphics-android/app/src/main/java/com/mundox/graphics/QuantumRenderer.java',
       'graphics-android/app/src/main/assets/shaders/common.glsl',
       'tools/render_optimization_check.py','tools/render_test_support.py']

def hashes():return {name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in PATHS}

def instruments(text):
    text=text.replace('void objectData(', 'int mxCells=0,mxObjects=0,mxFetches=0;\nvoid objectData(')
    text=text.replace('  uvec4 h=mxHashBytes', '  mxObjects++;\n  uvec4 h=mxHashBytes')
    # Declare counters before editState as well.
    text=text.replace('int mxCells=0,mxObjects=0,mxFetches=0;\n','')
    text=text.replace('int editState(', 'int mxCells=0,mxObjects=0,mxFetches=0;\nint editState(')
    text=text.replace('ivec2 item=texelFetch', 'mxFetches++;ivec2 item=texelFetch')
    text=text.replace('uint offset=texelFetch', 'mxFetches++;uint offset=texelFetch')
    text=text.replace('uint states=texelFetch', 'mxFetches++;uint states=texelFetch')
    text=text.replace('    int id=cell.y*uSide+cell.x;', '    mxCells++;int id=cell.y*uSide+cell.x;')
    return text.replace('void main(){','void mxMain(){')+'''
void main(){mxMain();outColor=encodeId(uDebug==7?mxCells:uDebug==8?mxObjects:mxFetches);gl_FragDepth=0.0;}
'''

def render_check(baseline, width, height, count, profile=False):
    import numpy as np
    sys.path.insert(0,str(ROOT/'graphics-desktop'))
    from renderer import Renderer,source,street,overview,camera
    from render_test_support import legacy_edit_texture
    r=Renderer(width,height);g=r.gl;new_texture=r.edits_texture;old_texture=g.object('glGenTextures')
    old=baseline/'graphics-desktop/shaders'
    before=(old/'eye.frag').read_text().replace('// @COMMON@',(old/'common.glsl').read_text())
    texts={'before':before,'after':source('eye.frag')};programs={}
    for label,text in texts.items():
        if profile:text=instruments(text)
        for mode in (0,1):
            fragment=text if mode==0 else text.replace('#version 300 es','#version 300 es\n#define MX_MIRRORS 1',1)
            p=g.program(source('eye.vert'),fragment);programs[label,mode]=p;r.locations[p]={}
    samples=[];compared=0;counts=[]
    try:
        for k in range(12 if profile or width<1920 else 6):
            mode=k%3;group=(k//3)%4
            if group==0:edits={}
            elif group==1:edits={i:i%8 for i in range(min(count,64))}
            elif group==2:edits={int(i):int((i+k)%8) for i in np.linspace(0,count-1,min(count,4096))}
            else:edits={int(i):int((i+k)%8) for i in np.linspace(0,count-1,min(count,32768))}
            r.edits_texture=new_texture
            upload=r.scene(count,2026+k*1234567,edits,prepare_explicit=False)
            old_bytes=legacy_edit_texture(r,edits,old_texture)
            r.set_quantum([1,0,-1,.5],.25 if k%2 else 0.)
            r.set_reflections(mode,budget=64)
            cam=street(width,height,k*.13)
            if k in (0,8):cam=overview(r.side,width,height,k*.17)
            if k==10:cam=camera([0,2,0],[1,2,1],width,height,near=.01,far=5000,fov=70)
            variants={};stats={}
            for label in (['before','after'] if k%2 else ['after','before']):
                r.edits_texture=old_texture if label=='before' else new_texture
                r.programs['mirrors' if mode else 'eye']=programs[label,int(mode!=0)]
                if profile:
                    totals={}
                    for name,debug in [('cells',7),('objects',8),('edit_fetches',9)]:
                        _,image=r.draw('eye',cam,t=k*.21,debug=debug,read=True,presentation=1)
                        a=image.astype(np.int64);n=a[:,:,0]+(a[:,:,1]<<8)+(a[:,:,2]<<16)-1
                        totals[name]={'total':int(n.sum()),'max':int(n.max()),'mean':float(n.mean())}
                    stats[label]=totals
                else:
                    # Warm shader/JIT at the same workload before timing. Each
                    # measured frame still executes all rays; no image reuse.
                    fresh,_=r.draw('eye',cam,t=k*.21,presentation=k%2)
                    timings=[]
                    for rep in range(3):
                        timing,image=r.draw('eye',cam,t=k*.21,read=True,presentation=k%2)
                        timings.append(timing['wall_draw_finish_ms'])
                    debug_images={0:image}
                    for debug in ([1,6] if mode else [1]):
                        _,debug_images[debug]=r.draw('eye',cam,t=k*.21,debug=debug,read=True,presentation=k%2)
                    variants[label]=debug_images
                    samples.append({'view':k,'mode':mode,'edits':len(edits),'version':label,'samples_ms':timings,
                                    'fresh_view_ms':fresh['wall_draw_finish_ms'],
                                    'first_specialization':k<2,'median_ms':statistics.median(timings)})
            if profile:
                for name in ['cells','objects']:assert stats['before'][name]==stats['after'][name],(k,name)
                counts.append({'view':k,'mode':mode,'edits':len(edits),**stats})
            else:
                for debug in variants['before']:
                    x,y=variants['before'][debug],variants['after'][debug]
                    assert np.array_equal(x,y),(k,debug,int(np.any(x!=y,axis=2).sum()))
                    compared+=width*height
                counts.append({'view':k,'old_edit_texture_bytes':old_bytes,'new_edit_texture_bytes':upload['edits_texture_bytes']})
        return {'status':'passed','driver':g.info,'case':[width,height,count],'samples':samples,'counts':counts,
                'compared_pixels':compared,'different_pixels':0,
                'median_ms':{label:statistics.median(s['median_ms'] for s in samples if s['version']==label) for label in texts} if samples else {},
                'rss_peak_kib':resource.getrusage(resource.RUSAGE_SELF).ru_maxrss}
    finally:r.close()

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--baseline',type=Path);p.add_argument('--child',choices=['render','profile']);p.add_argument('--case',type=int,nargs=3)
    a=p.parse_args()
    if a.child:
        resource.setrlimit(resource.RLIMIT_AS,(2*1024**3,2*1024**3));resource.setrlimit(resource.RLIMIT_CPU,(55,56))
        os.sched_setaffinity(0,{min(os.sched_getaffinity(0))})
        print(json.dumps(render_check(a.baseline,*a.case,profile=a.child=='profile')));return
    for name in ['common.glsl','eye.frag']:
        assert (ROOT/'graphics-desktop/shaders'/name).read_bytes()==(ROOT/'graphics-android/app/src/main/assets/shaders'/name).read_bytes()
    original_hashes=hashes();result={'baseline':BASE,'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        'ram_limit_bytes':2*1024**3,'cpu_seconds_per_child':55,'wall_seconds_per_child':60,
        'cpu_affinity':[min(os.sched_getaffinity(0))],'scope':'Host software GLES; no physical Android performance','cases':[]}
    a.output.parent.mkdir(parents=True,exist_ok=True)
    def save():a.output.write_text(json.dumps(result,indent=2)+'\n')
    env=dict(os.environ,OPENBLAS_NUM_THREADS='1',LP_NUM_THREADS='1',OMP_NUM_THREADS='1',LIBGL_ALWAYS_SOFTWARE='1')
    with tempfile.TemporaryDirectory(prefix='mx-render-reference-') as tmp:
        baseline=Path(tmp);shaders=baseline/'graphics-desktop/shaders';shaders.mkdir(parents=True)
        for name in ['common.glsl','eye.frag']:
            (shaders/name).write_bytes(subprocess.check_output(['git','show',BASE+':graphics-desktop/shaders/'+name],cwd=ROOT))
        for mode,case in [('profile',(160,90,1048576)),('render',(320,180,1024)),('render',(320,180,1048576)),
                          ('render',(960,540,1048576)),('render',(1920,1080,1048576))]:
            try:
                child=subprocess.run([sys.executable,__file__,'--child',mode,'--baseline',str(baseline),'--output',str(a.output),'--case',*map(str,case)],env=env,text=True,capture_output=True,timeout=60)
                if child.returncode:raise RuntimeError(child.stdout+child.stderr)
                row=json.loads(child.stdout)
            except subprocess.TimeoutExpired:row={'status':'wall_budget_reached','case':case}
            result['cases'].append({'kind':mode,**row});save()
            print(json.dumps({'kind':mode,'case':case,'status':row['status'],'median_ms':row.get('median_ms')}),flush=True)
    result['source_sha256']=hashes();assert result['source_sha256']==original_hashes,'Sources changed during campaign'
    save()
    if any(c['status']!='passed' for c in result['cases']):raise SystemExit('A declared case reached its budget')
if __name__=='__main__':main()
