#!/usr/bin/env python3
"""Compare identical native workloads against frozen and candidate headers.

Example:
  python3 tools/core-performance.py --baseline-include /tmp/old/native/include \
    --baseline-id 403bdd7311e6d6b37e849fbe2c85375a9a2cd2ac \
    --output results/core_performance_2026-10-19.json

No external simulator is used. These are host CPU component measurements,
not Android FPS, GPU timings, 8K/12K frame rates or an end-to-end app speedup.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile
from datetime import datetime, timezone


def command(argv):
    return subprocess.check_output([str(x) for x in argv], text=True)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def iterations(kind, qubits):
    if kind == "cz_gate":
        return 65536
    if kind == "measurement_round":
        return max(4, 16000 // (qubits * qubits // 16))
    if kind == "cached_query":
        return 2048
    return max(64, 65536 // qubits)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-include", type=Path, required=True)
    parser.add_argument("--baseline-id", default="unspecified; inspect header hashes")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--samples", type=int, default=7)
    parser.add_argument("--qubits", type=int, nargs="+", default=[20, 64, 128, 256])
    parser.add_argument("--cases", nargs="+", default=[
        "observer_build", "cz_gate", "measurement_round", "graphics_revision", "cached_query"])
    args = parser.parse_args()
    if args.samples < 3:
        parser.error("Use at least three independent samples")
    root = Path(__file__).resolve().parents[1]
    candidate = root / "native/include"
    headers = ["mundo_x/compact_core.hpp", "mundo_x/pauli_observer.hpp", "mundo_x/engine_session.hpp"]
    output = {
        "schema": 1,
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "baseline_id": args.baseline_id,
        "host": platform.platform(),
        "architecture": platform.machine(),
        "compiler": command([args.compiler, "--version"]).splitlines()[0],
        "compile_flags": ["-std=c++17", "-O3", "-DNDEBUG", "-pthread"],
        "samples_per_version": args.samples,
        "comparison_order": "alternating baseline/candidate then candidate/baseline",
        "scope": "Host CPU core components; not phone, GPU, FPS or rendering resolution",
        "seed_hex": "434f52455f504552",
        "header_sha256": {
            name: {"baseline": digest(args.baseline_include / name), "candidate": digest(candidate / name)}
            for name in headers
        },
        "benchmark_sha256": digest(root / "native/tests/core_performance_benchmark.cpp"),
        "cases": [],
    }
    if hasattr(os, "sched_getaffinity"):
        cpu = min(os.sched_getaffinity(0))
        os.sched_setaffinity(0, {cpu})
        output["pinned_cpu"] = cpu
    with tempfile.TemporaryDirectory(prefix="cosmic-core-perf-") as temporary:
        binaries = {}
        for name, include in (("baseline", args.baseline_include), ("candidate", candidate)):
            binary = Path(temporary) / name
            subprocess.run([args.compiler, *output["compile_flags"], "-I", str(include),
                            str(root / "native/tests/core_performance_benchmark.cpp"), "-o", str(binary)],
                           check=True)
            binaries[name] = binary
        validation = {name: json.loads(command([binary, "--validate"])) for name, binary in binaries.items()}
        if validation["baseline"] != validation["candidate"]:
            raise RuntimeError(f"State/measurement validation differs: {validation}")
        output["same_circuit_validation"] = validation["candidate"]
        print("Identical baseline/candidate full-state validation:", validation["candidate"], flush=True)
        for kind in args.cases:
            for n in args.qubits:
                count = iterations(kind, n)
                samples = {"baseline": [], "candidate": []}
                fingerprint = None
                for index in range(args.samples):
                    order = ("baseline", "candidate") if index % 2 == 0 else ("candidate", "baseline")
                    for name in order:
                        measured = json.loads(command([binaries[name], kind, n, count]))
                        actual = {key: value for key, value in measured.items() if key != "elapsed_ns"}
                        if fingerprint is not None and fingerprint != actual:
                            raise RuntimeError(f"Different results for {kind}/{n}: {actual} versus {fingerprint}")
                        fingerprint = actual
                        samples[name].append(measured["elapsed_ns"] / count / 1000.0)
                record = dict(fingerprint)
                record["unit"] = "microseconds per operation; a measurement round measures every qubit"
                record["samples_us_per_operation"] = samples
                record["median_us_per_operation"] = {name: statistics.median(values) for name, values in samples.items()}
                record["speedup_ratio"] = record["median_us_per_operation"]["baseline"] / record["median_us_per_operation"]["candidate"]
                output["cases"].append(record)
                print(f"{kind:20} {n:3} q: {record['median_us_per_operation']['baseline']:.3f} -> "
                      f"{record['median_us_per_operation']['candidate']:.3f} us; "
                      f"{record['speedup_ratio']:.2f}x", flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n")
    print(f"Saved {len(output['cases'])} measured cases to {args.output}", flush=True)


if __name__ == "__main__":
    main()
