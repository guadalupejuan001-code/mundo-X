#!/usr/bin/env python3
"""Paired production Solar GLSL, original arithmetic, fresh catalogue views.
Run after generating SolarRenderFixtures. Software GLES is reported as such.
"""
import argparse,ctypes as C,hashlib,json,os,resource,statistics,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE='af0b1797573b55a739dcaef7b784eb98825ff07c'
os.environ.setdefault('LP_NUM_THREADS','1');os.environ.setdefault('OPENBLAS_NUM_THREADS','1')
sys.path.insert(0,str(ROOT/'graphics-desktop'))

def run(fixtures,repeats,select):
    import numpy as np
    from PIL import Image
    from egl_runtime import GL,I,U,F,P
    g=GL()
    for name,ret,args in [('glUniform2i',None,[I,I,I]),('glUniform1iv',None,[I,I,P]),
            ('glUniform1uiv',None,[I,I,P]),('glUniform3fv',None,[I,I,P]),('glUniform4fv',None,[I,I,P]),
            ('glTexStorage3D',None,[U,I,U,I,I,I]),('glTexSubImage3D',None,[U,I,I,I,I,I,I,I,U,U,P]),
            ('glGenerateMipmap',None,[U])]:
        setattr(g,name,C.CFUNCTYPE(ret,*args)(g.egl.eglGetProcAddress(name.encode())))
    assets=ROOT/'solar-android/app/src/main/assets'
    old=subprocess.check_output(['git','show',BASE+':solar-android/app/src/main/assets/shaders/solar.frag'],cwd=ROOT,text=True)
    new=(assets/'shaders/solar.frag').read_text();vertex=(assets/'shaders/solar.vert').read_text()
    programs={k:g.program(vertex,v) for k,v in [('before',old),('after',new)]}
    # Separate untimed depth/identity checks. No production debug branch.
    for kind in ['id','depth']:
        for k,v in [('before',old),('after',new)]:
            expr='float value=float(id+1);' if kind=='id' else 'float value=floor(gl_FragDepth*16777215.0);'
            v=v.replace('    outColor=vec4(color*vignette,1.0);',expr+'outColor=vec4(mod(value,256.0),mod(floor(value/256.0),256.0),floor(value/65536.0),255.0)/255.0;')
            programs[k,kind]=g.program(vertex,v)
    vao=g.object('glGenVertexArrays');g.glBindVertexArray(vao)
    maps=g.object('glGenTextures');g.glActiveTexture(0x84C0);g.glBindTexture(0x8C1A,maps)
    g.glTexStorage3D(0x8C1A,12,0x8058,2048,1024,3)
    for layer,name in enumerate(['earth.png','moon.jpg','jupiter.jpg']):
        a=np.asarray(Image.open(assets/'textures'/name).convert('RGBA').resize((2048,1024),Image.Resampling.BILINEAR)).copy()
        g.glTexSubImage3D(0x8C1A,0,0,0,layer,2048,1024,1,0x1908,0x1401,a.ctypes.data)
    for key,value in [(0x2801,0x2703),(0x2800,0x2601),(0x2802,0x2901),(0x2803,0x812F)]:g.glTexParameteri(0x8C1A,key,value)
    g.glGenerateMipmap(0x8C1A)
    tiles=g.object('glGenTextures');g.glActiveTexture(0x84C1);g.glBindTexture(0x0DE1,tiles)
    for key,value in [(0x2801,0x2600),(0x2800,0x2600),(0x2802,0x812F),(0x2803,0x812F)]:g.glTexParameteri(0x0DE1,key,value)
    w,h=fixtures[0]['width'],fixtures[0]['height']
    target=g.object('glGenFramebuffers');g.glBindFramebuffer(0x8D40,target)
    color=g.object('glGenTextures');g.glActiveTexture(0x84C2);g.glBindTexture(0x0DE1,color)
    g.glTexImage2D(0x0DE1,0,0x8058,w,h,0,0x1908,0x1401,None);g.glFramebufferTexture2D(0x8D40,0x8CE0,0x0DE1,color,0)
    depth=g.object('glGenRenderbuffers');g.glBindRenderbuffer(0x8D41,depth);g.glRenderbufferStorage(0x8D41,0x81A6,w,h)
    g.glFramebufferRenderbuffer(0x8D40,0x8D00,0x8D41,depth)
    assert g.glCheckFramebufferStatus(0x8D40)==0x8CD5
    g.glViewport(0,0,w,h);g.glDisable(0x0BD0);g.glEnable(0x0B71);g.glDepthFunc(0x0203);g.glClearColor(0,0,0,1)
    def draw(p,f):
        g.glUseProgram(p)
        loc=lambda s:g.glGetUniformLocation(p,s.encode())
        for name,value in [('uResolution',[w,h])]:g.glUniform2f(loc(name),*value)
        for name,key in [('uForward','forward'),('uRight','right'),('uUp','up')]:g.glUniform3f(loc(name),*f[key])
        for name,value in [('uAspect',f['aspect']),('uTanHalfFov',f['tan']),('uNear',f['near']),('uFar',f['far']),('uTime',f['scene']*.7),('uCoupling',1)]:g.glUniform1f(loc(name),value)
        for name,value in [('uCount',23),('uMaps',0),('uBodyTiles',1),('uBodyTileSize',128),('uAtmosphereEnabled',int(f['scene']%4!=2))]:g.glUniform1i(loc(name),value)
        g.glUniform2i(loc('uBodyTileGrid'),f['tilesX'],f['tilesY'])
        for name,key,components in [('uBodies[0]','bodies',4),('uAppearance[0]','appearance',4),('uColors[0]','colors',4),('uAxes[0]','axes',3),('uAtmosphere[0]','atmosphere',4),('uAtmosphereColors[0]','atmoColors',3)]:
            a=np.array(f[key],dtype=np.float32);getattr(g,'glUniform%dfv'%components)(loc(name),23,a.ctypes.data)
        for name,value in [('uEntry',[f['selected'] if f['entry'] else -1,f['altitude'],f['radiusKm'],int(f['entry'])]),('uQ0',[1,0,-1,1]),('uQ1',[0,1,0,1]),('uEther',[0,0,0,1])]:
            a=np.array(value,dtype=np.float32);g.glUniform4fv(loc(name),1,a.ctypes.data)
        a=np.array(f['layers'],dtype=np.int32);g.glUniform1iv(loc('uLayers[0]'),23,a.ctypes.data)
        a=np.array(f['eclipses'],dtype=np.uint32);g.glUniform1uiv(loc('uEclipseMasks[0]'),23,a.ctypes.data)
        g.glClear(0x4100);start=time.perf_counter_ns();g.glDrawArrays(4,0,3);g.glFinish();elapsed=(time.perf_counter_ns()-start)/1e6
        image=np.empty((h,w,4),np.uint8);g.glReadPixels(0,0,w,h,0x1908,0x1401,image.ctypes.data);g.check('solar')
        return elapsed,image
    results=[];pixels=0;diffs={k:0 for k in ['color','id','depth']};maxdelta=0
    try:
        for f in fixtures:
            if select and f['scene'] not in select:continue
            a=np.array(f['tiles'],dtype=np.uint32);g.glActiveTexture(0x84C1);g.glBindTexture(0x0DE1,tiles)
            g.glTexImage2D(0x0DE1,0,0x8236,f['tilesX'],f['tilesY'],0,0x8D94,0x1405,a.ctypes.data)
            outputs={};samples={};first={}
            for key in (['before','after'] if f['scene']%2 else ['after','before']):
                first[key],_=draw(programs[key],f);samples[key]=[]
                for _ in range(repeats):
                    t,img=draw(programs[key],f);samples[key].append(t)
                outputs[key,'color']=img
            for kind in diffs:
                if kind!='color':
                    for key in ['before','after']:_,outputs[key,kind]=draw(programs[key,kind],f)
                x,y=outputs['before',kind],outputs['after',kind]
                different=0
                for row in range(0,h,128):
                    bx,by=x[row:row+128],y[row:row+128]
                    different+=int(np.any(bx!=by,axis=2).sum())
                    if kind=='color':maxdelta=max(maxdelta,int(np.abs(bx.astype(np.int16)-by).max()))
                diffs[kind]+=different;pixels+=w*h
                if different:print(json.dumps({'mismatch_scene':f['scene'],'kind':kind,'pixels':different}),file=sys.stderr,flush=True)
                del outputs['before',kind],outputs['after',kind],x,y
            results.append({'scene':f['scene'],'selected':f['selected'],'entry':f['entry'],'samples_ms':samples,'fresh_ms':first,'median_ms':{k:statistics.median(v) for k,v in samples.items()},'eclipse_candidates':sum(int(v).bit_count() for v in f['eclipses'])})
        return {'driver':g.info,'resolution':[w,h],'results':results,'compared_pixels':pixels,'different_pixels':diffs,'max_color_channel_delta':maxdelta,
            'median_ms':{k:statistics.median(v['median_ms'][k] for v in results) for k in ['before','after']},'rss_peak_kib':resource.getrusage(resource.RUSAGE_SELF).ru_maxrss}
    finally:g.close()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--fixtures',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--repeats',type=int,default=3);p.add_argument('--scenes',type=int,nargs='*');a=p.parse_args()
    resource.setrlimit(resource.RLIMIT_AS,(2*1024**3,2*1024**3));resource.setrlimit(resource.RLIMIT_CPU,(180,181));os.sched_setaffinity(0,{min(os.sched_getaffinity(0))})
    result=run(json.loads(a.fixtures.read_text()),a.repeats,a.scenes)
    result.update(baseline=BASE,source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),ram_budget_bytes=2*1024**3,cpu_budget_seconds=180,scope='Host software GLES; production shader, catalogue fixtures; not Android FPS',shader_sha256=hashlib.sha256((ROOT/'solar-android/app/src/main/assets/shaders/solar.frag').read_bytes()).hexdigest(),fixtures_sha256=hashlib.sha256(a.fixtures.read_bytes()).hexdigest())
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:result[k] for k in ['median_ms','different_pixels','max_color_channel_delta','rss_peak_kib']}))
    if any(result['different_pixels'].values()):sys.exit(1)
