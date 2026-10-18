#!/usr/bin/env python3
"""Isolated, reproducible scale probes; only child workers receive resource limits.

Examples:
  python tools/stress_architecture.py --quick --variants baseline --output-dir /tmp/mundox-quick
  python tools/stress_architecture.py --full --baseline-repo /workspace/mundo-X \
      --candidate-repo /workspace/mundo-X-limits --output-dir /tmp/mundox-full

Every method/case/repetition is a fresh process. The measured evolution excludes
allocation, circuit construction, norm checks and all dense reconstruction. Peak
RSS covers the whole worker, including allocation and circuit storage. Logical
payload is separately labelled and is never used as a replacement for process RSS.
Small correctness workers use an independent double statevector. The historical
baseline's public reconstruction replays its stabilizer base in complex<float>;
the precision-fixed candidate uses complex<double>. These separate correctness
workers are not used for timing or memory comparison.
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
import time


DEFAULT_ROOT = Path("/workspace/scratch/mundox-limits-20261017")
MAX_MEMORY = 1536 * 1024 * 1024
FAMILIES = ("product_ht", "product_t2", "random", "cli")
STOP_STATUSES = {"timeout", "cpu_budget", "memory_budget", "allocation_failure", "signal_failure", "runtime_failure", "correctness_failure"}


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as source:
        for part in iter(lambda: source.read(1024 * 1024), b""):
            h.update(part)
    return h.hexdigest()


def git(repo, *args):
    result = subprocess.run(["git", "-C", str(repo), *args], text=True, capture_output=True)
    return result.stdout.strip() if result.returncode == 0 else None


def source_manifest(repo, driver):
    paths = [driver, *sorted((repo / "native/include/mundo_x").glob("*.hpp"))]
    return {str(path.resolve()): sha256(path) for path in paths}


def dump(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n")


def lock_file(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    handle = open(path, "a+")
    fcntl.flock(handle, fcntl.LOCK_EX)
    return handle


def proc_rss(pid):
    try:
        values = {}
        for line in Path(f"/proc/{pid}/status").read_text().splitlines():
            if line.startswith(("VmRSS:", "VmHWM:")):
                key, value, _unit = line.split()
                values[key[:-1]] = int(value) * 1024
        return max(values.values(), default=0)
    except (FileNotFoundError, ProcessLookupError, PermissionError):
        return 0


def run_worker(command, args, output, name):
    stdout_path = output / "workers" / f"{name}.stdout.json"
    stderr_path = output / "workers" / f"{name}.stderr.txt"
    observed_rss = 0
    timed_out = False
    with lock_file(args.compute_lock), open(stdout_path, "w+") as stdout, open(stderr_path, "w+") as stderr:
        # The wall deadline starts only after obtaining the shared compute lock.
        start = time.monotonic()
        # Resource ceilings are installed at worker entry. Avoid preexec_fn:
        # its copied Python pages would inflate the worker's ru_maxrss.
        proc = subprocess.Popen(command, stdout=stdout, stderr=stderr)
        while proc.poll() is None:
            observed_rss = max(observed_rss, proc_rss(proc.pid))
            if time.monotonic() - start >= args.timeout:
                timed_out = True
                proc.kill()  # Our one worker only; no process-group or unrelated process signals.
                break
            time.sleep(0.01)
        returncode = proc.wait()
        process_wall_ms = (time.monotonic() - start) * 1000
        stdout.seek(0)
        raw_stdout = stdout.read()
        stderr.seek(0)
        raw_stderr = stderr.read()
    try:
        worker = json.loads(raw_stdout)
        if not isinstance(worker, dict):
            raise ValueError("worker result is not an object")
    except (ValueError, json.JSONDecodeError):
        worker = {}
    status = worker.get("status", "runtime_failure")
    if timed_out:
        status = "timeout"
    elif returncode == -24:  # Linux SIGXCPU; a declared budget frontier.
        status = "cpu_budget"
    elif returncode < 0:
        status = "signal_failure"
    elif returncode != 0 and status in ("ok", "expected_rejection"):
        status = "runtime_failure"
    measured_peak = max(observed_rss, worker.get("peak_rss_bytes", 0))
    progress = []
    for line in raw_stderr.splitlines():
        try:
            event = json.loads(line)
            if isinstance(event, dict) and "phase" in event:
                progress.append(event)
        except (ValueError, json.JSONDecodeError):
            pass
    return {**worker, "status": status, "returncode": returncode, "pid": proc.pid,
            "progress": progress, "last_completed_phase": progress[-1]["phase"] if progress else None,
            "process_wall_ms": process_wall_ms, "observed_peak_rss_lower_bound_bytes": observed_rss,
            "process_peak_rss_bytes": measured_peak,
            "rss_source": worker.get("peak_rss_source", "worker_rss_unspecified") if worker.get("peak_rss_bytes") else "proc_status_poll_lower_bound",
            "command": command, "command_shell": shlex.join(command), "stdout": raw_stdout,
            "stderr": raw_stderr, "stdout_file": str(stdout_path), "stderr_file": str(stderr_path)}


def build(repo, variant, driver, args):
    directory = args.output_dir / "build" / variant
    directory.mkdir(parents=True, exist_ok=True)
    binary = directory / "stress_driver"
    before = source_manifest(repo, driver)
    command = [args.compiler, "-std=c++17", "-O3", "-DNDEBUG", "-Wall", "-Wextra", "-Wpedantic",
               "-I", str(repo / "native/include"), str(driver), "-o", str(binary)]
    with lock_file(args.compute_lock):
        result = subprocess.run(command, text=True, capture_output=True, timeout=120)
    (directory / "compile.stdout.txt").write_text(result.stdout)
    (directory / "compile.stderr.txt").write_text(result.stderr)
    after = source_manifest(repo, driver)
    info = {"repo": str(repo), "git_head": git(repo, "rev-parse", "HEAD"),
            "git_status": git(repo, "status", "--short"), "command": command,
            "command_shell": shlex.join(command), "source_sha256": before,
            "source_unchanged_during_compile": before == after, "returncode": result.returncode,
            "binary": str(binary), "binary_sha256": sha256(binary) if binary.exists() else None}
    dump(directory / "build.json", info)
    if result.returncode:
        raise RuntimeError(f"{variant}: compiler failed; see {directory / 'compile.stderr.txt'}")
    if before != after:
        raise RuntimeError(f"{variant}: a source changed during compilation; retry when candidate is frozen")
    return binary, info


def csv_write(path, rows):
    keys = ["variant", "family", "method", "qubits", "requested_t_count", "seed", "repeat", "status",
            "gate_count", "t_gate_count", "circuit_fnv1a64", "initialization_ms", "evolution_ms",
            "process_wall_ms", "process_peak_rss_bytes", "rss_source", "declared_payload_bytes",
            "payload_kind", "component_count", "peak_component_count", "validation", "norm", "fidelity",
            "max_amplitude_error", "returncode", "reason", "command_shell", "stdout_file", "stderr_file"]
    with open(path, "w", newline="") as target:
        writer = csv.DictWriter(target, fieldnames=keys, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def comma_numbers(raw):
    values = [int(v) for v in raw.split(",") if v]
    if not values or any(v <= 0 for v in values):
        raise argparse.ArgumentTypeError("expected comma separated positive integers")
    return sorted(set(values))


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--smoke", action="store_true", help="n=2,4 and GHZ8 only, plus boundary63/64")
    mode.add_argument("--quick", action="store_true", help="n=8,12,16; compact GHZ1024,4096")
    mode.add_argument("--full", action="store_true", help="n=8,12,16,...60,63; adaptive stopping per method/family")
    parser.add_argument("--baseline-repo", type=Path, default=Path("/workspace/mundo-X"))
    parser.add_argument("--candidate-repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--variants", default="baseline,candidate", help="baseline,candidate or one of them")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_ROOT / "scale" / dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ"))
    parser.add_argument("--compute-lock", type=Path, default=DEFAULT_ROOT / "compute.lock")
    parser.add_argument("--timeout", type=float, default=15, help="wall seconds per worker, maximum15")
    parser.add_argument("--memory-mib", type=int, default=1536, help="address space limit per worker, maximum1536MiB")
    parser.add_argument("--qubits", type=comma_numbers, help="override non-Clifford sizes")
    parser.add_argument("--ghz-qubits", type=comma_numbers, help="override compact GHZ sizes")
    parser.add_argument("--families", default=",".join(FAMILIES), help="comma separated product_ht,product_t2,random,cli")
    parser.add_argument("--methods", default="reduced,dense,compact", help="comma separated methods; compact only T2 and GHZ")
    parser.add_argument("--t-multiplier", type=int, default=4, help="T count = multiplier*n in random and cli")
    parser.add_argument("--seed", type=int, default=20261017)
    parser.add_argument("--repeats", type=int, default=3, help="maximum3 repetitions for n<=repeat-max-n")
    parser.add_argument("--repeat-max-n", type=int, default=16)
    parser.add_argument("--validation-max-n", type=int, default=8, help="separate correctness worker, maximum12")
    parser.add_argument("--skip-boundaries", action="store_true")
    parser.add_argument("--compile-only", action="store_true")
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    if not 0 < args.timeout <= 15:
        parser.error("timeout must be in (0,15]")
    if not 64 <= args.memory_mib <= 1536:
        parser.error("memory must be in [64,1536]MiB")
    if not 1 <= args.repeats <= 3 or not 0 <= args.validation_max_n <= 12:
        parser.error("repeats must be1..3 and validation-max-n0..12")
    if not 0 <= args.seed < 2**64 or not 0 <= args.t_multiplier <= 128:
        parser.error("seed must fit uint64; t-multiplier must be0..128")
    args.variants = args.variants.split(",")
    args.families = args.families.split(",")
    args.methods = args.methods.split(",")
    if any(v not in {"baseline", "candidate"} for v in args.variants):
        parser.error("unknown variant")
    if any(v not in FAMILIES for v in args.families):
        parser.error("unknown family")
    if any(v not in {"reduced", "dense", "compact"} for v in args.methods):
        parser.error("unknown method")
    args.memory_bytes = args.memory_mib * 1024 * 1024
    if args.memory_bytes > MAX_MEMORY:
        parser.error("memory limit too high")
    args.output_dir = args.output_dir.resolve()
    args.baseline_repo = args.baseline_repo.resolve()
    args.candidate_repo = args.candidate_repo.resolve()
    args.compute_lock = args.compute_lock.resolve()
    return args


def main():
    args = parse_args()
    if args.output_dir.exists() and any(args.output_dir.iterdir()):
        raise RuntimeError("output directory must be empty; choose a new directory to preserve previous evidence")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "workers").mkdir()
    driver = Path(__file__).resolve().parents[1] / "native/tests/stress_driver.cpp"
    compiler = subprocess.run([args.compiler, "--version"], text=True, capture_output=True, check=True).stdout
    metadata = {"schema_version": 1, "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
                "supervisor_sha256": sha256(Path(__file__)), "argv": sys.argv,
                "settings": {k: str(v) if isinstance(v, Path) else v for k,v in vars(args).items()},
                "platform": platform.platform(), "logical_cpu_count": os.cpu_count(), "compiler": compiler,
                "notes": ["No quantum hardware or general quantum advantage is tested.",
                          "Every case/method/repeat uses a separate worker and exclusive compute lock.",
                          "Peak RSS is process memory; declared payload excludes allocator, capacity and some metadata.",
                          "Linux VmHWM avoids inherited pre-exec ru_maxrss; raw getrusage is also recorded.",
                          "Comparative timers use complex<double> coefficients or statevectors; tableau bits are exact.",
                          "Historical baseline public reconstruction uses a float base; the precision-fixed candidate uses double. Both are separate correctness workers.",
                          "No dense or full coefficient validation runs in evolution timers.",
                          "GHZ and product T-squared are structured stabilizer families.",
                          "Compact product_t2 rewrites each adjacent T,T to S using an exact gate identity.",
                          "Timeout is a resource frontier, not proof that a method cannot finish."]}
    dump(args.output_dir / "metadata.json", metadata)
    binaries = {}
    for variant in args.variants:
        repo = args.baseline_repo if variant == "baseline" else args.candidate_repo
        binaries[variant], _info = build(repo, variant, driver, args)
        print(f"built {variant}: {binaries[variant]}", flush=True)
    if args.compile_only:
        return 0
    sizes = args.qubits or ([2,4] if args.smoke else list(range(8,61,4))+[63] if args.full else [8,12,16])
    ghz_sizes = args.ghz_qubits or ([8] if args.smoke else [1024,4096,8192,16384,32768,65536] if args.full else [1024,4096])
    rows = []
    stopped = {}
    fingerprints = {}
    sequence = 0
    events = open(args.output_dir / "results.jsonl", "a", buffering=1)

    def emit(row):
        rows.append(row)
        events.write(json.dumps(row, sort_keys=True, allow_nan=False)+"\n")
        csv_write(args.output_dir / "results.csv", rows)
        print(f"{row['variant']} {row['family']} n={row['qubits']} {row['method']} r={row['repeat']}: {row['status']}"
              + (f" evolution={row['evolution_ms']:.3f}ms RSS={row['process_peak_rss_bytes']/1024**2:.1f}MiB components={row.get('component_count')}" if "evolution_ms" in row else ""), flush=True)

    def case(variant, family, method, n, repeat):
        nonlocal sequence
        sequence += 1
        base = {"variant": variant, "family": family, "method": method, "qubits": n,
                "requested_t_count": args.t_multiplier*n, "seed": args.seed, "repeat": repeat}
        key = (variant,family,method)
        if key in stopped:
            emit({**base, "status": "adaptive_skipped", "reason": stopped[key]})
            return
        # Reserve128MiB for binary/runtime/circuit overhead before launching.
        predicted = (1 << n)*16 if method == "dense" else n*(2*((n+63)//64)*8+1) if method == "compact" else 0
        if predicted > args.memory_bytes - 128*1024**2:
            reason = f"minimum state payload {predicted} exceeds address-space budget minus128MiB"
            stopped[key] = reason
            emit({**base, "status": "memory_budget", "reason": reason, "predicted_payload_bytes": predicted})
            return
        command = [str(binaries[variant]), "--method", method, "--family", family, "--qubits", str(n),
                   "--seed", str(args.seed), "--t-count", str(args.t_multiplier*n),
                   "--memory-bytes", str(args.memory_bytes), "--cpu-seconds", str(math.ceil(args.timeout))]
        name = f"{sequence:04d}-{variant}-{family}-{method}-n{n}-r{repeat}"
        result = run_worker(command, args, args.output_dir, name)
        # A reject at64 is expected, but success there is not a PASS for the boundary test.
        if family == "boundary" and n == 64 and result["status"] == "ok":
            result["status"] = "correctness_failure"
            result["reason"] = "reduced64 should reject its unsupported mask width"
        fingerprint = result.get("circuit_fnv1a64")
        fkey = (family,n,args.t_multiplier*n,args.seed)
        if fingerprint:
            if fkey in fingerprints and fingerprints[fkey] != fingerprint:
                result["status"] = "correctness_failure"
                result["reason"] = "circuit mismatch between variants/methods"
            fingerprints[fkey] = fingerprint
        if result["status"] in STOP_STATUSES:
            stopped[key] = f"stopped after n={n},repeat={repeat},status={result['status']}"
        emit({**base, **result})

    for family in args.families:
        for n in sizes:
            for variant in args.variants:
                for method in args.methods:
                    if method == "compact" and family != "product_t2":
                        continue
                    repeats = 1 if args.smoke or n > args.repeat_max_n else args.repeats
                    for repeat in range(1,repeats+1):
                        case(variant,family,method,n,repeat)
                if n <= args.validation_max_n:
                    case(variant,family,"validate",n,1)
    if "compact" in args.methods:
        for n in ghz_sizes:
            for variant in args.variants:
                repeats = 1 if args.smoke or n > 4096 else args.repeats
                for repeat in range(1,repeats+1):
                    case(variant,"ghz","compact",n,repeat)
    if not args.skip_boundaries:
        for variant in args.variants:
            for n in (63,64):
                case(variant,"boundary","reduced",n,1)
    events.close()
    counts = {}
    for row in rows:
        counts[row["status"]] = counts.get(row["status"],0)+1
    summary = {"case_count": len(rows), "status_counts": counts,
               "adaptive_stops": [{"variant": key[0],"family": key[1],"method": key[2],"reason": value} for key,value in stopped.items()],
               "resource_limits": {"address_space_bytes": args.memory_bytes,"worker_timeout_seconds": args.timeout},
               "outputs": {"csv": str(args.output_dir / "results.csv"), "jsonl": str(args.output_dir / "results.jsonl")}}
    dump(args.output_dir / "summary.json", summary)
    print(json.dumps(summary, sort_keys=True), flush=True)
    # Resource frontiers are expected observations; wrong answers and malformed workers fail the suite.
    return 1 if any(row["status"] in {"correctness_failure","runtime_failure","invalid_argument","signal_failure"} for row in rows) else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, subprocess.SubprocessError) as exc:
        print(f"stress_architecture: {exc}", file=sys.stderr)
        sys.exit(2)
