"""COSMIC X Engine: integrated quantum core and Mundo X graphic observer.

The first visual program defines opaque boxes on a finite plane grid.
CompactCore supplies exact stabilizer observables; GLES supplies image output.
This is a rendering engine foundation, not a universal quantum simulator.
"""
from dataclasses import dataclass
from pathlib import Path
import math,os,subprocess,tempfile,time
import numpy as np
from core_runtime import Core
from infinite_runtime import InfiniteCore, default_observables
from core_profile import DEFAULT_QUBITS
from renderer import Renderer,camera,overview,street

ROOT=Path(__file__).resolve().parent

def build_native():
    # Both Android and this observer compile the same repository-owned bridge
    # and core headers. No vendored copy or external engine implementation.
    folder=ROOT.parent/'native'
    binary=ROOT/'native/libmx_optical.so'
    sources=[folder/'src/graphics_core_bridge.cpp',folder/'src/infinite_bridge.cpp']
    dependencies=list((folder/'include').rglob('*.hpp'))+list((folder/'include').rglob('*.h'))+sources
    if not binary.exists() or any(p.stat().st_mtime>binary.stat().st_mtime for p in dependencies):
        binary.parent.mkdir(parents=True,exist_ok=True)
        fd,temporary=tempfile.mkstemp(prefix='.mx-build-',suffix='.so',dir=binary.parent)
        os.close(fd)
        try:
            subprocess.run(['c++','-std=c++17','-O3','-shared','-fPIC','-pthread',
                            '-I',str(folder/'include'),*[str(p) for p in sources],'-o',temporary],check=True)
            os.replace(temporary,binary)
        except subprocess.CalledProcessError as error:
            raise RuntimeError('The shared native core did not compile; see compiler output') from error
        finally:
            if os.path.exists(temporary):os.unlink(temporary)

def graph_program(n):
    gates=[('H',q) for q in range(n)]
    gates += [('CZ',q,q+1) for q in range(n-1)]
    gates += [('CZ',q,q+3) for q in range(0,n-3,2)]
    gates += [('S',q) for q in range(1,n,3)]
    return gates

@dataclass(frozen=True)
class VisualSnapshot:
    count:int
    seed:int
    scene_revision:int
    core_revision:int
    program_revision:int
    core_epoch:int
    edits:tuple
    coefficients:np.ndarray
    coupling:float

    @property
    def signature(self):
        return (self.scene_revision,self.core_epoch,self.program_revision)


class QuantumScene:
    """No per-object vertex/transform arrays are retained by the scene.

    The explicit visual law is h_i = hash_height(i, seed, edit_i) *
    animation(i,t) * (1 + coupling * <P_(i mod M)>).
    Every requested observable is preserved; M is not hard-coded to 64.
    """
    @property
    def count(self):return self._count
    @count.setter
    def count(self,value):
        if type(value) is not int or not 1<=value<=1_048_576:raise ValueError('Object count must be 1..1048576')
        if getattr(self,'_count',None)!=value:
            self._count=value
            if hasattr(self,'scene_revision'):self.scene_revision+=1

    @property
    def seed(self):return self._seed
    @seed.setter
    def seed(self,value):
        if type(value) is not int or not 0<=value<=0xffffffff:raise ValueError('Seed must be an unsigned 32-bit integer')
        if getattr(self,'_seed',None)!=value:
            self._seed=value
            if hasattr(self,'scene_revision'):self.scene_revision+=1

    @property
    def coupling(self):return self._coupling
    @coupling.setter
    def coupling(self,value):
        if type(value) not in (float,int) or not math.isfinite(value) or not abs(value)<=.25:raise ValueError('Absolute coupling <= .25')
        if getattr(self,'_coupling',None)!=value:
            self._coupling=float(value)
            if hasattr(self,'scene_revision'):self.scene_revision+=1

    @property
    def observables(self):return self._observables

    @property
    def edits(self):return self._sorted_edits

    @staticmethod
    def _validate_settings(count,qubits,coupling,default_program=True):
        if not isinstance(count,int) or not 1<=count<=1_048_576:
            raise ValueError('First grid backend supports 1..1048576 objects')
        if not 0<=abs(coupling)<=.25:raise ValueError('Absolute coupling <= .25')
        if type(qubits) is not int or qubits<1:
            raise ValueError('Positive integer qubit count required')

    def __init__(self,count=1_000_000,qubits=DEFAULT_QUBITS,seed=2026,coupling=.25):
        self._validate_settings(count,qubits,coupling)
        core=Core(qubits)
        try:
            core.apply(graph_program(qubits))
            self._attach(core,count,seed,coupling,owns_core=True)
        except Exception:
            core.close();raise

    @classmethod
    def from_core(cls,core,*,count=1_000_000,seed=2026,coupling=.25,observables=None):
        """Borrow an existing live CompactCore through this package's Core ABI.

        Does not prepare, reset, clone or measure the supplied state. Mutations
        through Core.apply remain visible by revision. The caller owns its
        lifetime and must keep it open until rendering has finished.
        """
        if not isinstance(core,Core) or core.backend!='compact':
            raise ValueError('A live compact Core from core_runtime is required')
        core._live()
        cls._validate_settings(count,core.n,coupling,observables is None)
        scene=cls.__new__(cls)
        scene._attach(core,count,seed,coupling,owns_core=False,observables=observables)
        return scene

    @classmethod
    def from_infinite_core(cls,core,*,count=1_000_000,seed=2026,coupling=.25,observables=None):
        """Borrow a live InfiniteSession and observe a finite, fixed program.

        There is no total register-size integer. Addresses stay in the sparse
        operators on the CPU; the existing GPU sees only their coefficients.
        """
        if not isinstance(core,InfiniteCore):
            raise ValueError('A live InfiniteCore from infinite_runtime is required')
        core._live()
        scene=cls.__new__(cls)
        scene._attach(core,count,seed,coupling,owns_core=False,
                      observables=default_observables() if observables is None else observables)
        return scene

    def _attach(self,core,count,seed,coupling,owns_core,observables=None):
        self.count=count;self.seed=seed;self.coupling=coupling
        self._core=core;self._owns_core=owns_core
        if observables is None:
            generators=core.generators();locals=[]
            for axis in ('X','Z'):
                for q in range(core.n):
                    p=['I']*core.n;p[q]=axis;locals.append(''.join(p))
            observables=generators+locals
        observables=tuple(observables)
        if not 1<=len(observables)<=4096:
            raise ValueError('The portable graphic program supports 1..4096 observables')
        core.query(observables)  # Read-only validation; never prepares the state.
        self._observables=observables
        self._edits={};self._sorted_edits=()
        self.scene_revision=0;self.program_revision=0
        self._snapshot=None;self._cached_core_key=None;self._values=None
        self.compilations=0;self.observable_batches=0

    def apply(self,gates):self._core.apply(gates)

    def set_observables(self,paulis):
        paulis=tuple(paulis)
        if not 1<=len(paulis)<=4096:raise ValueError('The portable graphic program supports 1..4096 observables')
        # Validate the complete list before changing its role in the scene.
        self._core.query(paulis)
        self._observables=paulis;self.program_revision+=1

    def edit(self,object_id,state):self.edit_many({object_id:state})

    def restore_edits(self,edits):
        rows=list(edits)
        if len(rows)>100_000:raise ValueError('Portable project supports up to 100000 stored edits')
        candidate={};previous=-1
        for row in rows:
            if not isinstance(row,(list,tuple)) or len(row)!=2:raise ValueError('Expected edit pair')
            k,v=row
            if type(k) is not int or not previous<k<1_048_576 or type(v) is not int or not 1<=v<=7:
                raise ValueError('Stored edits require sorted unique IDs and states 1..7')
            candidate[k]=v;previous=k
        if self._edits!=candidate:
            self._edits=candidate;self._sorted_edits=tuple(candidate.items());self.scene_revision+=1

    def edit_many(self,updates):
        updates=dict(updates)
        if any(type(k) is not int or not 0<=k<self.count or type(v) is not int or not 0<=v<=7 for k,v in updates.items()):
            raise ValueError('Edits require existing integer IDs and states 0..7')
        retained=set(self._edits)
        for k,v in updates.items():
            if v:retained.add(k)
            else:retained.discard(k)
        if len(retained)>100_000:raise ValueError('Portable project supports up to 100000 stored edits')
        if not any(self._edits.get(k,0)!=v for k,v in updates.items()):return
        for k,v in updates.items():
            if v==0:self._edits.pop(k,None)
            else:self._edits[k]=v
        self._sorted_edits=tuple(sorted(self._edits.items()));self.scene_revision+=1

    def compile(self):
        if isinstance(self._core,InfiniteCore):
            return self._compile_infinite()
        core_key=(self._core.epoch,self.program_revision)
        signature=(self.scene_revision,*core_key)
        if self._snapshot is not None and self._snapshot.signature==signature:return self._snapshot
        if core_key!=self._cached_core_key:
            self._values=self._core.query(self._observables).astype(np.float32)
            self._values.flags.writeable=False
            self._cached_core_key=core_key;self.observable_batches+=1
        self._snapshot=VisualSnapshot(self.count,self.seed,self.scene_revision,self._core.revision,self.program_revision,self._core.epoch,
                                      self._sorted_edits,self._values,self.coupling)
        self.compilations+=1
        return self._snapshot

    def _compile_infinite(self):
        # An epoch read can skip an unchanged observation. When an observation
        # is needed its coefficients, revision and epoch MUST come from one
        # native lock: a concurrent PC/host writer may change the same session.
        current_epoch=self._core.epoch
        core_key=(current_epoch,self.program_revision)
        signature=(self.scene_revision,*core_key)
        if self._snapshot is not None and self._snapshot.signature==signature:
            return self._snapshot
        if core_key!=self._cached_core_key:
            observed=self._core.query_observation(self._observables)
            self._values=observed.values.astype(np.float32)
            self._values.flags.writeable=False
            self._cached_core_key=(observed.epoch,self.program_revision)
            self._observed_revision=observed.revision
            self.observable_batches+=1
        observed_epoch,program_revision=self._cached_core_key
        self._snapshot=VisualSnapshot(self.count,self.seed,self.scene_revision,
            self._observed_revision,program_revision,observed_epoch,self._sorted_edits,
            self._values,self.coupling)
        self.compilations+=1
        return self._snapshot

    def describe(self):
        snapshot=self.compile()
        if isinstance(self._core,InfiniteCore):
            stats=self._core.stats()
            return {'objects':self.count,'backend':'infinite-clifford','background':stats['background'],
                    'active_qubits':stats['active_qubits'],'components':stats['components'],
                    'largest_component':stats['largest_component'],'observables':len(self._observables),
                    'scene_revision':self.scene_revision,'core_revision':snapshot.core_revision,
                    'program_revision':self.program_revision,'independent_edits':len(self._edits),
                    'logical_rule_bytes':32+8*len(self._edits),'core_tableau_bytes':stats['tableau_bytes'],
                    'core_address_bytes':stats['address_bytes'],'coefficient_bytes':snapshot.coefficients.nbytes,
                    'scope':'Infinite rule background with finite Clifford components and a finite graphic program; '
                            'payload byte counts exclude total RAM and GPU overhead'}
        return {'objects':self.count,'qubits':self._core.n,'observables':len(self._observables),
                'scene_revision':self.scene_revision,'core_revision':self._core.revision,
                'program_revision':self.program_revision,'independent_edits':len(self._edits),
                'logical_rule_bytes':32+8*len(self._edits),
                'core_state_array_bytes':self._core.counter('state_bytes'),
                'observer_payload_bytes':self._core.counter('observer_bytes'),
                'coefficient_bytes':snapshot.coefficients.nbytes,
                'scope':'Finite grid boxes driven by exact Clifford Pauli observables; sizes exclude runtime and GPU overhead'}

    def close(self):
        if self._owns_core:self._core.close()


class QuantumGraphicsEngine:
    def __init__(self,scene,width=1280,height=720):
        self.scene=scene;self.renderer=Renderer(width,height)
        self._uploaded_scene=None;self._uploaded_core=None
        self._frame_key=None;self._frame=None
        self.draws=0;self.reused_frames=0;self.last_sync={}

    def _sync(self):
        started=time.perf_counter_ns();snapshot=self.scene.compile()
        scene_key=(snapshot.count,snapshot.seed,snapshot.scene_revision)
        if scene_key!=self._uploaded_scene:
            active_edits={k:v for k,v in snapshot.edits if k<snapshot.count}
            self.last_sync=self.renderer.scene(snapshot.count,snapshot.seed,active_edits,prepare_explicit=False)
            self._uploaded_scene=scene_key
        core_key=(snapshot.core_epoch,snapshot.program_revision,snapshot.coupling)
        if core_key!=self._uploaded_core:
            self.renderer.set_quantum(snapshot.coefficients,snapshot.coupling)
            self._uploaded_core=core_key
        return snapshot,(time.perf_counter_ns()-started)/1e6

    def resize(self,width,height):
        self.renderer.resize(width,height);self._frame_key=None;self._frame=None

    def observe(self,cam,t=0.,read=True,selected=-1,force=False,presentation=0):
        started=time.perf_counter_ns();snapshot,sync_ms=self._sync()
        # The ray shader consumes the basis and projection scalars directly;
        # callers may change them without replacing vp. Compare every camera
        # uniform exactly instead of treating one matrix as their authority.
        camera_key=tuple(np.asarray(cam[name],dtype=np.float32).tobytes()
                         for name in ('vp','inverse','eye','forward','right','up'))
        camera_key+=tuple(float(cam[name]) for name in ('near','far','aspect','tan_half_fov'))
        key=(snapshot.signature,camera_key,float(t),int(selected),self.renderer.width,self.renderer.height,
             presentation,self.renderer.mirror_settings)
        if not force and key==self._frame_key and (not read or self._frame is not None):
            self.reused_frames+=1
            return self._frame,{'reused':True,'sync_ms':sync_ms,'total_ms':(time.perf_counter_ns()-started)/1e6}
        timing,pixels=self.renderer.draw('eye',cam,t,selected=selected,read=read,presentation=presentation)
        if pixels is not None:pixels.flags.writeable=False
        self._frame_key=key;self._frame=pixels;self.draws+=1
        return pixels,{'reused':False,'sync_ms':sync_ms,**timing,
                       'total_ms':(time.perf_counter_ns()-started)/1e6}

    def pick(self,cam,x,y,t=0.):
        if type(x) is not int or type(y) is not int or not 0<=x<self.renderer.width or not 0<=y<self.renderer.height:
            raise ValueError('Integer pixel outside image')
        self._sync()
        # A picking pass changes the framebuffer, including on failed readback.
        self._frame_key=None;self._frame=None
        _,rgba=self.renderer.draw('eye',cam,t,debug=1,read=True,pixel=(x,y))
        r,g,b=map(int,rgba[0,0,:3]);return r+(g<<8)+(b<<16)-1

    def close(self):self.renderer.close()
