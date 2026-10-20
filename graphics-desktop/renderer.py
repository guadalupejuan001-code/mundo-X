from pathlib import Path
import ctypes as C
import math,time
import numpy as np
from egl_runtime import GL,U,I,F,P

ROOT=Path(__file__).resolve().parent

def source(name):
    return (ROOT/'shaders'/name).read_text().replace('// @COMMON@',(ROOT/'shaders/common.glsl').read_text())

def normalize(v):
    return v/np.linalg.norm(v)

def camera(eye,target,width,height,near=.1,far=10000.,fov=48.):
    eye=np.asarray(eye,dtype=np.float64);target=np.asarray(target,dtype=np.float64)
    if not np.isfinite(eye).all() or not np.isfinite(target).all() or np.linalg.norm(target-eye)==0:
        raise ValueError('Camera needs finite distinct eye and target')
    if not 0<near<far or not 0<fov<180 or width<1 or height<1:raise ValueError('Invalid camera projection')
    forward=normalize(target-eye)
    reference_up=[0,0,1] if abs(forward[1])>.999 else [0,1,0]
    right=normalize(np.cross(forward,reference_up));up=np.cross(right,forward)
    view=np.eye(4);view[:3,:3]=np.array([right,up,-forward]);view[:3,3]=-view[:3,:3]@eye
    factor=1/math.tan(math.radians(fov)/2)
    projection=np.zeros((4,4));projection[0,0]=factor/(width/height);projection[1,1]=factor
    projection[2,2]=(far+near)/(near-far);projection[2,3]=2*far*near/(near-far);projection[3,2]=-1
    vp=projection@view
    return {'eye':eye,'vp':vp,'inverse':np.linalg.inv(vp),'near':near,'far':far,'fov':fov,
            'target':target,'forward':forward,'right':right,'up':up,'aspect':width/height,'tan_half_fov':1/factor}

def mesh():
    faces=[
      [-.5,-.5,.5,.5,-.5,.5,.5,.5,.5,-.5,.5,.5,0,0,1],
      [.5,-.5,-.5,-.5,-.5,-.5,-.5,.5,-.5,.5,.5,-.5,0,0,-1],
      [.5,-.5,.5,.5,-.5,-.5,.5,.5,-.5,.5,.5,.5,1,0,0],
      [-.5,-.5,-.5,-.5,-.5,.5,-.5,.5,.5,-.5,.5,-.5,-1,0,0],
      [-.5,.5,.5,.5,.5,.5,.5,.5,-.5,-.5,.5,-.5,0,1,0],
      [-.5,-.5,-.5,.5,-.5,-.5,.5,-.5,.5,-.5,-.5,.5,0,-1,0]]
    return np.array([face[index*3:index*3+3]+face[12:15] for face in faces for index in [0,1,2,0,2,3]],dtype=np.float32)

def transforms(count,seed,edits=None):
    side=math.ceil(math.sqrt(count));ids=np.arange(count,dtype=np.uint32)
    h=ids^np.uint32(seed&0xffffffff)^np.uint32(0x9e3779b9)
    h^=h>>16;h*=np.uint32(0x7feb352d);h^=h>>15;h*=np.uint32(0x846ca68b);h^=h>>16
    data=np.empty((count,4),dtype=np.float32)
    data[:,0]=(ids%side-(side-1)*.5)*2.2;data[:,1]=(ids//side-(side-1)*.5)*2.2
    data[:,2]=.7+(h&255).astype(np.float32)/128;data[:,3]=((h>>8)&255).astype(np.float32)/255
    # Match Java's float32 intermediate arithmetic for grid coordinates.
    data[:,0]=((ids%side).astype(np.float32)-np.float32((side-1)*.5))*np.float32(2.2)
    data[:,1]=((ids//side).astype(np.float32)-np.float32((side-1)*.5))*np.float32(2.2)
    for id,state in (edits or {}).items():
        if state==7:data[id,2]=0
        else:data[id,2]+=np.float32(state)*np.float32(.35)
    return data

class Renderer:
    def __init__(self,width=256,height=144):
        start=time.perf_counter_ns();self.gl=GL();g=self.gl
        self.programs={'eye':g.program(source('eye.vert'),source('eye.frag')),
                       'raster':g.program(source('raster.vert'),source('raster.frag'))}
        self.locations={p:{} for p in self.programs.values()}
        self.vao=g.object('glGenVertexArrays');g.glBindVertexArray(self.vao)
        self.vbo=g.object('glGenBuffers');g.glBindBuffer(0x8892,self.vbo)
        vertices=mesh();g.glBufferData(0x8892,vertices.nbytes,vertices.ctypes.data,0x88E4)
        for i in (0,1):
            g.glEnableVertexAttribArray(i);g.glVertexAttribPointer(i,3,0x1406,0,24,P(i*12))
        self.transforms_vbo=g.object('glGenBuffers')
        self.edits_texture=g.object('glGenTextures')
        self.quantum_texture=g.object('glGenTextures')
        self.color=g.object('glGenTextures');self.depth=g.object('glGenRenderbuffers');self.fbo=g.object('glGenFramebuffers')
        self.query=g.object('glGenQueries') if g.timer else None
        self.count=0;self.edits={};self.explicit_bytes=0
        self.quantum=np.zeros(1,dtype=np.float32);self.quantum_strength=0.
        self.mirror_settings=(0,16,True,1.)
        self.set_quantum(self.quantum,0.)
        self.resize(width,height)
        g.glDisable(0x0BD0);g.glDisable(0x0BE2);g.glDisable(0x0B44)
        g.check('initialize')
        self.initialization_ms=(time.perf_counter_ns()-start)/1e6

    def loc(self,program,name):
        cache=self.locations[program]
        if name not in cache:cache[name]=self.gl.glGetUniformLocation(program,name.encode())
        return cache[name]

    def resize(self,width,height):
        self.width=int(width);self.height=int(height);g=self.gl
        g.glBindTexture(0x0DE1,self.color)
        g.glTexImage2D(0x0DE1,0,0x8058,width,height,0,0x1908,0x1401,None)
        g.glTexParameteri(0x0DE1,0x2801,0x2600);g.glTexParameteri(0x0DE1,0x2800,0x2600)
        g.glBindRenderbuffer(0x8D41,self.depth);g.glRenderbufferStorage(0x8D41,0x81A6,width,height)
        g.glBindFramebuffer(0x8D40,self.fbo)
        g.glFramebufferTexture2D(0x8D40,0x8CE0,0x0DE1,self.color,0)
        g.glFramebufferRenderbuffer(0x8D40,0x8D00,0x8D41,self.depth)
        assert g.glCheckFramebufferStatus(0x8D40)==0x8CD5
        g.glViewport(0,0,width,height);g.check('resize')

    def scene(self,count,seed=2026,edits=None,prepare_explicit=True):
        if not 1<=count<=1048576:raise ValueError('Scene contract is 1..1048576 objects')
        edits=dict(edits or {})
        if len(edits)>count or any(not 0<=k<count or not 0<=v<=7 for k,v in edits.items()):
            raise ValueError('Invalid scene edits')
        start=time.perf_counter_ns()
        self.count=count;self.seed=seed;self.side=math.ceil(math.sqrt(count));self.edits=edits
        g=self.gl
        maximum=I();g.glGetIntegerv(0x0D33,C.byref(maximum))
        width=min(4096,maximum.value,max(1,len(edits)));height=max(1,math.ceil(len(edits)/width))
        if height>maximum.value:raise ValueError('Edit texture exceeds this device capability')
        data=np.full((height,width,2),-1,dtype=np.int32)
        for i,(id,state) in enumerate(sorted(edits.items())):data.reshape(-1,2)[i]=(id,state)
        g.glActiveTexture(0x84C0);g.glBindTexture(0x0DE1,self.edits_texture)
        g.glTexImage2D(0x0DE1,0,0x823B,width,height,0,0x8228,0x1404,data.ctypes.data)
        g.glTexParameteri(0x0DE1,0x2801,0x2600);g.glTexParameteri(0x0DE1,0x2800,0x2600)
        g.glTexParameteri(0x0DE1,0x2802,0x812F);g.glTexParameteri(0x0DE1,0x2803,0x812F)
        g.glFinish();compact_ms=(time.perf_counter_ns()-start)/1e6
        explicit_start=time.perf_counter_ns()
        self.explicit_ready=False
        if prepare_explicit:
            array=transforms(count,seed,edits)
            g.glBindBuffer(0x8892,self.transforms_vbo);g.glBufferData(0x8892,array.nbytes,array.ctypes.data,0x88E4)
            g.glVertexAttribPointer(2,4,0x1406,0,16,None);g.glVertexAttribDivisor(2,1)
            self.explicit_bytes=array.nbytes;self.explicit_ready=True
            g.glFinish()
        g.check('scene upload')
        return {'rule_and_edits_upload_ms':compact_ms,
                'explicit_build_upload_ms':(time.perf_counter_ns()-explicit_start)/1e6 if prepare_explicit else None,
                'explicit_vbo_bytes':self.explicit_bytes if prepare_explicit else 0,
                'logical_rule_bytes':32+8*len(edits),'edits_texture_bytes':data.nbytes}

    def set_quantum(self,values,strength):
        values=np.asarray(values,dtype=np.float32)
        if values.ndim!=1 or not len(values) or not np.isfinite(values).all() or np.max(np.abs(values))>1 or not abs(strength)<=.25:
            raise ValueError('Nonempty bounded expectations; absolute strength <= .25')
        self.quantum=values.copy();self.quantum_strength=float(strength)
        g=self.gl;maximum=I();g.glGetIntegerv(0x0D33,C.byref(maximum))
        width=min(len(values),4096,maximum.value);height=math.ceil(len(values)/width)
        if height>maximum.value:raise ValueError('Observable texture exceeds this device capability')
        data=np.zeros((height,width),dtype=np.float32);data.ravel()[:len(values)]=values
        g.glActiveTexture(0x84C1);g.glBindTexture(0x0DE1,self.quantum_texture)
        g.glTexImage2D(0x0DE1,0,0x822E,width,height,0,0x1903,0x1406,data.ctypes.data)
        g.glTexParameteri(0x0DE1,0x2801,0x2600);g.glTexParameteri(0x0DE1,0x2800,0x2600)
        g.glTexParameteri(0x0DE1,0x2802,0x812F);g.glTexParameteri(0x0DE1,0x2803,0x812F)
        g.glActiveTexture(0x84C0);g.check('quantum texture')

    def set_reflections(self,mode=0,*,budget=16,fold=True,reflectance=1.):
        """0 off, 1 floor mirror, 2 facing mirrors; one shared scene snapshot.

        Budget counts explicit bounces, not algebraically composed crossings.
        An unfinished ray is magenta and readback fails rather than hiding it.
        These observer settings are separate from the portable .cosmic world.
        """
        if type(mode) is not int or mode not in (0,1,2):raise ValueError('Mirror mode must be 0, 1 or 2')
        if type(budget) is not int or not 1<=budget<=64:raise ValueError('Explicit mirror budget must be 1..64')
        if type(fold) is not bool:raise ValueError('Mirror folding must be boolean')
        if type(reflectance) not in (float,int) or not math.isfinite(reflectance) or not 0<=reflectance<=1:
            raise ValueError('Mirror reflectance must be finite in [0,1]')
        self.mirror_settings=(mode,budget,fold,float(reflectance))

    def draw(self,route,cam,t=0.,debug=0,selected=-1,read=False,presentation=0,*,pixel=None):
        if route not in ('eye','procedural','explicit'):raise ValueError(route)
        if presentation not in (0,1):raise ValueError('Unknown observer presentation')
        if self.mirror_settings[0] and route!='eye':raise ValueError('Reflections require the eye ray route')
        if route=='explicit' and not self.explicit_ready:raise ValueError('Explicit reference not prepared')
        if not math.isfinite(t):raise ValueError('Nonfinite time')
        if pixel is not None:
            if (route!='eye' or debug!=1 or not read or len(pixel)!=2 or
                any(type(v) is not int for v in pixel) or
                not (0<=pixel[0]<self.width and 0<=pixel[1]<self.height)):
                raise ValueError('Picking requires one integer pixel inside the eye image')
        g=self.gl
        if self.mirror_settings[0] and 'mirrors' not in self.programs:
            fragment=source('eye.frag').replace('#version 300 es','#version 300 es\n#define MX_MIRRORS 1',1)
            p=g.program(source('eye.vert'),fragment)
            self.programs['mirrors']=p;self.locations[p]={}
        p=self.programs['mirrors' if self.mirror_settings[0] else 'eye' if route=='eye' else 'raster']
        g.glBindFramebuffer(0x8D40,self.fbo);g.glViewport(0,0,self.width,self.height)
        g.glUseProgram(p);g.glBindVertexArray(self.vao)
        for name,value in [('uCount',self.count),('uSide',self.side),('uEditCount',len(self.edits)),
                           ('uSelected',selected),('uDebug',debug),('uEdits',0),('uExplicit',int(route=='explicit')),
                           ('uQuantum',1),('uQuantumCount',len(self.quantum)),('uPresentation',presentation)]:
            g.glUniform1i(self.loc(p,name),value)
        mode,budget,fold,reflectance=self.mirror_settings
        for name,value in [('uMirrorMode',mode),('uMirrorBudget',budget),('uMirrorFold',int(fold))]:
            g.glUniform1i(self.loc(p,name),value)
        g.glUniform1f(self.loc(p,'uMirrorReflectance'),reflectance)
        for name,value in [('uTime',t),('uFogDistance',max(50,self.side*2.2)),('uQuantumStrength',self.quantum_strength),
                           ('uNear',cam['near']),('uFar',cam['far']),('uAspect',cam['aspect']),('uTanHalfFov',cam['tan_half_fov']),
                           ('uPixelAngle',2*cam['tan_half_fov']/self.height)]:
            g.glUniform1f(self.loc(p,name),value)
        seed=self.seed&0xffffffff;g.glUniform4ui(self.loc(p,'uSeedBytes'),*(seed>>(8*i)&255 for i in range(4)))
        g.glUniform3f(self.loc(p,'uEye'),*cam['eye'])
        for name,key in [('uCameraForward','forward'),('uCameraRight','right'),('uCameraUp','up')]:
            g.glUniform3f(self.loc(p,name),*cam[key])
        g.glUniform2f(self.loc(p,'uResolution'),self.width,self.height)
        for name,matrix in [('uVp',cam['vp']),('uInvVp',cam['inverse'])]:
            data=np.asarray(matrix.T,dtype=np.float32).copy();g.glUniformMatrix4fv(self.loc(p,name),1,0,data.ctypes.data)
        g.glActiveTexture(0x84C0);g.glBindTexture(0x0DE1,self.edits_texture)
        g.glActiveTexture(0x84C1);g.glBindTexture(0x0DE1,self.quantum_texture);g.glActiveTexture(0x84C0)
        if route=='explicit':g.glEnableVertexAttribArray(2)
        else:g.glDisableVertexAttribArray(2)
        if route=='eye':g.glDisable(0x0B44)
        else:g.glEnable(0x0B44)
        g.glEnable(0x0B71);g.glDepthFunc(0x0201);g.glDepthMask(1)
        if debug==1:g.glClearColor(0,0,0,1)
        else:g.glClearColor(.067,.086,.075,1)
        # Synchronous draw completion is reported separately from the driver's
        # elapsed query. Neither number is mislabeled as phone performance.
        g.glFinish();disjoint=I()
        if g.timer:g.glGetIntegerv(0x8FBB,C.byref(disjoint))
        start=time.perf_counter_ns()
        if g.timer:g.glBeginQuery(0x88BF,self.query)
        if pixel is not None:
            g.glEnable(0x0C11);g.glScissor(pixel[0],self.height-1-pixel[1],1,1)
        else:g.glDisable(0x0C11)
        try:
            g.glClear(0x4000|0x0100)
            if route=='eye':g.glDrawArrays(4,0,3)
            else:g.glDrawArraysInstanced(4,0,36,self.count)
        finally:g.glDisable(0x0C11)
        if g.timer:g.glEndQuery(0x88BF)
        g.glFinish();wall_ms=(time.perf_counter_ns()-start)/1e6
        query_ms=None;raw_query_ms=None
        if g.timer:
            elapsed=C.c_uint64();g.glGetQueryObjectui64vEXT(self.query,0x8866,C.byref(elapsed))
            g.glGetIntegerv(0x8FBB,C.byref(disjoint))
            raw_query_ms=elapsed.value/1e6
            # A driver returned an absolute-scale value on a first JIT draw.
            # Retain it for audit, but reject a duration incompatible with the
            # surrounding glFinish wall interval instead of reporting it.
            if not disjoint.value and 0<=raw_query_ms<=wall_ms*2+1:query_ms=raw_query_ms
        rgba=None
        if read:
            if pixel is None:
                rgba=np.empty((self.height,self.width,4),dtype=np.uint8)
                g.glReadPixels(0,0,self.width,self.height,0x1908,0x1401,rgba.ctypes.data)
                rgba=np.flipud(rgba).copy()
            else:
                rgba=np.empty((1,1,4),dtype=np.uint8)
                g.glReadPixels(pixel[0],self.height-1-pixel[1],1,1,0x1908,0x1401,rgba.ctypes.data)
            if np.any(np.all(rgba==[255,0,255,255],axis=2)):
                raise RuntimeError('Incomplete ray: grid traversal or explicit mirror budget exhausted')
        g.check('draw '+route)
        return {'wall_draw_finish_ms':wall_ms,'driver_elapsed_ms':query_ms,
                'raw_driver_elapsed_ms':raw_query_ms,'timer_disjoint':bool(disjoint.value)},rgba

    def close(self):self.gl.close()

def overview(side,width,height,angle=0.):
    extent=side*2.2
    eye=[extent*.72*math.cos(angle),extent*.8,extent*.95*math.sin(angle)+extent*.9]
    return camera(eye,[0,0,0],width,height,near=max(.05,extent*.01),far=extent*5)

def street(width,height,angle=0.):
    eye=[14*math.cos(angle),8,20*math.sin(angle)+18]
    return camera(eye,[0,1,0],width,height,near=.1,far=5000)
