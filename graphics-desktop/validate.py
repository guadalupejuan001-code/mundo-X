"""Independent geometric checks and core-driven engine behavior."""
from pathlib import Path
import json,math
import numpy as np
from engine import build_native,QuantumScene,QuantumGraphicsEngine
from renderer import Renderer,transforms,camera,street,overview

ROOT=Path(__file__).resolve().parent

def ids(image):
    a=image.astype(np.int64)
    return a[:,:,0]+(a[:,:,1]<<8)+(a[:,:,2]<<16)-1

def brute_reference(count,seed,edits,cam,width,height,time,positions,quantum=None,strength=0.):
    data=transforms(count,seed,edits).astype(np.float64)
    height_values=data[:,2]*(1+.08*np.sin(time*.8+data[:,0]*.07+data[:,1]*.06))
    if quantum is not None:height_values*=1+strength*quantum[np.arange(count)%len(quantum)]
    low=np.column_stack([data[:,0]-.38,np.zeros(count),data[:,1]-.38])
    high=np.column_stack([data[:,0]+.38,height_values,data[:,1]+.38])
    c=math.cos(time*.12);s=math.sin(time*.12)
    rot=np.array([[c,0,s],[0,1,0],[-s,0,c]])
    out=[]
    for y,x in positions:
        ndc=np.array([2*(x+.5)/width-1,1-2*(y+.5)/height])
        direction=cam['forward']+ndc[0]*cam['aspect']*cam['tan_half_fov']*cam['right']+ndc[1]*cam['tan_half_fov']*cam['up']
        direction/=np.linalg.norm(direction)
        cosine=direction@cam['forward']
        origin=cam['eye']+direction*(cam['near']/cosine)
        ro=rot.T@origin;rd=rot.T@direction
        enter=np.full(count,-np.inf);leave=np.full(count,np.inf)
        valid=height_values>0
        for axis in range(3):
            if abs(rd[axis])<1e-12:valid&=(ro[axis]>=low[:,axis])&(ro[axis]<=high[:,axis])
            else:
                a=(low[:,axis]-ro[axis])/rd[axis];b=(high[:,axis]-ro[axis])/rd[axis]
                enter=np.maximum(enter,np.minimum(a,b));leave=np.minimum(leave,np.maximum(a,b))
        valid &= (leave>=enter)&(enter>=0)&(enter<=(cam['far']-cam['near'])/cosine)
        distances=np.where(valid,enter,np.inf)
        nearest=int(np.argmin(distances));out.append(nearest if np.isfinite(distances[nearest]) else -1)
    return np.asarray(out)

def run():
    build_native();rng=np.random.default_rng(42)
    r=Renderer(192,108);checks=[];total=0
    for count,time_value,angle in [(1024,0.,0.),(1024,7.3,.6),(12345,2.4,-.35)]:
        edits={1:6,count//2:7,count-1:5}
        r.scene(count,2026,edits)
        cam=street(r.width,r.height,angle)
        q=np.array([1,0,-1,.5],dtype=np.float32);r.set_quantum(q,.25)
        _,observed=r.draw('eye',cam,time_value,debug=1,read=True)
        _,raster=r.draw('explicit',cam,time_value,debug=1,read=True)
        a=ids(observed);b=ids(raster)
        differences=np.argwhere(a!=b)
        random_points=np.column_stack([rng.integers(r.height,size=300),rng.integers(r.width,size=300)])
        positions=np.unique(np.concatenate([differences,random_points]),axis=0)
        expected=brute_reference(count,2026,edits,cam,r.width,r.height,time_value,positions,q,.25)
        actual=a[positions[:,0],positions[:,1]]
        wrong=np.flatnonzero(expected!=actual)
        # Report every discrepancy; strict brute-force checks remain separate
        # from known single-pixel raster edge rounding differences.
        checks.append({'objects':count,'time':time_value,'camera_angle':angle,
                       'raster_id_different_pixels':len(differences),'pixels':r.width*r.height,
                       'independent_brute_force_rays':len(positions),
                       'eye_vs_brute_mismatches':len(wrong),
                       'mismatches':[{'pixel':positions[i].tolist(),'eye':int(actual[i]),'brute':int(expected[i])} for i in wrong]})
        total+=len(positions)
    # The last object and partial final rows must remain addressable.
    edge_checks=[]
    for count in (1_000_000,1_000_003,1_048_576):
        r.scene(count,2026,prepare_explicit=False);r.set_quantum([0],0.)
        side=math.ceil(math.sqrt(count));center=(side-1)*.5
        for object_id in (0,count-1):
            x=(object_id%side-center)*2.2;z=(object_id//side-center)*2.2
            cam=camera([x,10,z],[x,0,z],r.width,r.height,near=.1,far=30,fov=20)
            _,frame=r.draw('eye',cam,debug=1,read=True)
            actual=int(ids(frame)[r.height//2,r.width//2])
            assert actual==object_id,(count,object_id,actual)
            edge_checks.append({'count':count,'queried_id':object_id,'observed_id':actual})
    r.close()
    scene=QuantumScene(1_000_000,20);engine=QuantumGraphicsEngine(scene,256,144)
    cam=street(256,144)
    before,first=engine.observe(cam)
    batches=scene.observable_batches
    for _ in range(20):
        reused,metrics=engine.observe(cam)
        assert metrics['reused'] and reused is before
    assert scene.observable_batches==batches
    engine.observe(street(256,144,.2),read=False)
    assert scene.observable_batches==batches
    scene.apply([('Z',0)]);after,gate=engine.observe(cam)
    changed=int(np.sum(np.any(before!=after,axis=2)))
    assert changed>0 and scene.observable_batches==batches+1
    chosen=engine.pick(cam,128,90)
    assert 0<=chosen<scene.count
    quantum_revision=scene._core.revision
    scene.edit(chosen,7)
    _,edit=engine.observe(cam)
    assert scene._core.revision==quantum_revision
    assert engine.pick(cam,128,90)!=chosen
    # Restore the source: explicit edit semantics, no hidden population pruning.
    scene.edit(chosen,0)
    assert engine.pick(cam,128,90)==chosen
    scene.edit_many({i:1 for i in range(5000)})
    engine.observe(cam,read=False)
    assert scene.describe()['independent_edits']==5000
    engine.close();scene.close()
    report={'driver':'GLES 3 on llvmpipe; software','geometric_cases':checks,
            'brute_force_rays':total,'first_and_last_object_checks':edge_checks,
            'engine':{'cached_requests':20,'camera_does_not_requery_core':True,
                      'gate_changes_pixels':changed,'pick_hide_restore':True,
                      'edit_preserves_core_revision':True,'more_than_4096_edits':5000}}
    (ROOT/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2),flush=True)
    if sum(c['eye_vs_brute_mismatches'] for c in checks):
        raise AssertionError('Inspect independent geometric mismatches before release')

if __name__=='__main__':run()
