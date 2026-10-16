"""Check transfer against the previous Mundo X binary; no rival timing runs."""
import argparse
import json
import math
import pathlib
import random
import statistics
import subprocess
import tempfile
from phase_circuits import adder, native_index, write_program


def run(runner, program, backend, reps):
    process = subprocess.run([runner, str(program), backend, str(reps)],
                             capture_output=True, text=True, check=True, timeout=60)
    return json.loads(process.stdout)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--runner', required=True)
    ap.add_argument('--previous-runner', required=True)
    ap.add_argument('--output', required=True)
    args = ap.parse_args()
    cases = [
        ('adder50', 50, adder(25, 21346502, 12207929), 35),
        ('superposed_adder12', 12, adder(6, 1, 2, True), 7),
        ('ghz50', 50, [('H', 0)] + [('C', 0, q) for q in range(1, 50)], 7),
        ('late_transfer50', 50, [('H', 0)] + [('P', 0, .001)] * 10000 +
         [('C', 0, 49), ('C', 0, 49)], 3),
    ]
    report = dict(previous_commit='40a9c8215fbe3e0bdc7a1242b3f9a982344d8d4e',
                  scope='Mundo X only; initialization, evolution and local XYZ readout',
                  memory_note='Logical engine payload, not allocator capacity, runner circuit, or process RSS',
                  cases=[], basis_inputs=[])
    with tempfile.TemporaryDirectory(prefix='mx-transfer-') as tmp:
        program = pathlib.Path(tmp)/'case.qx'
        for name, n, gates, reps in cases:
            write_program(program, n, gates)
            previous = run(args.previous_runner, program, 'adaptive', reps)
            current = run(args.runner, program, 'adaptive', reps)
            replay = run(args.runner, program, 'adaptive-replay', 1)
            error = max(abs(a-b) for a,b in zip(previous['pauli_xyz'],current['pauli_xyz']))
            replay_error = max(abs(a-b) for a,b in zip(previous['pauli_xyz'],replay['pauli_xyz']))
            assert error < 1e-10 and replay_error < 1e-10, (name,error,replay_error)
            assert abs(current['norm_squared']-1.) < 1e-10
            assert current['history_gates'] == current['replayed_gates'] == 0
            if name == 'late_transfer50':
                assert current['transferred_qubits'] == 50
                assert abs(current['pauli_xyz'][0]-math.cos(10.)) < 1e-10
                assert abs(current['pauli_xyz'][1]-math.sin(10.)) < 1e-10
            if name == 'adder50': assert current['logical_payload_bytes'] == 1600
            report['cases'].append(dict(case=name,qubits=n,gates=len(gates),repetitions=reps,
                previous_median_us=statistics.median(previous['init_evolution_readout_us']),
                current_median_us=statistics.median(current['init_evolution_readout_us']),
                previous_logical_bytes=previous['logical_payload_bytes'],
                current_logical_bytes=current['logical_payload_bytes'],
                previous_components=previous['components'],current_components=current['components'],
                transferred_qubits=current['transferred_qubits'],replayed_gates=current['replayed_gates'],
                max_observable_difference=error,legacy_replay_difference=replay_error,
                norm_error=abs(current['norm_squared']-1.)))
        rng = random.Random(750169)
        for _ in range(16):
            a, b = rng.randrange(2**25), rng.randrange(2**25)
            write_program(program,50,adder(25,a,b))
            result = run(args.runner,program,'adaptive',1)
            target = native_index(25,a,(a+b)%(2**25))
            expected = [v for q in range(50) for v in (0.,0.,1.-2*((target>>q)&1))]
            error = max(abs(x-y) for x,y in zip(result['pauli_xyz'],expected))
            assert error < 1e-10 and result['logical_payload_bytes'] == 1600
            report['basis_inputs'].append(dict(a=a,b=b,sum_mod_25=(a+b)%(2**25),error=error))
    report['all_passed'] = True
    pathlib.Path(args.output).write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__': main()
