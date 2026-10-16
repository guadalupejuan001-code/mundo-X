"""Independent state and observable checks against Qulacs FP64."""
import argparse
import json
import pathlib
import subprocess
import tempfile
import numpy as np
from qulacs import QuantumState
from phase_circuits import adder, native_index, write_program, qulacs_circuit


def expected_paulis(state, n):
    out = []
    indices = np.arange(len(state))
    for q in range(n):
        flipped = indices ^ (1 << q)
        signs = 1-2*((indices >> q)&1)
        out.extend([np.vdot(state[flipped], state).real,
                    np.sum(np.conj(state[flipped]) * 1j * signs * state).real,
                    np.sum(signs * abs(state)**2).real])
    return np.array(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runner", required=True)
    ap.add_argument("--output", required=True)
    args = ap.parse_args()
    rng = np.random.default_rng(361047)
    cases = []
    for m in (1, 2, 3):
        for a in range(2**m):
            for b in range(2**m):
                cases.append((f"adder_{m}_{a}_{b}", 2*m, adder(m,a,b), native_index(m,a,(a+b)%(2**m))))
    for n in (2, 3, 4, 6, 8):
        for trial in range(8):
            gates = [("H", q) for q in range(n)]
            for _ in range(36):
                op = str(rng.choice(["H","S","T","X","Z","P","C","Q"]))
                a,b = map(int,rng.choice(n,2,replace=False))
                if op == "P": gates.append((op,a,float(rng.uniform(-4,4))))
                elif op == "Q": gates.append((op,a,b,float(rng.uniform(-4,4))))
                elif op == "C": gates.append((op,a,b))
                else: gates.append((op,a))
            cases.append((f"random_{n}_{trial}",n,gates,None))
    for m in (2,3,4,5):
        cases.append((f"superposed_adder_{m}",2*m,adder(m,1,2,True),None))
    cases += [("late_fallback",4,[("H",0),("P",0,.39269908169872414),("H",1),("P",1,-.29),("Q",0,1,.71),("C",1,2),("T",3)],None),
              ("Y_sign",2,[("H",0),("S",0),("H",1),("P",1,-np.pi/2)],None),
              ("tiny_entangling_phase",2,[("H",0),("H",1),("Q",0,1,1e-7)],None)]
    results = []
    with tempfile.TemporaryDirectory(prefix="mx-phase-validation-") as tmp:
        program = pathlib.Path(tmp)/"case.qx"
        for name,n,gates,target in cases:
            reference = QuantumState(n)
            qulacs_circuit(n,gates).update_quantum_state(reference)
            v = reference.get_vector()
            expected = expected_paulis(v,n)
            write_program(program,n,gates)
            for backend in ("adaptive","reduced"):
                run=subprocess.run([args.runner,str(program),backend,"1","dump"],text=True,capture_output=True,timeout=20)
                if run.returncode: raise RuntimeError((name,backend,run.stderr))
                r=json.loads(run.stdout)
                actual=np.array([complex(*z) for z in r["state"]])
                norm=float(np.vdot(actual,actual).real)
                amplitude_error=float(np.max(np.abs(actual-v)))
                # Existing dense reconstruction uses float32 amplitudes. Record
                # raw error separately; the direct compact observables use FP64.
                normalized_error=float(np.max(np.abs(actual/np.sqrt(norm)-v)))
                observable_error=float(np.max(np.abs(np.array(r["pauli_xyz"])-expected)))
                norm_error=abs(r["norm_squared"]-1.)
                target_probability=None if target is None else float(abs(actual[target])**2/norm)
                assert amplitude_error < 4e-6,(name,backend,amplitude_error)
                assert normalized_error < 2e-6,(name,backend,normalized_error)
                assert observable_error < 1e-10,(name,backend,observable_error)
                assert norm_error < 1e-10,(name,backend,norm_error)
                if target is not None: assert target_probability > 1.-1e-10,(name,backend,target_probability)
                if name=="tiny_entangling_phase" and backend=="adaptive": assert not r["product"]
                results.append(dict(case=name,backend=backend,n=n,amplitude_error=amplitude_error,
                    normalized_amplitude_error=normalized_error,observable_error=observable_error,
                    norm_error=norm_error,target_probability=target_probability,product=r["product"],
                    components=r["components"],residual_sum=r["product_residual_sum"]))
    summary={"cases":len(cases),"engine_runs":len(results),"all_passed":True,
             "max_amplitude_error":max(x["amplitude_error"] for x in results),
             "max_normalized_amplitude_error":max(x["normalized_amplitude_error"] for x in results),
             "max_observable_error":max(x["observable_error"] for x in results),
             "max_norm_error":max(x["norm_error"] for x in results),"results":results}
    pathlib.Path(args.output).write_text(json.dumps(summary,indent=2)+"\n")
    print(json.dumps({k:v for k,v in summary.items() if k!="results"}))


if __name__ == "__main__": main()
