#!/usr/bin/env python3
"""Independent NumPy complex128 tensor/matrix oracle, including forced Z."""
import json
import sys
import numpy as np

single = {
    "H": np.array([[1,1],[1,-1]],dtype=np.complex128)/np.sqrt(2),
    "S": np.diag([1,1j]).astype(np.complex128),
    "X": np.array([[0,1],[1,0]],dtype=np.complex128),
    "Y": np.array([[0,-1j],[1j,0]],dtype=np.complex128),
    "Z": np.diag([1,-1]).astype(np.complex128),
    "T": np.diag([1,np.exp(1j*np.pi/4)]).astype(np.complex128),
}
double = {
    "CX": np.array([[1,0,0,0],[0,1,0,0],[0,0,0,1],[0,0,1,0]],dtype=np.complex128),
    "CZ": np.diag([1,1,1,-1]).astype(np.complex128),
}
def act(state,n,matrix,qubits):
    axes=[n-1-q for q in qubits]
    ordering=axes+[axis for axis in range(n) if axis not in axes]
    v=state.reshape((2,)*n).transpose(ordering).reshape(2**len(qubits),-1)
    return (matrix@v).reshape((2,)*n).transpose(np.argsort(ordering)).reshape(-1)

with open(sys.argv[1]) as f:
    n,depth=map(int,f.readline().split())
    gates=[line.split() for line in f]
assert len(gates)==depth
state=np.zeros(2**n,dtype=np.complex128); state[0]=1
nondeterministic=[]; probabilities=[]; declined_impossible=0
for op,a,b in gates:
    a=int(a); b=int(b)
    if op=="M":
        mask=((np.arange(2**n)>>a)&1)==b
        probability=float(np.vdot(state[mask],state[mask]).real/np.vdot(state,state).real)
        random=1e-10<probability<1-1e-10
        nondeterministic.append(random); probabilities.append(probability)
        if random:
            state[~mask]=0; state/=np.linalg.norm(state)
        elif probability<1e-10:
            # CompactCore deliberately returns false for any deterministic Z,
            # even when the requested branch is impossible; it leaves state.
            declined_impossible+=1
    else:
        state=act(state,n,double[op] if op in double else single[op],
                  [a,b] if op in double else [a])
out={"state":[[float(z.real),float(z.imag)] for z in state],
     "nondeterministic":nondeterministic,"measurement_probabilities":probabilities,
     "declined_impossible_measurements":declined_impossible,
     "numpy_version":np.__version__}
if len(sys.argv)>2:
    generators=json.load(open(sys.argv[2]))["generators"]
    expectations=[]
    masks=[]
    for generator in generators:
        acted=state.copy()
        for q,pauli in enumerate(generator[1:]):
            if pauli!="I": acted=act(acted,n,single[pauli],[q])
        sign=-1 if generator[0]=="-" else 1
        expectation=sign*np.vdot(state,acted)/np.vdot(state,state)
        expectations.append([float(expectation.real),float(expectation.imag)])
        x=sum(1<<q for q,p in enumerate(generator[1:]) if p in ("X","Y"))
        z=sum(1<<q for q,p in enumerate(generator[1:]) if p in ("Z","Y"))
        masks.append((x,z))
    out["generator_expectations"]=expectations
    pivots={}
    for x,z in masks:
        row=x|(z<<n)
        while row:
            pivot=row.bit_length()-1
            if pivot in pivots:row^=pivots[pivot]
            else:pivots[pivot]=row;break
    out["stabilizer_rank"]=len(pivots)
    out["generators_commute"]=all(((x&other_z).bit_count()+(z&other_x).bit_count())%2==0
                                  for x,z in masks for other_x,other_z in masks)
print(json.dumps(out))
