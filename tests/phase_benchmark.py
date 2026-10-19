"""Same circuits/CPU/XYZ output for Mundo X and Qiskit Aer MPS.

Aer execution metadata is reported separately from its Python API wall time.
This is a comparison of these implementations, not an isolated algorithm ratio.
"""
import argparse
import json
import os
import pathlib
import statistics
import subprocess
import time
import numpy as np
import qiskit
import qiskit_aer
from qiskit.quantum_info import Pauli
from qiskit_aer import AerSimulator
from phase_circuits import adder, native_index, write_program, qiskit_circuit


def expected_basis(n, target):
    return [v for q in range(n) for v in (0.,0.,1.-2*((target>>q)&1))]


def native(runner, path, reps):
    r=subprocess.run([runner,str(path),"adaptive",str(reps)],text=True,capture_output=True,check=True,timeout=60)
    return json.loads(r.stdout)


def aer_circuit(n, gates):
    qc=qiskit_circuit(n,gates)
    for q in range(n):
        for axis in 'XYZ': qc.save_expectation_value(Pauli(axis),[q],label=f'{axis}{q}')
    return qc


def aer_run(simulator, qc):
    start=time.perf_counter_ns()
    result=simulator.run(qc,shots=1).result()
    stop=time.perf_counter_ns()
    assert result.success, result
    values=result.data(0)
    return {"api_wall_us":(stop-start)/1000.,
            "engine_execution_us":result.results[0].metadata['time_taken']*1e6,
            "pauli_xyz":[float(values[f'{axis}{q}']) for q in range(qc.num_qubits) for axis in 'XYZ'],
            "method":result.results[0].metadata['method']}


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--runner',required=True)
    ap.add_argument('--output-dir',required=True)
    args=ap.parse_args()
    folder=pathlib.Path(args.output_dir); folder.mkdir(parents=True,exist_ok=True)
    options=dict(method='matrix_product_state',precision='double',max_parallel_threads=1,
                 max_parallel_experiments=1,matrix_product_state_truncation_threshold=1e-28,
                 enable_truncation=False)
    # Both documented swap directions are tried; use the faster one for the
    # acceptance comparison. No restrictive bond dimension is imposed.
    sims=[AerSimulator(**options,mps_swap_direction=d) for d in ('mps_swap_left','mps_swap_right')]
    cases=[]
    for m in (6,10,25):
        cases.append((f'adder_{2*m}',m,21346502%(2**m),12207929%(2**m),False))
    cases.append(('superposed_adder_12',6,1,2,True))
    results={"versions":{"qiskit":qiskit.__version__,"qiskit_aer":qiskit_aer.__version__},
             "options":options,"timing_note":"Circuit construction/file parsing, imports and process start excluded. Native includes state initialization/evolution/XYZ readout; Aer metadata includes experiment execution/XYZ readout. API wall time is separate.",
             "benchmarks":[],"large_basis_checks":[]}
    for name,m,a,b,superposed in cases:
        n=2*m; gates=adder(m,a,b,superposed)
        path=folder/(name+'.qx'); write_program(path,n,gates)
        qc=aer_circuit(n,gates)
        # Independent launch groups reduce dependence on one in-process batch.
        native_runs=[]; aer_runs=[[],[]]
        for group in range(5):
            for idx in (range(2) if group%2 else reversed(range(2))):
                aer_run(sims[idx],qc)
                aer_runs[idx].extend(aer_run(sims[idx],qc) for _ in range(7))
            native_runs.append(native(args.runner,path,7))
        nt=[t for r in native_runs for t in r['init_evolution_readout_us']]
        aer_medians=[statistics.median(r['engine_execution_us'] for r in group) for group in aer_runs]
        best=min(range(2),key=lambda i:aer_medians[i])
        reference=np.array(aer_runs[best][-1]['pauli_xyz'])
        max_pair_error=max(float(np.max(np.abs(np.array(r['pauli_xyz'])-reference))) for r in native_runs)
        assert max_pair_error < 1e-10,(name,max_pair_error)
        exact_error=None
        if not superposed:
            expected=expected_basis(n,native_index(m,a,(a+b)%(2**m)))
            exact_error=max(float(np.max(np.abs(np.array(r['pauli_xyz'])-expected))) for r in native_runs)
            assert exact_error < 1e-10,(name,exact_error)
        result={"case":name,"qubits":n,"a":a,"b":b,"superposed":superposed,"unitary_gates":len(gates),
                "native_median_us":statistics.median(nt),"native_min_us":min(nt),"native_max_us":max(nt),
                "aer_engine_median_us":aer_medians[best],"aer_direction":('left','right')[best],
                "aer_api_median_us":statistics.median(r['api_wall_us'] for r in aer_runs[best]),
                "speed_ratio_aer_over_native":aer_medians[best]/statistics.median(nt),
                "native_product":native_runs[-1]['product'],"native_components":native_runs[-1]['components'],
                "native_logical_payload_bytes":native_runs[-1]['logical_payload_bytes'],
                "native_norm_squared":native_runs[-1]['norm_squared'],"max_observable_difference":max_pair_error,
                "max_exact_basis_observable_error":exact_error,"native_runs":native_runs,"aer_runs":aer_runs}
        results['benchmarks'].append(result)
        print(json.dumps({k:v for k,v in result.items() if k not in ('native_runs','aer_runs')}),flush=True)
        (folder/'comparison.json').write_text(json.dumps(results,indent=2)+'\n')
    rng=np.random.default_rng(750169)
    for i in range(16):
        m=25; a,b=map(int,rng.integers(0,2**m,size=2))
        gates=adder(m,a,b); path=folder/'additional_basis_50.qx'
        write_program(path,50,gates)
        nr=native(args.runner,path,1)
        ar=aer_run(sims[0],aer_circuit(50,gates))
        expected=expected_basis(50,native_index(m,a,(a+b)%(2**m)))
        ne=float(np.max(np.abs(np.array(nr['pauli_xyz'])-expected)))
        ae=float(np.max(np.abs(np.array(ar['pauli_xyz'])-expected)))
        assert ne < 1e-10 and ae < 1e-10,(i,ne,ae)
        results['large_basis_checks'].append(dict(a=a,b=b,sum_mod_25=(a+b)%(2**m),native_error=ne,aer_error=ae,
                                                product=nr['product'],norm=nr['norm_squared'],residual_sum=nr['product_residual_sum']))
    large=next(r for r in results['benchmarks'] if r['case']=='adder_50')
    results['main_acceptance_passed']=large['native_median_us'] < .85*large['aer_engine_median_us']
    (folder/'comparison.json').write_text(json.dumps(results,indent=2)+'\n')
    print(json.dumps({"additional_50_qubit_inputs_checked":len(results['large_basis_checks']),
                      "main_acceptance_passed":results['main_acceptance_passed']}),flush=True)


if __name__=='__main__': main()
