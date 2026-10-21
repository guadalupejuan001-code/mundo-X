#!/usr/bin/env python3
"""Paired production Solar GLSL against a frozen defined-filter reference.
Run after generating SolarRenderFixtures. Software GLES is reported as such.
"""
import argparse,ctypes as C,ctypes.util,hashlib,json,os,resource,statistics,subprocess,sys,tempfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE='af0b1797573b55a739dcaef7b784eb98825ff07c'
DEFINED_REFERENCE=ROOT/'solar-android/tests/fixtures/solar-defined-reference.frag'
os.environ.setdefault('LP_NUM_THREADS','1');os.environ.setdefault('OPENBLAS_NUM_THREADS','1')
sys.path.insert(0,str(ROOT/'graphics-desktop'))

def run(fixtures,repeats,select,candidate_shader=None,baseline_shader=None,color_only=False,capture_dir=None,warmup_frames=1,single_program=False):
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
    old=(baseline_shader or DEFINED_REFERENCE).read_text()
    new=(candidate_shader or assets/'shaders/solar.frag').read_text();vertex=(assets/'shaders/solar.vert').read_text()
    if single_program and old!=new:raise ValueError('--single-program requires identical baseline/candidate source; compare independent replay artifacts for A/B')
    if single_program:
        program=g.program(vertex,new);programs={k:program for k in ['before','after']}
    else:programs={k:g.program(vertex,v) for k,v in [('before',old),('after',new)]}
    # Separate untimed depth/identity checks. No production debug branch.
    for kind in ([] if color_only else ['id','depth']):
        for k,v in [('before',old),('after',new)]:
            if single_program and k=='after':programs[k,kind]=programs['before',kind];continue
            expr='float value=float(id+1);' if kind=='id' else 'float value=floor(gl_FragDepth*16777215.0);'
            v=v.replace('    outColor=vec4(color*vignette,1.0);',expr+'outColor=vec4(mod(value,256.0),mod(floor(value/256.0),256.0),floor(value/65536.0),255.0)/255.0;')
            programs[k,kind]=g.program(vertex,v)
    vao=g.object('glGenVertexArrays');g.glBindVertexArray(vao)
    maps=g.object('glGenTextures');g.glActiveTexture(0x84C0);g.glBindTexture(0x8C1A,maps)
    g.glTexStorage3D(0x8C1A,12,0x8058,2048,1024,3)
    for layer,name in enumerate(fixtures[0].get('mapOrder',['earth.png','moon.jpg','jupiter.jpg'])):
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
        for name,value in [('uAspect',f['aspect']),('uTanHalfFov',f['tan']),('uNear',f['near']),('uFar',f['far']),('uTime',f.get('time',f['scene']*.7)),('uCoupling',f.get('coupling',1))]:g.glUniform1f(loc(name),value)
        for name,value in [('uCount',23),('uMaps',0),('uBodyTiles',1),('uBodyTileSize',f.get('tileSize',128)),('uAtmosphereEnabled',int(f.get('atmosphereEnabled',f['scene']%4!=2)))]:g.glUniform1i(loc(name),value)
        g.glUniform2i(loc('uBodyTileGrid'),f['tilesX'],f['tilesY'])
        for name,key,components in [('uBodies[0]','bodies',4),('uAppearance[0]','appearance',4),('uColors[0]','colors',4),('uAxes[0]','axes',3),('uAtmosphere[0]','atmosphere',4),('uAtmosphereColors[0]','atmoColors',3)]:
            a=np.array(f[key],dtype=np.float32);getattr(g,'glUniform%dfv'%components)(loc(name),23,a.ctypes.data)
        for name,value in [('uEntry',[f['selected'] if f['entry'] else -1,f['altitude'],f['radiusKm'],int(f['entry'])]),('uQ0',f['quantum'][:4]),('uQ1',f['quantum'][4:]),('uEther',f['ether'][:4])]:
            a=np.array(value,dtype=np.float32);g.glUniform4fv(loc(name),1,a.ctypes.data)
        a=np.array(f['layers'],dtype=np.int32);g.glUniform1iv(loc('uLayers[0]'),23,a.ctypes.data)
        a=np.array(f['eclipses'],dtype=np.uint32);g.glUniform1uiv(loc('uEclipseMasks[0]'),23,a.ctypes.data)
        g.glClear(0x4100);start=time.perf_counter_ns();g.glDrawArrays(4,0,3);g.glFinish();elapsed=(time.perf_counter_ns()-start)/1e6
        image=np.empty((h,w,4),np.uint8);g.glReadPixels(0,0,w,h,0x1908,0x1401,image.ctypes.data);g.check('solar')
        return elapsed,image
    results=[];pixels=0;diffs={k:0 for k in (['color'] if color_only else ['color','id','depth'])};maxdelta=0
    repeat_diffs={k:0 for k in ['before','after']}
    warmup_diffs={k:0 for k in ['before','after']}
    if capture_dir:capture_dir.mkdir(parents=True,exist_ok=True)
    def capture(f,key,index,img):
        if capture_dir:
            Image.fromarray(img).save(capture_dir/f"scene{f['scene']:02d}-{key}-{index}.png")
    def memory():
        status=Path('/proc/self/status')
        return {line.split(':')[0]:line.split(':')[1].strip() for line in status.read_text().splitlines() if line.startswith(('VmSize:','VmRSS:','VmPeak:','VmHWM:'))} if status.exists() else {}
    try:
        for f in fixtures:
            if select and f['scene'] not in select:continue
            a=np.array(f['tiles'],dtype=np.uint32);g.glActiveTexture(0x84C1);g.glBindTexture(0x0DE1,tiles)
            g.glTexImage2D(0x0DE1,0,0x8236,f['tilesX'],f['tilesY'],0,0x8D94,0x1405,a.ctypes.data)
            outputs={};samples={};first={};scene_repeat_diffs={};scene_warmup_diffs={}
            for key in (['before','after'] if f['scene']%2 else ['after','before']):
                print(json.dumps({'stage':'draw','scene':f['scene'],'program':key,'memory':memory()}),file=sys.stderr,flush=True)
                first[key],previous=draw(programs[key],f);samples[key]=[];scene_repeat_diffs[key]=[]
                capture(f,key,0,previous)
                scene_warmup_diffs[key]=[]
                for warmup in range(1,warmup_frames):
                    _,img=draw(programs[key],f)
                    changed=int(np.any(previous!=img,axis=2).sum())
                    warmup_diffs[key]+=changed;scene_warmup_diffs[key].append(changed)
                    capture(f,key,'warmup'+str(warmup),img);previous=img
                for repeat in range(repeats):
                    t,img=draw(programs[key],f);samples[key].append(t)
                    changed=int(np.any(previous!=img,axis=2).sum())
                    repeat_diffs[key]+=changed;scene_repeat_diffs[key].append(changed)
                    capture(f,key,repeat+1,img)
                    previous=img
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
            results.append({'scene':f['scene'],'selected':f['selected'],'entry':f['entry'],'samples_ms':samples,'fresh_ms':first,'warmup_different_pixels':scene_warmup_diffs,'repeat_different_pixels':scene_repeat_diffs,'median_ms':{k:statistics.median(v) for k,v in samples.items()},'eclipse_candidates':sum(int(v).bit_count() for v in f['eclipses'])})
        return {'driver':g.info,'resolution':[w,h],'results':results,'compared_pixels':pixels,'different_pixels':diffs,'max_color_channel_delta':maxdelta,
            'repeat_different_pixels':repeat_diffs,
            'warmup_frames':warmup_frames,'warmup_different_pixels':warmup_diffs,
            'independent_programs':not single_program,'process_memory':memory(),
            'median_ms':{k:statistics.median(v['median_ms'][k] for v in results) for k in ['before','after']},'rss_peak_kib':resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
            'process_threads':len(list(Path('/proc/self/task').iterdir())) if Path('/proc/self/task').exists() else None}
    finally:g.close()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--fixtures',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--repeats',type=int,default=3);p.add_argument('--scenes',type=int,nargs='*');p.add_argument('--candidate-shader',type=Path)
    p.add_argument('--baseline-shader',type=Path,default=DEFINED_REFERENCE,help='Reference shader (default: frozen reference with defined filtering and full original material evaluation)')
    p.add_argument('--egl-library',type=Path,help='Load an explicit EGL implementation, such as the Android SDK SwiftShader library')
    p.add_argument('--color-only',action='store_true',help='Replay the unmodified color shader without compiling auxiliary ID/depth programs')
    p.add_argument('--capture-dir',type=Path,help='Preserve every unmodified RGBA capture as a lossless PNG (rows in OpenGL readback order)')
    p.add_argument('--warmup-frames',type=int,default=1,help='Symmetric untimed full draws per program/scene; retain their pixel differences separately before strict repeated captures')
    p.add_argument('--swiftshader-threads',type=int,help='Explicit legacy SwiftShader worker count via an isolated SwiftShader.ini; requires --egl-library')
    p.add_argument('--single-program',action='store_true',help='Replay one shader in this process to bound JIT memory; A/B requires independently captured artifacts')
    a=p.parse_args()
    if a.repeats<1:p.error('--repeats must be positive')
    if a.warmup_frames<1:p.error('--warmup-frames must be positive')
    if a.swiftshader_threads is not None and (not a.egl_library or a.swiftshader_threads<0):p.error('--swiftshader-threads requires --egl-library and a nonnegative worker count')
    if a.egl_library:
        a.egl_library=a.egl_library.resolve()
        find_library=ctypes.util.find_library
        ctypes.util.find_library=lambda name:str(a.egl_library.resolve()) if name=='EGL' else find_library(name)
    resource.setrlimit(resource.RLIMIT_AS,(2*1024**3,2*1024**3));resource.setrlimit(resource.RLIMIT_CPU,(180,181));os.sched_setaffinity(0,{min(os.sched_getaffinity(0))})
    for key in ['fixtures','output','candidate_shader','baseline_shader','capture_dir']:
        if getattr(a,key) is not None:setattr(a,key,getattr(a,key).resolve())
    with tempfile.TemporaryDirectory(prefix='solar-driver-') as driver_dir:
        previous_cwd=Path.cwd()
        if a.swiftshader_threads is not None:
            driver_config=f'[Processor]\nThreadCount={a.swiftshader_threads}\n[Testing]\nDisableServer=1\n'
            (Path(driver_dir)/'SwiftShader.ini').write_text(driver_config);os.chdir(driver_dir)
        try:result=run(json.loads(a.fixtures.read_text()),a.repeats,a.scenes,a.candidate_shader,a.baseline_shader,a.color_only,a.capture_dir,a.warmup_frames,a.single_program)
        finally:os.chdir(previous_cwd)
    if a.swiftshader_threads is not None:
        if 'SwiftShader' not in result['driver']['renderer']:raise RuntimeError('Requested SwiftShader worker configuration with another renderer')
        result.update(swiftshader_threads=a.swiftshader_threads,driver_configuration=driver_config)
    candidate=a.candidate_shader or ROOT/'solar-android/app/src/main/assets/shaders/solar.frag'
    result.update(baseline=BASE,source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),ram_budget_bytes=2*1024**3,cpu_budget_seconds=180,scope='Host software GLES; catalogue fixtures; not Android FPS',experimental_candidate=a.candidate_shader is not None,shader_sha256=hashlib.sha256(candidate.read_bytes()).hexdigest(),fixtures_sha256=hashlib.sha256(a.fixtures.read_bytes()).hexdigest())
    if a.baseline_shader:result.update(baseline_shader_sha256=hashlib.sha256(a.baseline_shader.read_bytes()).hexdigest(),baseline=str(a.baseline_shader))
    if a.egl_library:result['egl_library']=str(a.egl_library.resolve())
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:result[k] for k in ['median_ms','different_pixels','max_color_channel_delta','rss_peak_kib']}))
    if any(result['different_pixels'].values()) or any(result['repeat_different_pixels'].values()):sys.exit(1)
