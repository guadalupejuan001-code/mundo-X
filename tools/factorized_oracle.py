#!/usr/bin/env python3
"""Reproducible independent complex128 tensor/matrix tests of FactorizedCore.

Compilation and execution share the experiment's flock, 20-second process
timeout and 1 GiB address-space ceiling. No network or original files required.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import random
import resource
import shutil
import subprocess
import sys
import tempfile
import time

for _thread_variable in ("OPENBLAS_NUM_THREADS", "OMP_NUM_THREADS", "MKL_NUM_THREADS"):
    os.environ[_thread_variable] = "1"
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
EXAM_ROOT = Path(tempfile.gettempdir()) / "mundox-factorized-exams"
DEFAULT_OUT_ROOT = EXAM_ROOT / "factorized-oracle-runs"
LOCK = EXAM_ROOT / "compute.lock"
SQRT_HALF = 1.0 / math.sqrt(2.0)
MATRICES = {
    "H": np.array([[1, 1], [1, -1]], dtype=np.complex128) * SQRT_HALF,
    "S": np.diag(np.array([1, 1j], dtype=np.complex128)),
    "T": np.diag(np.array([1, np.exp(1j * math.pi / 4)], dtype=np.complex128)),
    "X": np.array([[0, 1], [1, 0]], dtype=np.complex128),
    "Z": np.diag(np.array([1, -1], dtype=np.complex128)),
    "CX": np.array([[1, 0, 0, 0], [0, 1, 0, 0],
                    [0, 0, 0, 1], [0, 0, 1, 0]], dtype=np.complex128),
    "CZ": np.diag(np.array([1, 1, 1, -1], dtype=np.complex128)),
}


def evolve(state, n, gate):
    """Apply a matrix by tensor-axis permutation; q0 is the least significant."""
    op, *qubits = gate
    axes = [n - 1 - q for q in qubits]
    remaining = [axis for axis in range(n) if axis not in axes]
    permutation = axes + remaining
    inverse = np.argsort(permutation)
    tensor = state.reshape((2,) * n).transpose(permutation)
    updated = MATRICES[op] @ tensor.reshape(1 << len(qubits), -1)
    return updated.reshape((2,) * n).transpose(inverse).reshape(-1)


def gate(op, a, b=None):
    return [op, a] if b is None else [op, a, b]


def snap(label, **checks):
    return {"snapshot": label, "checks": checks}


def split(q):
    return {"split": q}


def inverse(circuit):
    result = []
    for operation in reversed(circuit):
        repetitions = {"S": 3, "T": 7}.get(operation[0], 1)
        result.extend([operation] * repetitions)
    return result


def make_cases():
    cases = []
    for n in range(1, 11):
        for trial in range(8):
            seed = 0x4D554E444F580000 + n * 100 + trial
            rng = random.Random(seed)
            operations = [snap("initial", factors=n, largest=1)]
            for step in range(96):
                op = rng.choice(list(MATRICES) if n > 1 else list(MATRICES)[:5])
                a = rng.randrange(n)
                b = rng.choice([q for q in range(n) if q != a]) if n > 1 else 0
                operations.append(gate(op, a, b) if op in ("CX", "CZ") else gate(op, a))
                if step % 13 == 0 or step == 95:
                    operations.append(snap(f"step_{step + 1}"))
            cases.append(dict(id=f"random_n{n}_trial{trial}", n=n, max_block=12,
                              seed=seed, operations=operations))
        # Disconnected non-stabilizer states must remain linear in storage.
        operations = []
        for q in range(n):
            operations += [gate("H", q), gate("T", q)]
        operations.append(snap("magic_product", factors=n, largest=1, amplitudes=2*n))
        cases.append(dict(id=f"magic_product_n{n}", n=n, max_block=12,
                          operations=operations))
        # T^2=S and T^8=I checked on superpositions, including phase-sensitive amplitudes.
        operations = []
        for q in range(n):
            operations += [gate("H", q)] + [gate("T", q)] * 8
            operations += [gate("T", q), gate("T", q)] + [gate("S", q)] * 3
        operations.append(snap("identities", factors=n, largest=1))
        cases.append(dict(id=f"t_identities_n{n}", n=n, max_block=12,
                          operations=operations))

    for n in (2, 3, 5, 8, 10):
        ghz = [gate("H", 0)] + [gate("CX", 0, q) for q in range(1, n)]
        operations = ghz + [snap("entangled", factors=1, largest=n)]
        operations += list(reversed(ghz[1:]))
        operations += [split(q) for q in range(n)]
        operations += [snap("disconnected", factors=n, largest=1)]
        cases.append(dict(id=f"ghz_disconnect_n{n}", n=n, max_block=12,
                          operations=operations))

    for op in ("CX", "CZ"):
        for control in (0, 1):
            operations = ([gate("X", 0)] if control else []) + [gate("H", 1), gate("T", 1)]
            operations += [gate(op, 0, 1), snap("computational_control", factors=2, largest=1),
                           split(0), snap("computational_control_recovered", factors=2, largest=1)]
            cases.append(dict(id=f"{op.lower()}_basis_control{control}", n=2,
                              max_block=12, operations=operations))
        operations = [gate("H", 0), gate("T", 0), gate("H", 1), gate("T", 1)]
        operations += [gate(op, 0, 1), snap("connected", factors=1, largest=2), gate(op, 0, 1)]
        # A deliberately retained regression: coefficient-wise == can refuse
        # a mathematically separable complex product after float roundoff.
        operations += [split(0), split(1), snap("strict_false_negative", factors=1, largest=2, split=0)]
        cases.append(dict(id=f"{op.lower()}_twice_complex_product_strict_false_negative", n=2, max_block=12,
                          operations=operations))
        operations = [gate("H", 0), gate("H", 1), gate(op, 0, 1),
                      gate(op, 0, 1), split(0), split(1),
                      snap("recovered_binary_product", factors=2, largest=1)]
        cases.append(dict(id=f"{op.lower()}_twice_binary_product", n=2, max_block=12,
                          operations=operations))

    for trial in range(10):
        seed = 0x494E564552534500 + trial
        rng = random.Random(seed)
        circuit = []
        for _ in range(60):
            op = rng.choice(list(MATRICES))
            a, b = rng.sample(range(6), 2)
            circuit.append(gate(op, a, b) if op in ("CX", "CZ") else gate(op, a))
        cases.append(dict(id=f"inverse_trial{trial}", n=6, max_block=12,
                          seed=seed, operations=circuit + inverse(circuit) + [snap("inverse_identity")]))

    operations = [gate("H", 0), gate("CX", 0, 1), gate("H", 2), gate("CX", 2, 3)]
    operations += [snap("bell_pairs", factors=2, largest=2), gate("CX", 1, 2),
                   snap("joined", factors=1, largest=4), gate("CX", 1, 2)]
    operations += [split(q) for q in range(4)]
    operations += [snap("singleton_search_cannot_recover_pairs", factors=1, largest=4),
                   {"split_set": [0, 1]}, snap("bell_pairs_unjoined", factors=2, largest=2, split=1)]
    cases.append(dict(id="bell_pair_join_unjoin_explicit_cut", n=4,
                      max_block=12, operations=operations))

    operations = [gate("H", 0), gate("CX", 0, 1), gate("H", 2), gate("T", 2),
                  snap("before_limit", factors=2, largest=2),
                  {"reject": gate("CX", 1, 2)},
                  snap("after_limit", factors=2, largest=2, rejected=1, identical_to="before_limit"),
                  {"reject": gate("H", 3)}, snap("invalid_qubit", rejected=1, identical_to="after_limit"),
                  {"reject": gate("CX", 0, 0)}, snap("same_qubit", rejected=1, identical_to="invalid_qubit")]
    cases.append(dict(id="block_limit_atomic_rejection", n=3, max_block=2,
                      operations=operations))

    # Repeated HT visits a nearly computational state. Its nonzero Schmidt
    # coefficient must not be discarded by a strict factorization attempt.
    state = np.array([1.0, 0.0], dtype=np.complex128)
    best = (1.0, 0)
    for repetitions in range(1, 20001):
        state = evolve(evolve(state, 1, gate("H", 0)), 1, gate("T", 0))
        small = min(abs(state))
        if 0 < small < best[0]:
            best = (small, repetitions)
    operations = [g for _ in range(best[1]) for g in (gate("H", 0), gate("T", 0))]
    operations += [gate("CX", 0, 1), snap("near_product_before", factors=1, largest=2),
                   split(0), snap("strict_split_rejected", factors=1, largest=2, split=0,
                                  identical_to="near_product_before")]
    cases.append(dict(id="nearly_separable_strict_no_truncation", n=2, max_block=12,
                      smallest_schmidt_estimate=best[0], ht_repetitions=best[1],
                      operations=operations))

    # Flat singleton storage must survive unrelated representative slot reuse.
    # Every cycle restores a basis product, including its accumulated phase.
    for trial in range(3):
        seed = 0x534C4F5452455553 + trial
        rng = random.Random(seed)
        operations = []
        for cycle in range(16):
            control, target = rng.sample(range(10), 2)
            operations += [gate("H", control), gate("CX", control, target),
                           snap(f"cycle_{cycle}_merged", factors=9, largest=2),
                           gate("CX", control, target), split(target), split(control),
                           snap(f"cycle_{cycle}_detached", factors=10, largest=1),
                           gate("X", control), gate("S", control),
                           gate("X", control), gate("S", control), gate("H", control),
                           snap(f"cycle_{cycle}_phase_restored", factors=10, largest=1)]
        cases.append(dict(id=f"layout_singleton_detach_reuse_trial{trial}", n=10,
                          seed=seed, max_block=12, operations=operations))

    pairs = [(7, 1), (4, 0), (6, 2), (5, 3)]
    operations = [operation for control, target in pairs
                  for operation in (gate("H", control), gate("CX", control, target))]
    operations += [snap("remote_pairs", factors=4, largest=2)]
    for cycle, (a, b, cut) in enumerate(((1, 4, [1, 7]), (6, 7, [2, 6]),
                                        (3, 0, [5, 3]), (5, 6, [6, 2]))):
        operations += [gate("CX", a, b), snap(f"bridge_{cycle}", factors=3, largest=4),
                       gate("CX", a, b), {"split_set": cut},
                       snap(f"cut_{cycle}", factors=4, largest=2, split=1)]
    for control, target in reversed(pairs):
        operations += [gate("CX", control, target), split(target), split(control),
                       gate("H", control)]
    operations.append(snap("remote_all_singletons", factors=8, largest=1))
    cases.append(dict(id="layout_noncontiguous_dense_dense_cuts", n=8,
                      max_block=12, operations=operations))

    # Reconstructing a binary product can produce non-unit-norm individual
    # factors. Queries must normalize the queried factor, not change the state.
    operations = [gate("H", q) for q in (0, 5, 2, 4)]
    operations += [gate("CX", 5, 0), split(5), gate("CX", 2, 4), split(2),
                   snap("split_gauges", factors=6, largest=1), gate("CZ", 5, 4),
                   snap("gauge_singletons_joined", factors=5, largest=2),
                   gate("CX", 0, 2), snap("two_gauge_blocks", factors=4, largest=2),
                   gate("CZ", 0, 5), snap("gauge_dense_dense", factors=3, largest=4),
                   gate("CZ", 0, 5), {"split_set": [2, 0]},
                   snap("gauge_cut", factors=4, largest=2)]
    cases.append(dict(id="layout_split_unnormalized_gauges", n=6, max_block=12,
                      operations=operations))

    # The computational-control fast path must retain a phase stored in its
    # singleton, including after that singleton was detached from a dense block.
    for one in (0, 1):
        operations = [gate("H", 5), gate("CX", 5, 1), gate("CX", 5, 1),
                      split(1), split(5), gate("H", 5),
                      gate("X", 5), gate("Z", 5), gate("X", 5)]
        if one:
            operations.append(gate("X", 5))
        operations += [gate("H", 2), gate("T", 2), gate("CX", 5, 2),
                       snap("phased_cx", factors=6, largest=1), gate("CZ", 5, 2),
                       snap("phased_cz", factors=6, largest=1)]
        cases.append(dict(id=f"layout_detached_negative_basis_control{one}", n=6,
                          max_block=12, operations=operations))

    operations = [gate("H", 3), gate("CX", 3, 0), snap("before_invalid_queries")]
    for index, (kind, argument) in enumerate((("AMPLITUDE", 3), ("AMPLITUDE", 5),
                                             ("LOG", 0), ("LOG", 5), ("PROB", 4),
                                             ("EXPECTZ", 4), ("FACTOR", 4), ("SAME", 4))):
        previous = "before_invalid_queries" if index == 0 else f"invalid_query_{index-1}"
        operations += [{"reject_query": [kind, argument]},
                       snap(f"invalid_query_{index}", rejected=1, identical_to=previous)]
    cases.append(dict(id="layout_query_width_and_index_guards", n=4,
                      max_block=12, operations=operations))
    return cases


def limit_process():
    resource.setrlimit(resource.RLIMIT_AS, (1024**3, 1024**3))
    resource.setrlimit(resource.RLIMIT_CPU, (20, 20))
    if hasattr(os, "sched_getaffinity"):
        os.sched_setaffinity(0, {min(os.sched_getaffinity(0))})


def run_locked(command, **kwargs):
    LOCK.parent.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, OPENBLAS_NUM_THREADS="1", OMP_NUM_THREADS="1", MKL_NUM_THREADS="1")
    with LOCK.open("a") as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        return subprocess.run(command, env=env, timeout=20, check=True,
                              preexec_fn=limit_process, text=True, capture_output=True, **kwargs)


def prepare(cases):
    lines, expected = [], {}
    for case in cases:
        n = case["n"]
        state = np.zeros(1 << n, dtype=np.complex128)
        state[0] = 1
        lines.append(f"CASE {case['id']} {n} {case['max_block']}")
        for operation in case["operations"]:
            if isinstance(operation, list):
                lines.append(" ".join(map(str, operation)))
                state = evolve(state, n, operation)
            elif "snapshot" in operation:
                label = operation["snapshot"]
                lines.append(f"SNAP {label}")
                expected[(case["id"], label)] = (state.copy(), operation["checks"], n)
            elif "split" in operation:
                lines.append(f"TRY {operation['split']}")
            elif "split_set" in operation:
                qubits = operation["split_set"]
                lines.append("TRYSET " + str(len(qubits)) + " " + " ".join(map(str, qubits)))
            elif "reject" in operation:
                lines.append("REJECT " + " ".join(map(str, operation["reject"])))
            elif "reject_query" in operation:
                lines.append("REJECT_QUERY " + " ".join(map(str, operation["reject_query"])))
        lines.append("END")
    return "\n".join(lines) + "\n", expected


def check_results(output, expected):
    errors, metrics, seen = [], [], set()
    stored_states = {}
    for line in output.splitlines():
        actual = json.loads(line)
        key = actual["case"], actual["label"]
        if key in seen:
            errors.append({"case": key, "reason": "duplicate snapshot"})
        seen.add(key)
        reference, checks, n = expected[key]
        dense = np.asarray(actual.pop("state"), dtype=np.float64)
        dense = dense[:, 0] + 1j*dense[:, 1]
        l2 = float(np.linalg.norm(dense-reference))
        probabilities = np.abs(reference.reshape((2,)*n)) ** 2
        probabilities /= probabilities.sum()
        marginal = np.array([np.take(probabilities, 1, axis=n-1-q).sum() for q in range(n)])
        probability_error = float(np.max(np.abs(np.array(actual["probability_one"])-marginal)))
        expectation_error = float(np.max(np.abs(np.array(actual["expectation_z"])-(1-2*marginal))))
        selected = np.asarray(actual.pop("queried_amplitudes"), dtype=np.float64)
        selected = selected[:, 0] + 1j*selected[:, 1]
        indexes = [0, 1, len(reference)//3, len(reference)-1]
        amplitude_error = float(np.max(np.abs(selected-reference[indexes])))
        tolerance = 3e-11
        actual.update(l2_error=l2, probability_error=probability_error,
                      expectation_error=expectation_error, amplitude_error=amplitude_error,
                      reference_norm=float(np.linalg.norm(reference)),
                      actual_norm=float(np.linalg.norm(dense)))
        metrics.append(actual)
        for field, value in checks.items():
            if field == "identical_to":
                previous_state, previous_partition = stored_states[(key[0], value)]
                if not np.array_equal(dense, previous_state) or actual["partition"] != previous_partition:
                    errors.append({"case": key, "reason": f"state/partition changed from {value}"})
                continue
            if actual[field] != value:
                errors.append({"case": key, "reason": f"{field}: expected {value}, got {actual[field]}"})
        if max(l2, probability_error, expectation_error, amplitude_error) > tolerance:
            errors.append({"case": key, "reason": "independent oracle mismatch", "metrics": actual})
        partition = actual["partition"]
        if sorted(q for block in partition for q in block) != list(range(n)):
            errors.append({"case": key, "reason": "partition does not cover each qubit once"})
        if actual["amplitudes"] != sum(2**len(block) for block in partition):
            errors.append({"case": key, "reason": "logical amplitude count mismatch"})
        if actual["factors"] != len(partition) or actual["largest"] != max(map(len, partition)):
            errors.append({"case": key, "reason": "factor count or largest factor metadata mismatch"})
        if actual["logical_bytes"] != 16*actual["amplitudes"]:
            errors.append({"case": key, "reason": "logical amplitude byte count mismatch"})
        expected_members = [next(block for block in partition if q in block) for q in range(n)]
        if actual["factor_qubits"] != expected_members:
            errors.append({"case": key, "reason": "per-qubit factor membership mismatch"})
        if actual["factor_qubit_counts"] != list(map(len, expected_members)):
            errors.append({"case": key, "reason": "per-qubit factor width mismatch"})
        same = [b in expected_members[a] for a in range(n) for b in range(n)]
        if actual["same_factor"] != same:
            errors.append({"case": key, "reason": "same-factor mapping mismatch"})
        stored_states[key] = (dense, actual["partition"])
    for key in expected.keys() - seen:
        errors.append({"case": key, "reason": "missing snapshot"})
    return metrics, errors


def wide_protocol():
    lines, expected = [], {}
    for n in (4096, 100000):
        case_id = f"analytic_product_n{n}"
        lines += [f"CASE {case_id} {n} 20", "LOG initial_zero one", "ALLHT"]
        expected[(case_id, "initial_zero")] = dict(n=n, zero=True, probability=0)
        for pattern in ("zero", "one", "third"):
            lines.append(f"LOG ht_{pattern} {pattern}")
            ones = n if pattern == "one" else ((n+2)//3 if pattern == "third" else 0)
            phase = math.remainder((ones % 8) * math.pi / 4, 2*math.pi)
            expected[(case_id, f"ht_{pattern}")] = dict(
                n=n, zero=False, probability=0.5,
                log_magnitude=-n*math.log(2)/2, phase=phase)
        lines.append("END")
    return "\n".join(lines) + "\n", expected


def check_wide(output, expected):
    metrics, errors, seen = [], [], set()
    for line in output.splitlines():
        actual = json.loads(line)
        key = actual["case"], actual["label"]
        reference = expected[key]
        seen.add(key)
        n = reference["n"]
        for field, value in dict(log_zero=reference["zero"], factors=n,
                                 amplitudes=2*n, logical_bytes=32*n).items():
            if actual[field] != value:
                errors.append(dict(case=key, reason=f"{field}: expected {value}, got {actual[field]}"))
        probability_error = max(abs(value-reference["probability"])
                                for value in actual["probability_one"])
        actual["probability_error"] = probability_error
        if probability_error > 1e-13:
            errors.append(dict(case=key, reason="analytic local probability mismatch"))
        if not reference["zero"]:
            log_error = abs(actual["log_magnitude"]-reference["log_magnitude"])
            phase_error = abs(math.remainder(actual["phase"]-reference["phase"], 2*math.pi))
            actual.update(log_magnitude_error=log_error, phase_error=phase_error)
            if max(log_error, phase_error) > 1e-10:
                errors.append(dict(case=key, reason="analytic logarithmic amplitude mismatch"))
            raw = complex(*actual["raw_amplitude"])
            actual["raw_numeric_class"] = "zero" if raw == 0 else "subnormal" if abs(raw) < np.finfo(float).tiny else "normal"
            if actual["raw_numeric_class"] == "normal":
                errors.append(dict(case=key, reason="expected unrepresentably small raw amplitude"))
        metrics.append(actual)
    for key in expected.keys()-seen:
        errors.append(dict(case=key, reason="missing wide analytic query"))
    return metrics, errors


def fingerprint(path):
    path = Path(path).resolve()
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024*1024), b""):
            digest.update(chunk)
    return dict(path=str(path), sha256=digest.hexdigest(), bytes=path.stat().st_size)


def main():
    global LOCK
    parser = argparse.ArgumentParser(description=__doc__)
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT_ROOT / timestamp,
                        help="Fresh output directory; existing evidence is never overwritten.")
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--compute-lock", type=Path, default=LOCK,
                        help="Shared lock also used by stress_factorized.py.")
    args = parser.parse_args()
    LOCK = args.compute_lock.resolve()
    args.out = args.out.resolve()
    if args.out.exists() and (not args.out.is_dir() or any(args.out.iterdir())):
        parser.error(f"--out must be empty or absent; preserving existing evidence: {args.out}")
    args.out.mkdir(parents=True, exist_ok=True)
    runner = (args.runner or args.out / "factorized-oracle-runner").resolve()
    compiler_command = None
    if args.runner is None:
        compiler_command = ["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wpedantic",
                            "-I", str(ROOT / "native/include"),
                            str(ROOT / "native/tests/factorized_oracle_runner.cpp"), "-o", str(runner)]
        build = run_locked(compiler_command)
        (args.out / "build.log").write_text(build.stdout + build.stderr)
    started = time.perf_counter()
    cases = make_cases()
    protocol, expected = prepare(cases)
    (args.out / "cases.json").write_text(json.dumps(cases, indent=2) + "\n")
    (args.out / "protocol.txt").write_text(protocol)
    protocol_wide, expected_wide = wide_protocol()
    (args.out / "wide-protocol.txt").write_text(protocol_wide)
    metadata = dict(
        created_at_utc=datetime.now(timezone.utc).isoformat(),
        invocation=[sys.executable, str(Path(__file__).resolve()), *sys.argv[1:]],
        compiler_command=compiler_command,
        runner_origin="compiled_by_this_run" if args.runner is None else "provided_executable",
        versions=dict(python=sys.version, numpy=np.__version__),
        executables=dict(
            runner=fingerprint(runner),
            python=fingerprint(sys.executable),
            compiler=fingerprint(shutil.which("g++")) if compiler_command else None),
        sources={
            "runner": fingerprint(ROOT / "native/tests/factorized_oracle_runner.cpp"),
            "factorized_core": fingerprint(ROOT / "native/include/mundo_x/factorized_core.hpp"),
            "oracle_script": fingerprint(__file__),
        },
        inputs=dict(cases=fingerprint(args.out / "cases.json"),
                    protocol=fingerprint(args.out / "protocol.txt"),
                    wide_protocol=fingerprint(args.out / "wide-protocol.txt") if protocol_wide else None),
        cases=[dict(id=case["id"], qubits=case["n"], seed=case.get("seed")) for case in cases],
        execution_commands=[dict(mode="engine", command=[str(runner)], stdin=str(args.out / "protocol.txt")),
                            dict(mode="wide_analytic", command=[str(runner)], stdin=str(args.out / "wide-protocol.txt"))],
        execution_limits=dict(address_space_bytes=1024**3, timeout_seconds=20,
                              cpu_seconds=20, cpus=1, blas_threads=1,
                              cpu_affinity=[min(os.sched_getaffinity(0))] if hasattr(os, "sched_getaffinity") else None,
                              lock=str(LOCK)),
    )
    (args.out / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    result = run_locked([str(runner)], input=protocol)
    (args.out / "engine.jsonl").write_text(result.stdout)
    (args.out / "engine.stderr").write_text(result.stderr)
    metrics, errors = check_results(result.stdout, expected)
    (args.out / "engine-metrics.json").write_text(json.dumps(metrics, indent=2) + "\n")
    result_wide = run_locked([str(runner)], input=protocol_wide)
    (args.out / "wide.jsonl").write_text(result_wide.stdout)
    wide_metrics, wide_errors = check_wide(result_wide.stdout, expected_wide)
    (args.out / "wide-metrics.json").write_text(json.dumps(wide_metrics, indent=2) + "\n")
    errors.extend(dict(mode="wide_analytic", **error) for error in wide_errors)
    summary = dict(passed=not errors, case_count=len(cases), snapshots=len(metrics),
                   layout_adversarial_case_count=sum(case["id"].startswith("layout_") for case in cases),
                   gates=sum(isinstance(op, list) for case in cases for op in case["operations"]),
                   max_l2_error=max(m["l2_error"] for m in metrics),
                   max_probability_error=max(m["probability_error"] for m in metrics),
                   max_expectation_error=max(m["expectation_error"] for m in metrics),
                   max_amplitude_error=max(m["amplitude_error"] for m in metrics),
                   wide_analytic_queries=len(wide_metrics),
                   wide_max_log_magnitude_error=max((m.get("log_magnitude_error", 0) for m in wide_metrics), default=0),
                   wide_max_phase_error=max((m.get("phase_error", 0) for m in wide_metrics), default=0),
                   elapsed_including_python_seconds=time.perf_counter()-started,
                   tolerance=3e-11, oracle="NumPy complex128 tensor-axis matrix evolution",
                   wide_log_phase_tolerance=1e-10,
                   mode="engine",
                   observed_limitations=[
                       "Strict coefficient-wise == splitting can reject mathematically separable HT tensor HT after CX^2 or CZ^2; coefficients are preserved.",
                       "Full-state oracle tests cover only 1 through 10 qubits; they do not establish correctness for every circuit.",
                       "Raw complex<double> amplitudes for large products underflow to zero or subnormal values. Logarithmic amplitude preserves magnitude and phase without a dense export; it does not fix underflow inside a dense factor.",
                   ],
                   execution_limits=dict(address_space_bytes=1024**3, timeout_seconds=20,
                                         cpu_seconds=20, cpus=1, blas_threads=1,
                                         cpu_affinity=[min(os.sched_getaffinity(0))] if hasattr(os, "sched_getaffinity") else None,
                                         lock=str(LOCK)),
                   errors=errors)
    (args.out / "metrics.json").write_text(json.dumps(metrics, indent=2) + "\n")
    (args.out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    raise SystemExit(0 if summary["passed"] else 1)


if __name__ == "__main__":
    main()
