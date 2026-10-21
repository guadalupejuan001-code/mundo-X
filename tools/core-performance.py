#!/usr/bin/env python3
"""Compare identical native workloads against frozen and candidate headers.

Example:
  python3 tools/core-performance.py --baseline-include /tmp/old/native/include \
    --baseline-id 403bdd7311e6d6b37e849fbe2c85375a9a2cd2ac \
    --output results/core_performance_2026-10-19.json

No external simulator is used. These are host CPU component measurements,
not Android FPS, GPU timings, 8K/12K frame rates or an end-to-end app speedup.

Workload/checksum/core payload must match exactly. The observer's payload is
stable per version; candidate growth is capped at the 2*n*sizeof(size_t) index
introduced by 00e2f01 (pivot_rows_), and both sizes and their delta are retained.
Linux native invocations have a 2 GiB RLIMIT_AS and a finite deadline. This
bounds virtual address space; payload excludes allocator overhead and is not RSS.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import tempfile
from datetime import datetime, timezone


NATIVE_ADDRESS_SPACE_BYTES = 2 * 1024 ** 3
NATIVE_TIMEOUT_SECONDS = 120.0
PIVOT_INDEX_COMMIT = "00e2f014186d9db9d2d87b3f7aabee3338d2b2a5"
OBSERVER_CASES = {"observer_build", "graphics_revision", "cached_query"}


def command(argv):
    return subprocess.check_output([str(x) for x in argv], text=True, timeout=120)


def effective_native_limit():
    if sys.platform != "linux":
        raise RuntimeError("This bounded host driver requires Linux RLIMIT_AS")
    import resource
    inherited = resource.getrlimit(resource.RLIMIT_AS)
    return min([NATIVE_ADDRESS_SPACE_BYTES] + [limit for limit in inherited if limit != resource.RLIM_INFINITY])


def native_command(argv, timeout_seconds, address_space_bytes):
    """Run one native process with a finite deadline and address-space limit."""
    import resource
    if not math.isfinite(timeout_seconds) or timeout_seconds <= 0:
        raise ValueError("A finite positive native deadline is required")
    if not 0 < address_space_bytes <= NATIVE_ADDRESS_SPACE_BYTES:
        raise ValueError("Native address space must not exceed 2 GiB")

    def limit_address_space():
        resource.setrlimit(resource.RLIMIT_AS, (address_space_bytes, address_space_bytes))

    result = subprocess.run([str(x) for x in argv], text=True, capture_output=True,
                            timeout=timeout_seconds, preexec_fn=limit_address_space, check=False)
    if result.returncode:
        raise RuntimeError(f"Native process failed ({result.returncode}): {' '.join(map(str, argv))}\n"
                           + result.stderr.strip())
    return result.stdout


def fingerprint(measured, kind, qubits, count):
    if any(measured.get(key) != value for key, value in
           (("case", kind), ("qubits", qubits), ("iterations", count))):
        raise RuntimeError("Native benchmark changed the requested workload")
    for key in ("elapsed_ns", "core_payload_bytes", "observer_payload_bytes"):
        if type(measured.get(key)) is not int or measured[key] < (1 if key == "elapsed_ns" else 0):
            raise RuntimeError(f"Invalid native measurement: {key}")
    if not isinstance(measured.get("checksum"), str) or not measured["checksum"]:
        raise RuntimeError("Native checksum is missing")
    # Keep core payload, workload and checksum exact. Observer storage is a
    # versioned resource measurement, checked separately against its known index.
    return {key: value for key, value in measured.items() if key not in {"elapsed_ns", "observer_payload_bytes"}}


def require_same_fingerprint(expected, actual, label):
    if expected is not None and expected != actual:
        raise RuntimeError(f"Different results for {label}: {actual} versus {expected}")


def observer_payload_record(samples, kind, qubits, size_t_bytes):
    values = {}
    for version in ("baseline", "candidate"):
        measured = samples[version]
        if not measured or any(type(value) is not int or value < 0 for value in measured):
            raise RuntimeError(f"Invalid observer payload samples: {version}")
        if len(set(measured)) != 1:
            raise RuntimeError(f"Observer payload changed between identical {version} repetitions")
        values[version] = measured[0]
    if len(samples["baseline"]) != len(samples["candidate"]):
        raise RuntimeError("Observer versions did not receive the same sample count")
    # 00e2f01 adds pivot_rows_.assign(2*n_, n_): one size_t per X/Z column.
    # This is a cap on the measured payload increase, not an exemption from
    # memory checking and not a claim about allocator overhead or process RSS.
    budget = 2 * qubits * size_t_bytes if kind in OBSERVER_CASES else 0
    if kind not in OBSERVER_CASES and any(values.values()):
        raise RuntimeError("A case without an observer reported observer storage")
    delta = values["candidate"] - values["baseline"]
    if delta > budget:
        raise RuntimeError(f"Observer payload growth {delta} exceeds pivot_rows_ budget {budget}")
    return {
        # Preserve the schema-1 numeric field for readers of previous results.
        # It reports the candidate; the additive map makes both versions explicit.
        "observer_payload_bytes": values["candidate"],
        "observer_payload_bytes_by_version": values,
        "observer_payload_samples_bytes": samples,
        "observer_payload_delta_bytes": delta,
        "observer_payload_growth_budget_bytes": budget,
        "observer_payload_ratio": values["candidate"] / values["baseline"] if values["baseline"] else None,
        "observer_payload_stable_by_version": True,
    }


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
    parser.add_argument("--native-timeout-seconds", type=float, default=NATIVE_TIMEOUT_SECONDS)
    parser.add_argument("--qubits", type=int, nargs="+", default=[20, 64, 128, 256])
    parser.add_argument("--cases", nargs="+", default=[
        "observer_build", "cz_gate", "measurement_round", "graphics_revision", "cached_query"])
    args = parser.parse_args()
    if args.samples < 3:
        parser.error("Use at least three independent samples")
    if not math.isfinite(args.native_timeout_seconds) or args.native_timeout_seconds <= 0:
        parser.error("Use a finite positive native deadline")
    if any(n < 2 or n > 256 for n in args.qubits):
        parser.error("Native benchmark workloads require 2 through 256 qubits")
    native_limit = effective_native_limit()
    root = Path(__file__).resolve().parents[1]
    candidate = root / "native/include"
    headers = ["mundo_x/compact_core.hpp", "mundo_x/pauli_observer.hpp", "mundo_x/engine_session.hpp"]
    output = {
        "schema": 1,
        "status": "running",
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
        "native_process_budget": {
            "requested_address_space_bytes": NATIVE_ADDRESS_SPACE_BYTES,
            "effective_address_space_bytes": native_limit,
            "deadline_seconds_per_invocation": args.native_timeout_seconds,
            "scope": "Linux RLIMIT_AS on ABI probe, full validation and every baseline/candidate sample; compiler excluded; RSS not measured",
        },
        "observer_payload_policy": {
            "representation_commit": PIVOT_INDEX_COMMIT,
            "growth_limit": "2 * qubits * sizeof(size_t), the pivot_rows_ X/Z column index; zero for cases without an observer",
            "scope": "Logical payload only; excludes vector objects, spare capacities and allocator overhead",
            "core_payload_must_remain_identical": True,
            "legacy_observer_payload_bytes_version": "candidate",
        },
        "cases": [],
    }
    if hasattr(os, "sched_getaffinity"):
        cpu = min(os.sched_getaffinity(0))
        os.sched_setaffinity(0, {cpu})
        output["pinned_cpu"] = cpu
    def persist():
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(output, indent=2) + "\n")

    persist()
    try:
        with tempfile.TemporaryDirectory(prefix="cosmic-core-perf-") as temporary:
            # Query the actual C++ compiler ABI; do not assume Python's pointer width.
            abi_source = Path(temporary) / "abi.cpp"
            abi_source.write_text("#include <cstddef>\n#include <iostream>\nint main(){std::cout << sizeof(std::size_t);}\n")
            abi_binary = Path(temporary) / "abi"
            subprocess.run([args.compiler, *output["compile_flags"], str(abi_source), "-o", str(abi_binary)],
                           check=True, timeout=120)
            size_t_bytes = int(native_command([abi_binary], args.native_timeout_seconds, native_limit))
            if size_t_bytes not in (4, 8):
                raise RuntimeError(f"Unsupported benchmark size_t width: {size_t_bytes}")
            output["observer_payload_policy"]["compiler_size_t_bytes"] = size_t_bytes
            binaries = {}
            for name, include in (("baseline", args.baseline_include), ("candidate", candidate)):
                binary = Path(temporary) / name
                subprocess.run([args.compiler, *output["compile_flags"], "-I", str(include),
                                str(root / "native/tests/core_performance_benchmark.cpp"), "-o", str(binary)],
                               check=True, timeout=120)
                binaries[name] = binary
            validation = {name: json.loads(native_command([binary, "--validate"], args.native_timeout_seconds, native_limit))
                          for name, binary in binaries.items()}
            output["validation_by_version"] = validation
            if validation["baseline"] != validation["candidate"]:
                raise RuntimeError(f"State/measurement validation differs: {validation}")
            output["same_circuit_validation"] = validation["candidate"]
            persist()
            print("Identical baseline/candidate full-state validation:", validation["candidate"], flush=True)
            for kind in args.cases:
                for n in args.qubits:
                    count = iterations(kind, n)
                    samples = {"baseline": [], "candidate": []}
                    payloads = {"baseline": [], "candidate": []}
                    output["active_payload_samples_bytes"] = payloads
                    expected = None
                    for index in range(args.samples):
                        order = ("baseline", "candidate") if index % 2 == 0 else ("candidate", "baseline")
                        for name in order:
                            output["active_sample"] = {"case": kind, "qubits": n, "iterations": count,
                                                       "version": name, "sample": index + 1}
                            measured = json.loads(native_command([binaries[name], kind, n, count],
                                                                 args.native_timeout_seconds, native_limit))
                            output["active_sample"]["measurement"] = measured
                            actual = fingerprint(measured, kind, n, count)
                            require_same_fingerprint(expected, actual, f"{kind}/{n}")
                            expected = actual
                            payloads[name].append(measured["observer_payload_bytes"])
                            samples[name].append(measured["elapsed_ns"] / count / 1000.0)
                    record = dict(expected)
                    record.update(observer_payload_record(payloads, kind, n, size_t_bytes))
                    record["unit"] = "microseconds per operation; a measurement round measures every qubit"
                    record["samples_us_per_operation"] = samples
                    record["median_us_per_operation"] = {name: statistics.median(values) for name, values in samples.items()}
                    record["speedup_ratio"] = record["median_us_per_operation"]["baseline"] / record["median_us_per_operation"]["candidate"]
                    output["cases"].append(record)
                    persist()
                    print(f"{kind:20} {n:3} q: {record['median_us_per_operation']['baseline']:.3f} -> "
                          f"{record['median_us_per_operation']['candidate']:.3f} us; "
                          f"{record['speedup_ratio']:.2f}x; observer payload "
                          f"{record['observer_payload_bytes_by_version']['baseline']} -> {record['observer_payload_bytes_by_version']['candidate']} bytes "
                          f"(delta {record['observer_payload_delta_bytes']:+}, growth budget {record['observer_payload_growth_budget_bytes']})",
                          flush=True)
        output.pop("active_sample", None)
        output.pop("active_payload_samples_bytes", None)
        output["status"] = "passed"
    except Exception as error:
        output["status"] = "failed"
        output["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        persist()
    print(f"Saved {len(output['cases'])} measured cases to {args.output}", flush=True)


if __name__ == "__main__":
    main()
