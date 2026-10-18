#!/usr/bin/env python3
"""Serial, bounded streaming-circuit exams with reproducible source snapshots.

Two revisions use the same driver, circuit and supported-query hashes. No saved
gate list is allocated by the C++ worker. Logical amplitudes, estimated reserved
buffers and actual process RSS are separate measurements. Every process gets one
CPU and at most12GiB AS/60s wall; the16GiB cgroup is monitored without changing it.
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
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(tempfile.gettempdir()) / "mundox-scale-exams"
FAMILIES = ("product_ht", "heterogeneous_product", "bell_pairs", "ghz_blocks", "bridge_recover", "ghz")
RESOURCE = {"timeout", "cpu_budget", "allocation_failure", "logical_budget_rejection",
            "memory_headroom_abort", "memory_headroom_skip", "predicted_memory_skip"}
FAILURES = {"query_validation_failed", "runtime_failure", "signal_failure", "invalid_argument", "hash_mismatch"}


def dump(path, obj):
    path.write_text(json.dumps(obj, indent=2, sort_keys=True, allow_nan=False) + "\n")


def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def git(repo, *args):
    try:
        p = subprocess.run(["git", "-C", str(repo), *args], text=True, capture_output=True)
    except OSError:
        return None
    return p.stdout.strip() if p.returncode == 0 else None


def lock(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    handle = path.open("a+")
    fcntl.flock(handle, fcntl.LOCK_EX)
    return handle


def memory():
    root = Path("/sys/fs/cgroup")
    out = {}
    for field in ("memory.current", "memory.max", "memory.events"):
        path = root / field
        out[field] = path.read_text().strip() if path.exists() else None
    return out


def available(snapshot):
    if snapshot["memory.max"] in (None, "max") or snapshot["memory.current"] is None:
        raise RuntimeError("a finite Linux cgroup memory ceiling is required")
    return int(snapshot["memory.max"]) - int(snapshot["memory.current"])


def manifest(repo, driver):
    paths = [driver, *sorted((repo / "native/include/mundo_x").glob("*.hpp"))]
    return {str(p.resolve()): sha(p) for p in paths}


def build(repo, variant, args):
    output = args.output_dir / "build" / variant
    output.mkdir(parents=True)
    driver = args.candidate_repo / "native/tests/factorized_stream_benchmark.cpp"
    before = manifest(repo, driver)
    # Compile saved copies so evidence always contains exactly the built text.
    shutil.copytree(repo / "native/include/mundo_x", output / "include/mundo_x")
    shutil.copy2(driver, output / "factorized_stream_benchmark.cpp")
    binary = output / "factorized_stream_benchmark"
    command = [args.compiler, "-std=c++17", "-O3", "-DNDEBUG", "-Wall", "-Wextra", "-Wpedantic",
               "-I", str(output / "include"), str(output / "factorized_stream_benchmark.cpp"), "-o", str(binary)]
    with lock(args.compute_lock):
        result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    (output / "compile.stdout.txt").write_text(result.stdout)
    (output / "compile.stderr.txt").write_text(result.stderr)
    after = manifest(repo, driver)
    info = {"repo": str(repo), "git_head": git(repo, "rev-parse", "HEAD"),
            "git_status": git(repo, "status", "--short"), "source_sha256": before,
            "source_unchanged_during_build": before == after, "returncode": result.returncode,
            "command": command, "command_shell": shlex.join(command),
            "binary_sha256": sha(binary) if binary.exists() else None}
    dump(output / "build.json", info)
    if result.returncode:
        raise RuntimeError(f"build failed: {output / 'compile.stderr.txt'}")
    if before != after:
        raise RuntimeError("source changed during compilation; retry after freeze")
    return binary, info


def process_memory(pid):
    try:
        return {line.split()[0][:-1]: int(line.split()[1]) * 1024
                for line in Path(f"/proc/{pid}/status").read_text().splitlines()
                if line.startswith(("VmRSS:", "VmHWM:", "VmSize:"))}
    except (FileNotFoundError, ProcessLookupError, PermissionError):
        return {}


def worker(command, name, args):
    out_path = args.output_dir / "workers" / f"{name}.stdout.json"
    err_path = args.output_dir / "workers" / f"{name}.stderr.jsonl"
    peak = peak_vm = max_cgroup = 0
    stop = None
    with lock(args.compute_lock):
        before = memory()
        if available(before) < args.memory_bytes + args.reserve_bytes:
            return {"status": "memory_headroom_skip", "reason": "cgroup free headroom below workerAS+reserve",
                    "cgroup_before": before, "command": command, "command_shell": shlex.join(command)}
        with out_path.open("w+") as out, err_path.open("w+") as err:
            start = time.monotonic()
            proc = subprocess.Popen(command, stdout=out, stderr=err)
            while proc.poll() is None:
                pm = process_memory(proc.pid)
                peak = max(peak, pm.get("VmHWM", 0), pm.get("VmRSS", 0))
                peak_vm = max(peak_vm, pm.get("VmSize", 0))
                current = memory()
                max_cgroup = max(max_cgroup, int(current["memory.current"]))
                if available(current) < args.reserve_bytes:
                    stop = "memory_headroom_abort"
                elif time.monotonic() - start >= args.timeout:
                    stop = "timeout"
                if stop:
                    proc.kill()  # Only our child PID, never a group or another process.
                    break
                time.sleep(0.025)
            code = proc.wait()
            wall = (time.monotonic() - start) * 1000
            out.seek(0)
            raw_out = out.read()
            err.seek(0)
            raw_err = err.read()
    try:
        result = json.loads(raw_out)
        if not isinstance(result, dict):
            result = {}
    except (ValueError, json.JSONDecodeError):
        result = {}
    status = stop or result.get("status", "runtime_failure")
    if not stop and code == -24:
        status = "cpu_budget"
    elif not stop and code < 0:
        status = "signal_failure"
    elif code != 0 and status == "ok":
        status = "runtime_failure"
    progress = []
    for line in raw_err.splitlines():
        try:
            event = json.loads(line)
            if isinstance(event, dict) and "phase" in event:
                progress.append(event)
        except (ValueError, json.JSONDecodeError):
            pass
    return {**result, "status": status, "returncode": code, "process_wall_ms": wall,
            "process_peak_rss_bytes": max(peak, result.get("peak_rss_bytes", 0)),
            "observed_peak_virtual_bytes": peak_vm, "observed_peak_cgroup_current_bytes": max_cgroup,
            "rss_source": "linux_worker_VmHWM_plus_proc_poll_no_inherited_ru_maxrss",
            "cgroup_before": before, "cgroup_after": memory(),
            "command": command, "command_shell": shlex.join(command),
            "stdout_file": str(out_path), "stderr_file": str(err_path),
            "stdout": raw_out, "stderr": raw_err, "progress": progress,
            "last_completed_phase": progress[-1]["phase"] if progress else None}


def csv_write(path, rows):
    fields = ["variant", "family", "method", "qubits", "seed", "status", "circuit_fnv1a64",
              "query_a_fnv1a64", "query_b_fnv1a64", "gate_count", "circuit_hash_preparation_ms",
              "initialization_ms", "evolution_ms", "refactorization_ms", "basis_preparation_ms",
              "query_hash_preparation_ms", "query_ms", "analytic_validation_ms", "negative_query_ms",
              "small_oracle_ms", "worker_total_ms", "process_wall_ms", "logical_amplitude_bytes",
              "process_peak_rss_bytes", "factor_count", "largest_factor_qubits", "amplitude_count",
              "legacy_tolerance_pass", "scaled_log_tolerance", "scaled_tolerance_pass",
              "max_local_query_error", "max_log_magnitude_error", "max_log_phase_error",
              "small_oracle_pass", "max_small_oracle_amplitude_error", "negative_zero_probe_pass",
              "reason", "command_shell", "stdout_file", "stderr_file"]
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def sizes(raw):
    values = sorted(set(int(x) for x in raw.split(",")))
    if not values or values[0] < 1 or values[-1] > 200000000:
        raise argparse.ArgumentTypeError("sizes must be positive and <=200000000")
    return values


def parse():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    modes = p.add_mutually_exclusive_group()
    modes.add_argument("--smoke", action="store_true")
    modes.add_argument("--full", action="store_true")
    p.add_argument("--baseline-repo", type=Path,
                   help="optional baseline source directory; Git metadata is not required")
    p.add_argument("--candidate-repo", type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument("--variants",
                   help="default:candidate; baseline,candidate when --baseline-repo is provided")
    p.add_argument("--methods", default="factorized", help="available method: factorized")
    p.add_argument("--families", default=",".join(FAMILIES))
    p.add_argument("--output-dir", type=Path, default=ROOT / dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ"))
    p.add_argument("--compute-lock", type=Path, default=ROOT / "compute.lock")
    p.add_argument("--product-qubits", type=sizes)
    p.add_argument("--pair-qubits", type=sizes)
    p.add_argument("--group-qubits", type=sizes)
    p.add_argument("--ghz-qubits", type=sizes)
    p.add_argument("--baseline-max-qubits", type=int, default=10000000)
    p.add_argument("--memory-mib", type=int, default=8192)
    p.add_argument("--payload-mib", type=int, default=4096)
    p.add_argument("--headroom-reserve-mib", type=int, default=2048)
    p.add_argument("--timeout", type=float, default=45)
    p.add_argument("--seed", type=int, default=20261018)
    p.add_argument("--compiler", default="g++")
    p.add_argument("--compile-only", action="store_true")
    a = p.parse_args()
    if not 64 <= a.memory_mib <= 12288 or not 1 <= a.payload_mib <= 8192 or not 0 < a.timeout <= 60:
        p.error("worker limits:AS64..12288MiB,payload1..8192MiB,wall(0,60]s")
    if not 1024 <= a.headroom_reserve_mib <= 8192 or not 0 <= a.seed < 2**64:
        p.error("reserve must be>=1GiB and seed fit uint64")
    chosen_variants = a.variants or ("baseline,candidate" if a.baseline_repo is not None else "candidate")
    a.variants, a.methods, a.families = chosen_variants.split(","), a.methods.split(","), a.families.split(",")
    if any(v not in ("baseline", "candidate") for v in a.variants) or any(m != "factorized" for m in a.methods) or any(f not in FAMILIES for f in a.families):
        p.error("unknown variant, method or family")
    if "baseline" in a.variants and a.baseline_repo is None:
        p.error("--variants includes baseline: provide --baseline-repo SOURCE_DIRECTORY or use --variants candidate")
    if not 1 <= a.baseline_max_qubits <= 10000000:
        p.error("baseline cap must be1..10000000")
    for name in ("baseline_repo", "candidate_repo", "output_dir", "compute_lock"):
        value = getattr(a, name)
        if value is not None:
            setattr(a, name, value.resolve())
    a.memory_bytes = a.memory_mib * 1024**2
    a.payload_bytes = a.payload_mib * 1024**2
    a.reserve_bytes = a.headroom_reserve_mib * 1024**2
    return a


def main():
    a = parse()
    if a.output_dir.exists() and any(a.output_dir.iterdir()):
        raise RuntimeError("existing evidence is never overwritten; select an empty output directory")
    a.output_dir.mkdir(parents=True, exist_ok=True)
    (a.output_dir / "workers").mkdir()
    smoke = a.smoke or not a.full
    plan = {
        "product_ht": a.product_qubits or ([4, 8] if smoke else [1000000, 10000000, 30000000, 100000000]),
        "heterogeneous_product": a.product_qubits or ([4, 8] if smoke else [1000000, 10000000, 30000000, 100000000]),
        "bell_pairs": a.pair_qubits or ([4, 8] if smoke else [1000000, 10000000]),
        "ghz_blocks": a.group_qubits or ([4, 8] if smoke else [1000000, 10000000]),
        "bridge_recover": [4],
        "ghz": a.ghz_qubits or ([4, 8] if smoke else [21, 24, 25, 26, 27, 28])}
    preregistration = {"created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "argv": sys.argv, "platform": platform.platform(), "supervisor_sha256": sha(Path(__file__)),
        "settings": {k: str(v) if isinstance(v, Path) else v for k, v in vars(a).items()},
        "plan": plan, "cgroup_before": memory(),
        "acceptance": {"local_and_small_dense_oracle_absolute": 3e-11, "legacy_log_absolute": 1e-8,
                       "scaled_log_criterion": "min(1e-7,max(3e-11,4*N*double_epsilon))",
                       "criterion_is_heuristic_not_a_proven_error_bound": True,
                       "values_by_N": {str(n): min(1e-7, max(3e-11, 4*n*sys.float_info.epsilon))
                                       for ns in plan.values() for n in ns}},
        "notes": ["The full circuit is generated and hashed before initialization; no gate list exists.",
                  "Evolution includes streaming loop generation and requested factorization work.",
                  "Two supported heterogeneous bitstrings are constructed from8 patterns; no O(N) reference state.",
                  "Preparation and hashes for query bitstrings are separately timed; a negative zero probe is separate.",
                  "Legacy tolerance failures remain explicit even if the preregistered scale criterion passes.",
                  "One observation per case; no confidence intervals or general quantum advantage claim.",
                  "Logical amplitudes and storage estimates are not process RSS; allocator/temporaries may add memory.",
                  "Old implementation is capped at10million qubits; full GHZ scale probes use candidate only.",
                  "Bridge uses320B amplitude payload and an explicit factorization cut.",
                  "Every worker is serial under the shared lock; only its own PID may be stopped."]}
    dump(a.output_dir / "metadata.json", preregistration)
    shutil.copy2(Path(__file__), a.output_dir / "supervisor_snapshot.py")
    binaries, builds = {}, {}
    for variant in a.variants:
        repo = a.baseline_repo if variant == "baseline" else a.candidate_repo
        binaries[variant], builds[variant] = build(repo, variant, a)
        print(f"built {variant}", flush=True)
    if a.compile_only:
        return 0
    rows, fingerprints, stopped, previous = [], {}, {}, {}
    events = (a.output_dir / "results.jsonl").open("a", buffering=1)

    def emit(row):
        rows.append(row)
        events.write(json.dumps(row, sort_keys=True, allow_nan=False) + "\n")
        csv_write(a.output_dir / "results.csv", rows)
        print(json.dumps({k: row.get(k) for k in ("variant", "family", "method", "qubits", "status",
              "initialization_ms", "evolution_ms", "query_ms", "process_peak_rss_bytes",
              "legacy_tolerance_pass", "scaled_tolerance_pass", "max_log_magnitude_error", "max_log_phase_error")}), flush=True)

    for family in a.families:
        for n in plan[family]:
            for variant in a.variants:
                for method in a.methods:
                    base = {"variant": variant, "family": family, "qubits": n, "method": method, "seed": a.seed}
                    key = (variant, family, method)
                    if variant == "baseline" and (n > a.baseline_max_qubits or family == "ghz" and not smoke):
                        emit({**base, "status": "baseline_scope_skipped", "reason": "preregistered baseline size/family policy"})
                        continue
                    if key in stopped:
                        emit({**base, "status": "adaptive_skipped", "reason": stopped[key]})
                        continue
                    if key in previous:
                        pn, prss = previous[key]
                        predicted = prss * n / pn + 256 * 1024**2
                        if n > pn and family != "ghz" and predicted > a.memory_bytes - 128 * 1024**2:
                            emit({**base, "status": "predicted_memory_skip", "reason": "prior worker RSS scaling exceedsAS margin",
                                  "predicted_process_rss_bytes": predicted})
                            stopped[key] = "predicted memory frontier"
                            continue
                    payload = 320 if family == "bridge_recover" else a.payload_bytes
                    command = [str(binaries[variant]), "--method", method, "--family", family, "--qubits", str(n),
                               "--seed", str(a.seed), "--max-block-qubits", "28", "--block-size", "4",
                               "--memory-bytes", str(a.memory_bytes), "--logical-amplitude-bytes", str(payload),
                               "--cpu-seconds", str(math.ceil(a.timeout))]
                    result = worker(command, f"{len(rows)+1:03d}-{variant}-{family}-{method}-n{n}", a)
                    if "circuit_fnv1a64" in result:
                        fp = tuple(result[k] for k in ("circuit_fnv1a64", "query_a_fnv1a64", "query_b_fnv1a64"))
                        fkey = (family, n, a.seed)
                        if fkey in fingerprints and fingerprints[fkey] != fp:
                            result["status"] = "hash_mismatch"
                            result["reason"] = "circuit or supported-query input differs between variants/methods"
                        fingerprints[fkey] = fp
                    if result["status"] in RESOURCE:
                        stopped[key] = f"n={n}:{result['status']}"
                    if result.get("process_peak_rss_bytes") and result["status"] == "ok":
                        previous[key] = (n, result["process_peak_rss_bytes"])
                    emit({**base, **result})
    events.close()
    counts = {}
    for row in rows:
        counts[row["status"]] = counts.get(row["status"], 0) + 1
    unchanged = {}
    for variant in a.variants:
        repo = a.baseline_repo if variant == "baseline" else a.candidate_repo
        unchanged[variant] = manifest(repo, a.candidate_repo / "native/tests/factorized_stream_benchmark.cpp") == builds[variant]["source_sha256"]
    summary = {"case_count": len(rows), "status_counts": counts,
               "legacy_tolerance_failures": [{k: r[k] for k in ("variant", "family", "method", "qubits", "max_log_magnitude_error", "max_log_phase_error")}
                                             for r in rows if r.get("legacy_tolerance_pass") is False],
               "source_unchanged_during_suite": unchanged, "cgroup_after": memory(),
               "outputs": {"csv": str(a.output_dir / "results.csv"), "jsonl": str(a.output_dir / "results.jsonl")}}
    dump(a.output_dir / "summary.json", summary)
    print(json.dumps(summary, sort_keys=True), flush=True)
    return int(any(r["status"] in FAILURES for r in rows) or not all(unchanged.values()))


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, subprocess.SubprocessError) as exc:
        print(f"scale_factorized_stream: {exc}", file=sys.stderr)
        sys.exit(2)
