#!/usr/bin/env python3
"""Bounded, isolated exams for the experimental factorized engine.

Each method and circuit runs in one fresh C++ process. Timers separate engine
initialization, evolution (including requested factorization), and local queries.
Amplitude payload is not RAM: only /proc/<pid>/status VmHWM measures process RSS.
Previous result directories are never overwritten. No third-party Python modules.
"""

import argparse
import csv
import datetime as dt
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import shlex
import subprocess
import sys
import tempfile
import time


ROOT = Path(tempfile.gettempdir()) / "mundox-factorized-exams"
MAX_MEMORY = 1024 * 1024 * 1024
FAMILIES = ("product_ht", "product_t2", "bell_pairs", "ghz", "bridge_uncompute")
FACTOR_METHODS = {"factorized"}
RESOURCE_STOPS = {"timeout", "cpu_budget", "allocation_failure", "memory_budget"}
FAILURES = {"correctness_failure", "runtime_failure", "invalid_argument", "signal_failure", "unexpected_budget_rejection"}


def dump(path, obj):
    path.write_text(json.dumps(obj, indent=2, sort_keys=True, allow_nan=False) + "\n")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def git(repo, *args):
    proc = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True)
    return proc.stdout.strip() if proc.returncode == 0 else None


def lock(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    handle = open(path, "a+")
    fcntl.flock(handle, fcntl.LOCK_EX)
    return handle


def source_manifest(repo):
    files = [repo / "native/tests/factorized_benchmark.cpp",
             repo / "tools/stress_factorized.py",
             *sorted((repo / "native/include/mundo_x").glob("*.hpp"))]
    return {str(p.relative_to(repo)): sha256(p) for p in files}


def proc_hwm(pid):
    """No ru_maxrss: fork/exec can inherit the Python parent's high water mark."""
    try:
        for line in Path(f"/proc/{pid}/status").read_text().splitlines():
            if line.startswith("VmHWM:"):
                return int(line.split()[1]) * 1024
    except (FileNotFoundError, ProcessLookupError, PermissionError):
        pass
    return 0


def build(args):
    directory = args.output_dir / "build"
    directory.mkdir()
    binary = directory / "factorized_benchmark"
    before = source_manifest(args.repo)
    command = [args.compiler, "-std=c++17", "-O3", "-DNDEBUG", "-Wall", "-Wextra", "-Wpedantic",
               "-I", str(args.repo / "native/include"),
               str(args.repo / "native/tests/factorized_benchmark.cpp"), "-o", str(binary)]
    with lock(args.compute_lock):
        result = subprocess.run(command, text=True, capture_output=True, timeout=120)
    (directory / "compile.stdout.txt").write_text(result.stdout)
    (directory / "compile.stderr.txt").write_text(result.stderr)
    after = source_manifest(args.repo)
    info = {"command": command, "command_shell": shlex.join(command),
            "returncode": result.returncode, "source_sha256": before,
            "source_unchanged_during_compile": before == after,
            "binary_sha256": sha256(binary) if binary.exists() else None,
            "git_head": git(args.repo, "rev-parse", "HEAD"),
            "git_status": git(args.repo, "status", "--short")}
    dump(directory / "build.json", info)
    if result.returncode:
        raise RuntimeError(f"build failed: {directory / 'compile.stderr.txt'}")
    if before != after:
        raise RuntimeError("sources changed during build; rerun after the header is frozen")
    return binary, info


def run_worker(command, args, name):
    stdout_path = args.output_dir / "workers" / f"{name}.stdout.json"
    stderr_path = args.output_dir / "workers" / f"{name}.stderr.jsonl"
    observed = 0
    timed_out = False
    with lock(args.compute_lock), open(stdout_path, "w+") as out, open(stderr_path, "w+") as err:
        start = time.monotonic()
        # Limits and CPU affinity are applied after exec in C++. No preexec_fn.
        proc = subprocess.Popen(command, stdout=out, stderr=err)
        while proc.poll() is None:
            observed = max(observed, proc_hwm(proc.pid))
            if time.monotonic() - start >= args.timeout:
                timed_out = True
                proc.kill()  # Only this worker PID; unrelated work is untouched.
                break
            time.sleep(0.01)
        code = proc.wait()
        wall_ms = (time.monotonic() - start) * 1000
        out.seek(0)
        raw_out = out.read()
        err.seek(0)
        raw_err = err.read()
    try:
        worker = json.loads(raw_out)
        if not isinstance(worker, dict):
            raise ValueError("worker output is not an object")
    except (ValueError, json.JSONDecodeError):
        worker = {}
    status = worker.get("status", "runtime_failure")
    if timed_out:
        status = "timeout"
    elif code == -24:
        status = "cpu_budget"
    elif code < 0:
        status = "signal_failure"
    elif code != 0 and status in {"ok", "expected_budget_rejection"}:
        status = "runtime_failure"
    progress = []
    for line in raw_err.splitlines():
        try:
            event = json.loads(line)
            if isinstance(event, dict) and "phase" in event:
                progress.append(event)
        except (ValueError, json.JSONDecodeError):
            pass
    worker_peak = worker.get("peak_rss_bytes", 0)
    return {**worker, "status": status, "returncode": code,
            "process_wall_ms": wall_ms, "process_peak_rss_bytes": max(worker_peak, observed),
            "rss_source": "linux_worker_proc_self_VmHWM" if worker_peak else "proc_poll_VmHWM_lower_bound",
            "observed_peak_rss_lower_bound_bytes": observed,
            "progress": progress, "last_completed_phase": progress[-1]["phase"] if progress else None,
            "command": command, "command_shell": shlex.join(command),
            "stdout_file": str(stdout_path), "stderr_file": str(stderr_path),
            "stdout": raw_out, "stderr": raw_err}


def write_csv(path, rows):
    fields = ["family", "method", "qubits", "repeat", "seed", "status", "max_block_qubits",
              "gate_count", "executed_gate_count", "circuit_fnv1a64", "initialization_ms",
              "evolution_ms", "refactorization_ms", "query_ms", "query_status", "process_wall_ms",
              "process_peak_rss_bytes", "rss_source", "amplitude_count", "logical_amplitude_bytes",
              "amplitude_payload_kind", "metadata_payload_bytes", "factor_count", "largest_factor_qubits",
              "component_count", "validation", "max_query_error", "budget_state_preserved",
              "max_log_magnitude_error", "max_log_phase_error", "raw_zero_amplitude_underflow", "raw_one_amplitude_underflow",
              "budget_preservation_scope", "scenario", "returncode", "reason", "command_shell", "stdout_file", "stderr_file"]
    with open(path, "w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def numbers(raw):
    try:
        values = sorted(set(int(x) for x in raw.split(",")))
    except ValueError as exc:
        raise argparse.ArgumentTypeError("expected comma separated integers") from exc
    if not values or values[0] < 1:
        raise argparse.ArgumentTypeError("expected positive integers")
    return values


def parse_args():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = p.add_mutually_exclusive_group()
    mode.add_argument("--smoke", action="store_true", help="small API/correctness probes only")
    mode.add_argument("--full", action="store_true", help="products to100000, Bell pairs to10000, GHZ2..24")
    p.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument("--output-dir", type=Path, default=ROOT / "scale" / dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ"))
    p.add_argument("--compute-lock", type=Path, default=ROOT / "compute.lock")
    p.add_argument("--compiler", default="g++")
    p.add_argument("--compile-only", action="store_true")
    p.add_argument("--families", default=",".join(FAMILIES))
    p.add_argument("--methods", default="factorized,reduced,dense",
                   help="comma separated: factorized,reduced,dense")
    p.add_argument("--product-qubits", type=numbers)
    p.add_argument("--bell-qubits", type=numbers)
    p.add_argument("--ghz-qubits", type=numbers)
    p.add_argument("--comparison-max-qubits", type=int, default=22)
    p.add_argument("--max-block-qubits", type=int, default=20)
    p.add_argument("--logical-amplitude-mib", type=int, default=256)
    p.add_argument("--memory-mib", type=int, default=1024)
    p.add_argument("--timeout", type=float, default=15)
    p.add_argument("--repeats", type=int, default=1)
    p.add_argument("--seed", type=int, default=20261017)
    p.add_argument("--skip-budget-probe", action="store_true")
    p.add_argument("--skip-tight-budget-probe", action="store_true",
                   help="skip the320B Factorized bridge and explicit-cut check")
    a = p.parse_args()
    a.families = a.families.split(",")
    a.methods = a.methods.split(",")
    if any(x not in FAMILIES for x in a.families) or any(x not in FACTOR_METHODS | {"reduced", "dense"} for x in a.methods):
        p.error("unknown family or method")
    if not 0 < a.timeout <= 15 or not 64 <= a.memory_mib <= 1024:
        p.error("timeout must be in(0,15] seconds and memory in[64,1024]MiB")
    if not 1 <= a.repeats <= 3 or not 1 <= a.max_block_qubits <= 24:
        p.error("repeats must be1..3 and max-block-qubits1..24")
    if not 1 <= a.logical_amplitude_mib <= 512 or not 1 <= a.comparison_max_qubits <= 24:
        p.error("amplitude budget must be1..512MiB; comparison max1..24")
    if not 0 <= a.seed < 2**64:
        p.error("seed must fit uint64")
    a.repo = a.repo.resolve()
    a.output_dir = a.output_dir.resolve()
    a.compute_lock = a.compute_lock.resolve()
    a.memory_bytes = a.memory_mib * 1024**2
    a.logical_amplitude_bytes = a.logical_amplitude_mib * 1024**2
    return a


def main():
    args = parse_args()
    if args.output_dir.exists() and any(args.output_dir.iterdir()):
        raise RuntimeError("output directory must be empty; existing evidence is preserved")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "workers").mkdir()
    compiler = subprocess.run([args.compiler, "--version"], capture_output=True, text=True, check=True).stdout
    metadata = {"schema_version": 1, "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
                "argv": sys.argv, "platform": platform.platform(), "compiler": compiler,
                "settings": {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
                "notes": ["Classical simulation; no quantum hardware or general quantum advantage is claimed.",
                          "One fresh worker, one allowed CPU, and exclusive shared compute lock per case.",
                          "15s wall ceiling and1GiB address-space ceiling are enforced for workers only.",
                          "Amplitude/complex-coefficient payload excludes all metadata, capacity and allocator costs.",
                          "Metadata payload is null unless measured; process RSS includes the entire worker.",
                          "Linux VmHWM is used, never inherited Python ru_maxrss.",
                          "Circuit hashes identify requested gates; rejected workers also report executed count.",
                          "Reduced has no native local-query API; dense reconstruction is intentionally omitted.",
                          "Evolution includes requested refactorization; its time is also reported separately.",
                          "The separate bridge_tight_budget scenario checks Factorized with320B amplitude budget and an explicit cut.",
                          "Defaults keep the256MiB amplitude budget; bridge factorization is explicitly requested.",
                          "Large product raw amplitudes can underflow; independent analytic log-magnitude and phase checks remain active.",
                          "Resource frontiers and intentional budget rejection are observations, not wrong answers."]}
    dump(args.output_dir / "metadata.json", metadata)
    binary, build_info = build(args)
    print(f"built {binary}", flush=True)
    if args.compile_only:
        return 0

    smoke = args.smoke or not args.full
    products = args.product_qubits or ([2, 4, 8] if smoke else [2, 4, 8, 12, 16, 22, 100, 1000, 10000, 100000])
    bells = args.bell_qubits or ([2, 4, 8] if smoke else [2, 4, 8, 16, 22, 100, 1000, 10000])
    ghzs = args.ghz_qubits or ([2, 4, 8] if smoke else list(range(2, 25)))
    cases = {"product_ht": products, "product_t2": products, "bell_pairs": bells,
             "ghz": ghzs, "bridge_uncompute": [4]}
    rows = []
    stopped = {}
    fingerprints = {}
    events = open(args.output_dir / "results.jsonl", "a", buffering=1)

    def emit(row):
        rows.append(row)
        events.write(json.dumps(row, sort_keys=True, allow_nan=False) + "\n")
        write_csv(args.output_dir / "results.csv", rows)
        detail = ""
        if "evolution_ms" in row:
            detail = (f" evolution={row['evolution_ms']:.3f}ms"
                      f" RSS={row['process_peak_rss_bytes']/1024**2:.2f}MiB"
                      f" factors={row.get('factor_count')} largest={row.get('largest_factor_qubits')}")
        print(f"{row['family']} n={row['qubits']} {row['method']} r={row['repeat']}: {row['status']}{detail}", flush=True)

    def case(family, n, method, repeat=1, budget_probe=False, tight_budget_probe=False):
        block_limit = 3 if budget_probe else args.max_block_qubits
        amplitude_budget = 320 if tight_budget_probe else args.logical_amplitude_bytes
        expected_reject = method in FACTOR_METHODS and (budget_probe or family == "ghz" and n > block_limit)
        base = {"family": family, "qubits": n, "method": method, "repeat": repeat,
                "seed": args.seed, "max_block_qubits": block_limit,
                "scenario": "budget_rejection" if budget_probe else "bridge_tight_budget" if tight_budget_probe else "default",
                "logical_amplitude_budget_bytes": amplitude_budget,
                "intentional_budget_probe": budget_probe, "expected_budget_rejection": expected_reject}
        key = (family, method)
        if key in stopped and not budget_probe:
            emit({**base, "status": "adaptive_skipped", "reason": stopped[key]})
            return
        payload = (1 << n) * 16 if method == "dense" and n < 63 else 0
        if payload > args.memory_bytes - 128 * 1024**2:
            stopped[key] = "minimum dense amplitude payload exceeds address-space limit minus128MiB"
            emit({**base, "status": "memory_budget", "reason": stopped[key], "predicted_payload_bytes": payload})
            return
        command = [str(binary), "--method", method, "--family", family, "--qubits", str(n),
                   "--seed", str(args.seed), "--max-block-qubits", str(block_limit),
                   "--logical-amplitude-bytes", str(amplitude_budget),
                   "--expect-factor-count", "2" if tight_budget_probe else "0",
                   "--expect-budget-rejection", "1" if expected_reject else "0",
                   "--memory-bytes", str(args.memory_bytes), "--cpu-seconds", str(math.ceil(args.timeout))]
        name = f"{len(rows)+1:04d}-{family}-{method}-n{n}-r{repeat}" + ("-budget" if budget_probe else "")
        result = run_worker(command, args, name)
        fp = result.get("circuit_fnv1a64")
        if fp:
            fkey = (family, n, args.seed)
            if fkey in fingerprints and fingerprints[fkey] != fp:
                result["status"] = "correctness_failure"
                result["reason"] = "requested gate hash differs between methods"
            fingerprints[fkey] = fp
        if result["status"] in RESOURCE_STOPS and not budget_probe:
            stopped[key] = f"n={n} reached {result['status']} within declared budget"
        emit({**base, **result})

    for family in args.families:
        for n in cases[family]:
            for method in args.methods:
                if method not in FACTOR_METHODS and n > args.comparison_max_qubits:
                    continue
                repeats = 1 if smoke or n >= 22 else args.repeats
                for repeat in range(1, repeats + 1):
                    case(family, n, method, repeat)
    if not args.skip_budget_probe:
        for method in args.methods:
            if method in FACTOR_METHODS:
                case("bridge_uncompute", 4, method, budget_probe=True)
    if not args.skip_tight_budget_probe:
        for method in args.methods:
            if method in FACTOR_METHODS:
                case("bridge_uncompute", 4, method, tight_budget_probe=True)
    events.close()
    after = source_manifest(args.repo)
    counts = {}
    for row in rows:
        counts[row["status"]] = counts.get(row["status"], 0) + 1
    summary = {"case_count": len(rows), "status_counts": counts,
               "source_unchanged_during_suite": build_info["source_sha256"] == after,
               "source_sha256_at_end": after,
               "resource_frontiers": [{"family": k[0], "method": k[1], "reason": v} for k, v in stopped.items()],
               "resource_limits": {"worker_address_space_bytes": args.memory_bytes,
                                   "worker_wall_seconds": args.timeout, "allowed_cpus_per_worker": 1},
               "output_csv": str(args.output_dir / "results.csv"), "output_jsonl": str(args.output_dir / "results.jsonl")}
    dump(args.output_dir / "summary.json", summary)
    print(json.dumps(summary, sort_keys=True), flush=True)
    return int(any(r["status"] in FAILURES for r in rows) or not summary["source_unchanged_during_suite"])


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, subprocess.SubprocessError) as exc:
        print(f"stress_factorized: {exc}", file=sys.stderr)
        sys.exit(2)
