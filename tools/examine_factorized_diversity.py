#!/usr/bin/env python3
"""Native FactorizedCore diversity exams; Python and C++ standard libraries only.

No external simulator, NumPy, SDK, network or installed package is used. The
compiler builds repository code and a generic long-double 2x2 reference.
"""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import resource
import shutil
import subprocess
import sys
import tempfile
import time

REPO = Path(__file__).resolve().parents[1]
EXAM_ROOT = Path(tempfile.gettempdir()) / "mundox-native-diversity-exams"
MAX_BYTES = 2 * 1024**3
RESERVE_BYTES = 2 * 1024**3


def dump(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n")


def fingerprint(path):
    path = Path(path).resolve()
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024*1024), b""):
            digest.update(block)
    return dict(path=str(path), sha256=digest.hexdigest(), bytes=path.stat().st_size)


def source_manifest():
    paths = [REPO / "native/tests/factorized_diversity.cpp", Path(__file__).resolve(),
             *sorted((REPO / "native/include/mundo_x").glob("*.hpp"))]
    return {str(path.relative_to(REPO)): fingerprint(path) for path in paths}


def child_limits():
    resource.setrlimit(resource.RLIMIT_AS, (MAX_BYTES, MAX_BYTES))
    resource.setrlimit(resource.RLIMIT_CPU, (45, 45))
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    if hasattr(os, "sched_getaffinity"):
        os.sched_setaffinity(0, {min(os.sched_getaffinity(0))})


def proc_hwm(pid):
    try:
        for line in Path(f"/proc/{pid}/status").read_text().splitlines():
            if line.startswith("VmHWM:"):
                return int(line.split()[1]) * 1024
    except (FileNotFoundError, PermissionError, ProcessLookupError):
        pass
    return 0


def cgroup_memory():
    root = Path("/sys/fs/cgroup")
    result = {}
    for field in ("memory.current", "memory.max", "memory.events"):
        path = root / field
        result[field] = path.read_text().strip() if path.exists() else None
    return result


def free_headroom(snapshot):
    if snapshot["memory.max"] in (None, "max") or snapshot["memory.current"] is None:
        raise RuntimeError("this supervisor requires a finite Linux cgroup memory ceiling")
    return int(snapshot["memory.max"])-int(snapshot["memory.current"])


def acquire(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    handle = path.open("a")
    fcntl.flock(handle, fcntl.LOCK_EX)
    return handle


def run_worker(binary, n, depth, args):
    label = f"n{n}-depth{depth}-seed{args.seed}"
    command = [str(binary), "--n", str(n), "--depth", str(depth), "--seed", str(args.seed),
               "--max-bytes", str(MAX_BYTES), "--cpu-seconds", "45"]
    output = args.out / f"{label}.stdout.json"
    error = args.out / f"{label}.stderr.jsonl"
    observed = 0
    timed_out = False
    headroom_abort = False
    max_cgroup_current = 0
    with acquire(args.compute_lock), output.open("w+") as stdout, error.open("w+") as stderr:
        before = cgroup_memory()
        if free_headroom(before) < MAX_BYTES+RESERVE_BYTES:
            worker = dict(status="memory_headroom_skip", n=n, depth=depth, seed=args.seed,
                          command=command, cgroup_before=before,
                          reason="free cgroup headroom below worker AS ceiling plus 2 GiB reserve")
            dump(args.out / f"{label}.result.json", worker)
            return worker
        started = time.monotonic()
        process = subprocess.Popen(command, stdout=stdout, stderr=stderr)
        while process.poll() is None:
            observed = max(observed, proc_hwm(process.pid))
            current = cgroup_memory()
            max_cgroup_current = max(max_cgroup_current, int(current["memory.current"]))
            if free_headroom(current) < RESERVE_BYTES:
                headroom_abort = True
                process.kill()  # Only this fresh worker, never unrelated processes.
                break
            if time.monotonic()-started > 45:
                timed_out = True
                process.kill()
                break
            time.sleep(0.01)
        code = process.wait()
        wall_ms = (time.monotonic()-started)*1000
        stdout.seek(0)
        raw = stdout.read()
    try:
        worker = json.loads(raw)
    except (ValueError, json.JSONDecodeError):
        worker = {}
    status = worker.get("status", "runtime_failure")
    if headroom_abort:
        status = "memory_headroom_abort"
    elif timed_out:
        status = "wall_timeout"
    elif code < 0:
        status = "cpu_budget" if code == -24 else "signal_failure"
    elif code != 0 and status == "ok":
        status = "runtime_failure"
    worker.update(status=status, command=command, returncode=code,
                  supervisor_wall_ms=wall_ms,
                  peak_rss_bytes_observed_fresh_pid=observed,
                  observed_peak_cgroup_current_bytes=max_cgroup_current,
                  cgroup_before=before, cgroup_after=cgroup_memory())
    dump(args.out / f"{label}.result.json", worker)
    return worker


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sizes", default="128", help="Comma-separated counts, up to 1000000.")
    parser.add_argument("--depths", default="32", help="32, 64, or 32,64.")
    parser.add_argument("--seed", type=lambda value: int(value, 0), default=0x4D554E444F585F44)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--compute-lock", type=Path, default=EXAM_ROOT / "compute.lock")
    timestamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    parser.add_argument("--out", type=Path, default=EXAM_ROOT / timestamp)
    args = parser.parse_args()
    try:
        sizes = [int(value) for value in args.sizes.split(",")]
        depths = [int(value) for value in args.depths.split(",")]
    except ValueError:
        parser.error("sizes/depths must be comma-separated integers")
    if not sizes or any(n < 1 or n > 1000000 for n in sizes) or any(d not in (32, 64) for d in depths):
        parser.error("n must be 1..1000000 and depths must be 32 or 64")
    if args.seed < 0 or args.seed > 2**64-1 or len(set(sizes)) != len(sizes) or len(set(depths)) != len(depths):
        parser.error("seed must fit uint64 and sizes/depths cannot repeat")
    args.out = args.out.resolve()
    args.compute_lock = args.compute_lock.resolve()
    if args.out.exists() and (not args.out.is_dir() or any(args.out.iterdir())):
        parser.error(f"preserving existing evidence; --out must be empty or absent: {args.out}")
    args.out.mkdir(parents=True, exist_ok=True)
    binary = args.out / "factorized-diversity"
    compiler = shutil.which(args.compiler)
    if compiler is None:
        parser.error(f"compiler not found: {args.compiler}")
    command = [compiler, "-std=c++17", "-O3", "-DNDEBUG", "-Wall", "-Wextra", "-Wpedantic",
               "-I", str(REPO / "native/include"), str(REPO / "native/tests/factorized_diversity.cpp"),
               "-o", str(binary)]
    manifest = source_manifest()
    with acquire(args.compute_lock):
        initial_memory = cgroup_memory()
        if free_headroom(initial_memory) < MAX_BYTES+RESERVE_BYTES:
            dump(args.out / "build-headroom-skip.json", dict(status="memory_headroom_skip", cgroup=initial_memory))
            raise SystemExit("not enough cgroup headroom for compiler AS ceiling plus reserve")
        build = subprocess.run(command, text=True, capture_output=True, timeout=45, preexec_fn=child_limits)
    (args.out / "compile.stdout.txt").write_text(build.stdout)
    (args.out / "compile.stderr.txt").write_text(build.stderr)
    metadata = dict(created_at_utc=dt.datetime.now(dt.timezone.utc).isoformat(),
                    compiler_command=command, compiler=fingerprint(compiler),
                    compiler_returncode=build.returncode, sources=manifest,
                    source_unchanged_during_compile=manifest == source_manifest(),
                    binary=fingerprint(binary) if binary.exists() else None,
                    python_version=sys.version, python_executable=sys.executable,
                    external_simulators_or_packages_used=False,
                    cases=[dict(n=n, depth=d, seed=args.seed) for n in sizes for d in depths],
                    preregistered_criteria=dict(local_probability_abs_error=3e-11,
                        global_log_magnitude_and_circular_phase_abs_error=1e-7,
                        legacy_global_tolerance_reported_separately=1e-8,
                        global_tolerance_is_heuristic_not_an_error_bound=True,
                        signature_samples_at_most=4096, distinct_gate_trace_hash_fraction_at_least=0.95,
                        gate_trace_diversity_is_not_a_physical_state_uniqueness_claim=True),
                    resources=dict(cpu_affinity_count=1, address_space_bytes=MAX_BYTES,
                                   worker_wall_timeout_seconds=45, worker_cpu_seconds=45,
                                   headroom_reserve_bytes=RESERVE_BYTES,
                                   compute_lock=str(args.compute_lock)),
                    cgroup_before=initial_memory,
                    rss_method="fresh exec worker /proc/<pid>/status VmHWM; never inherited ru_maxrss")
    dump(args.out / "metadata.json", metadata)
    if build.returncode or not metadata["source_unchanged_during_compile"]:
        raise SystemExit("build failed or sources changed; inspect compile.stderr.txt and metadata.json")
    results = [run_worker(binary, n, depth, args) for n in sizes for depth in depths]
    fields = ["n", "depth", "status", "production_gates", "max_local_probability_abs_error",
              "initialization_ms", "evolution_ms", "reference_validation_and_query_selection_ms",
              "global_queries_ms", "peak_rss_bytes_self_proc", "peak_rss_bytes_observed_fresh_pid",
              "distinct_sampled_gate_trace_hashes", "signature_samples", "legacy_global_tolerance_pass"]
    with (args.out / "results.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(results)
    unchanged = manifest == source_manifest()
    summary = dict(passed=unchanged and all(result.get("status") == "ok" for result in results),
                   cases=len(results), total_local_probabilities_checked=sum(result.get("all_local_probabilities_checked", 0) for result in results),
                   global_queries_checked=sum(len(result.get("global_queries", [])) for result in results),
                   source_unchanged_during_suite=unchanged,
                   results=results,
                   cgroup_after=cgroup_memory(),
                   limitations=["Independent product states; this exam does not test entangled-state scaling.",
                                "Distinct sampled gate traces do not establish distinct physical states.",
                                "1e-7 global tolerance was chosen before execution as a heuristic, not a proven bound.",
                                "Supported bitstrings select queries only; no amplitudes are removed or renormalized in production."])
    dump(args.out / "summary.json", summary)
    print(json.dumps({key: value for key, value in summary.items() if key != "results"}, indent=2))
    raise SystemExit(0 if summary["passed"] else 1)


if __name__ == "__main__":
    main()
