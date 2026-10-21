#!/usr/bin/env python3
"""Protect procedural/explicit raster consumers of the shared edit decoder."""
import argparse,hashlib,json,os,resource,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE='d0c148dae0ef09159ad0b1e18f6cafaf6285a7aa'

def check():
    import numpy as np
    sys.path.insert(0,str(ROOT/'graphics-desktop'))
    from renderer import Renderer,source,street,overview
    from render_test_support import legacy_edit_texture
    r=Renderer(192,108);g=r.gl;old_texture=g.object('glGenTextures');new_texture=r.edits_texture
    def old(name):return subprocess.check_output(['git','show',BASE+':graphics-desktop/shaders/'+name],cwd=ROOT,text=True)
    common=old('common.glsl')
    p=g.program(old('raster.vert').replace('// @COMMON@',common),old('raster.frag').replace('// @COMMON@',common))
    g.check('reference raster');r.locations[p]={};current=r.programs['raster'];records=[]
    try:
        for k,count in enumerate([1,257,1024,4096,4096]):
            edits={i:(i+k)%8 for i in range(count)} if k<4 else {}
            r.edits_texture=new_texture;r.scene(count,2026+k*191,edits);legacy_edit_texture(r,edits,old_texture)
            r.set_quantum([1,-1,.25,0],.25);cam=overview(r.side,192,108,k*.2) if k%2 else street(192,108,k*.2)
            for route in ['procedural','explicit']:
                for debug in [0,1]:
                    images={}
                    for label,program,texture in [('before',p,old_texture),('after',current,new_texture)]:
                        r.programs['raster']=program;r.edits_texture=texture
                        _,images[label]=r.draw(route,cam,t=k*.7,debug=debug,read=True,presentation=k%2)
                    assert np.array_equal(images['before'],images['after']),(k,route,debug)
                    records.append({'case':k,'objects':count,'route':route,'debug':debug,
                        'image_sha256':hashlib.sha256(images['after'].tobytes()).hexdigest()})
        return {'status':'passed','baseline':BASE,'driver':g.info,'records':records,
                'compared_pixels':len(records)*192*108,'different_pixels':0,
                'rss_peak_kib':resource.getrusage(resource.RUSAGE_SELF).ru_maxrss}
    finally:r.close()
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    resource.setrlimit(resource.RLIMIT_AS,(2*1024**3,2*1024**3));resource.setrlimit(resource.RLIMIT_CPU,(55,56))
    os.sched_setaffinity(0,{min(os.sched_getaffinity(0))});r=check();a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(r,indent=2)+'\n');print('raster routes passed:',r['compared_pixels'],'pixels')
