import ctypes as ct
from numbers import Integral
from pathlib import Path
import numpy as np

class Core:
    GATES = {'H':0, 'S':1, 'X':2, 'Z':3, 'CX':4, 'CNOT':4, 'CZ':5, 'Y':6}
    GATE_NAMES = ('H','S','X','Z','CX','CZ','Y')
    def __init__(self, n, backend='compact'):
        if backend not in ('compact', 'dense'):
            raise ValueError('Unknown backend')
        self.lib = ct.CDLL(str(Path(__file__).parent/'native/libmx_optical.so'))
        lib=self.lib
        lib.mx_last_error.restype=ct.c_char_p
        lib.mx_create.argtypes=[ct.c_int,ct.c_size_t]; lib.mx_create.restype=ct.c_void_p
        lib.mx_destroy.argtypes=[ct.c_void_p]
        lib.mx_apply.argtypes=[ct.c_void_p,ct.POINTER(ct.c_uint64),ct.c_size_t]
        lib.mx_apply.restype=ct.c_int
        lib.mx_restore.argtypes=[ct.c_void_p,ct.POINTER(ct.c_uint64),ct.c_size_t,ct.c_uint64]
        lib.mx_restore.restype=ct.c_int
        lib.mx_snapshot.argtypes=[ct.c_void_p,ct.POINTER(ct.c_uint64),ct.c_size_t,ct.POINTER(ct.c_size_t),ct.POINTER(ct.c_uint64)]
        lib.mx_snapshot.restype=ct.c_int
        lib.mx_query.argtypes=[ct.c_void_p,ct.POINTER(ct.c_char_p),ct.c_size_t,ct.POINTER(ct.c_double)]
        lib.mx_generator.argtypes=[ct.c_void_p,ct.c_size_t,ct.c_void_p,ct.c_size_t]
        for name in ('revision','epoch','rebuilds','state_bytes','observer_bytes'):
            fn=getattr(lib, 'mx_'+name)
            fn.argtypes=[ct.c_void_p]; fn.restype=ct.c_uint64
        if isinstance(n,bool) or not isinstance(n,Integral) or n<1:raise ValueError('Positive integer qubit count required')
        self.n=int(n); self.backend=backend
        self.handle=lib.mx_create(int(backend=='dense'),self.n)
        if not self.handle: self._check(-1)

    def _check(self, code):
        if code: raise ValueError(self.lib.mx_last_error().decode())

    def close(self):
        if self.handle:
            self.lib.mx_destroy(self.handle); self.handle=None

    def __enter__(self): return self
    def __exit__(self, *_): self.close()

    def _live(self):
        if not self.handle: raise RuntimeError('Core is closed')

    def _encode_gates(self,gates):
        rows=[]
        for gate in gates:
            if not isinstance(gate,(tuple,list)) or len(gate) not in (2,3) or type(gate[0]) is not str or gate[0] not in self.GATES:
                raise ValueError('Only H/S/X/Y/Z/CX/CZ supported by this bridge (CNOT aliases CX)')
            op=self.GATES[gate[0]]
            if len(gate)!=(3 if op in (4,5) else 2): raise ValueError('Gate arity')
            if any(isinstance(q,bool) or not isinstance(q,Integral) or not 0<=q<self.n for q in gate[1:]):
                raise ValueError('Gate operands must be existing integer qubit indices')
            a=int(gate[1]); b=int(gate[2]) if op in (4,5) else 0
            if op in (4,5) and a==b:raise ValueError('Two-qubit gate requires distinct indices')
            rows.append((op,a,b))
        return np.asarray(rows,dtype=np.uint64).reshape((-1,3))

    def apply(self, gates):
        self._live()
        a=self._encode_gates(gates)
        self._check(self.lib.mx_apply(self.handle,a.ctypes.data_as(ct.POINTER(ct.c_uint64)),len(a)))

    def restore(self,gates,revision):
        """Transactional replay from zero; the live handle keeps a fresh epoch."""
        self._live()
        if type(revision) is not int or not 0<=revision<=0xffffffffffffffff:raise ValueError('Invalid revision')
        a=self._encode_gates(gates)
        self._check(self.lib.mx_restore(self.handle,a.ctypes.data_as(ct.POINTER(ct.c_uint64)),len(a),revision))

    def snapshot(self):
        """Read native authoritative history and revision in one snapshot."""
        self._live()
        for _ in range(3):
            count=ct.c_size_t();revision=ct.c_uint64()
            self._check(self.lib.mx_snapshot(self.handle,None,0,ct.byref(count),ct.byref(revision)))
            values=np.empty((count.value,3),dtype=np.uint64)
            status=self.lib.mx_snapshot(self.handle,values.ctypes.data_as(ct.POINTER(ct.c_uint64)),len(values),ct.byref(count),ct.byref(revision))
            if status==0:
                history=[]
                for op,a,b in values[:count.value]:
                    op,a,b=int(op),int(a),int(b)
                    history.append((self.GATE_NAMES[op],a,b) if op in (4,5) else (self.GATE_NAMES[op],a))
                return {'qubits':self.n,'revision':revision.value,'gates':history}
        self._check(status)

    def history(self):return tuple(self.snapshot()['gates'])

    def query(self, paulis):
        self._live()
        encoded=[]
        for s in paulis:
            if not isinstance(s,str):raise ValueError('Pauli strings required')
            body=s[1:] if s[:1] in ('+','-') else s
            if len(body)!=self.n or any(c not in 'IXYZ' for c in body):
                raise ValueError('Invalid Hermitian Pauli')
            encoded.append(s.encode('ascii'))
        args=(ct.c_char_p*len(encoded))(*encoded)
        out=np.empty(len(encoded),dtype=np.float64)
        self._check(self.lib.mx_query(self.handle,args,len(encoded),out.ctypes.data_as(ct.POINTER(ct.c_double))))
        return out

    def generators(self):
        self._live()
        out=[]
        for i in range(self.n):
            buf=ct.create_string_buffer(self.n+2)
            self._check(self.lib.mx_generator(self.handle,i,buf,len(buf)))
            out.append(buf.value.decode())
        return out

    def counter(self, name):
        self._live()
        return int(getattr(self.lib,'mx_'+name)(self.handle))

    @property
    def revision(self): return self.counter('revision')

    @property
    def epoch(self): return self.counter('epoch')
