#!/usr/bin/env python3
"""Bounded examinations of the existing CompactCore and Reduced engines.

Python standard library only. The C++ worker uses the repository's DenseCore64
for small reference states, analytic signatures/identities for Compact, and no
external simulator. No production core or Factorized engine is substituted.
"""
import argparse
from collections import Counter
import datetime
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import resource
import signal
import subprocess
import sys
import tempfile
import time

HEADROOM_RESERVE_BYTES = 2 * 1024**3


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, content):
    path.write_text(json.dumps(content, indent=2, sort_keys=True) + "\n")


def git_revision(repo):
    try:
        revision = subprocess.check_output(
            ["git", "-C", str(repo), "rev-parse", "HEAD"],
            text=True, stderr=subprocess.DEVNULL).strip()
        return {"git_head": revision, "git_metadata_error": None}
    except FileNotFoundError:
        return {"git_head": None, "git_metadata_error": "git executable unavailable; source hashes identify the inputs"}
    except subprocess.CalledProcessError:
        return {"git_head": None, "git_metadata_error": "git revision unavailable; source hashes identify the inputs"}


def headroom():
    remaining = []
    root = Path("/sys/fs/cgroup")
    try:
        maximum = (root / "memory.max").read_text().strip()
        if maximum != "max":
            remaining.append(int(maximum) - int((root / "memory.current").read_text()))
    except (FileNotFoundError, ValueError):
        pass
    try:
        for line in Path("/proc/meminfo").read_text().splitlines():
            if line.startswith("MemAvailable:"):
                remaining.append(int(line.split()[1]) * 1024)
    except FileNotFoundError:
        pass
    return min(remaining) if remaining else None


def resident_high_water(pid):
    try:
        for line in Path(f"/proc/{pid}/status").read_text().splitlines():
            if line.startswith("VmHWM:"):
                return int(line.split()[1]) * 1024
    except (FileNotFoundError, ProcessLookupError, PermissionError):
        pass
    return 0


def bounded(command, args, label):
    stdout_file = args.output_dir / "raw" / (label + ".stdout.jsonl")
    stderr_file = args.output_dir / "raw" / (label + ".stderr.txt")
    args.compute_lock.parent.mkdir(parents=True, exist_ok=True)
    with args.compute_lock.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        available = headroom()
        if available is not None and available < args.memory_bytes + HEADROOM_RESERVE_BYTES:
            return {"status": "headroom_skip", "available_bytes": available,
                    "required_headroom_bytes": args.memory_bytes + HEADROOM_RESERVE_BYTES,
                    "command": command, "label": label, "events": []}

        def ceilings():
            os.sched_setaffinity(0, {args.cpu})
            resource.setrlimit(resource.RLIMIT_AS, (args.memory_bytes, args.memory_bytes))
            resource.setrlimit(resource.RLIMIT_CPU, (max(1, math.ceil(args.timeout) - 1), math.ceil(args.timeout)))
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

        with stdout_file.open("w+") as stdout, stderr_file.open("w+") as stderr:
            start = time.monotonic()
            child = subprocess.Popen(command, stdout=stdout, stderr=stderr,
                                     preexec_fn=ceilings, start_new_session=True,
                                     env=dict(os.environ, OMP_NUM_THREADS="1", OPENBLAS_NUM_THREADS="1"))
            timed_out, memory_headroom_abort, peak = False, False, 0
            minimum_available = available
            while child.poll() is None:
                peak = max(peak, resident_high_water(child.pid))
                current_available = headroom()
                if current_available is not None:
                    minimum_available = current_available if minimum_available is None else min(minimum_available, current_available)
                    if current_available < HEADROOM_RESERVE_BYTES:
                        memory_headroom_abort = True
                        os.killpg(child.pid, signal.SIGKILL)
                        break
                if time.monotonic() - start >= args.timeout:
                    timed_out = True
                    # The session belongs to this one examination/compile job.
                    # This also contains compiler descendants; no other PID is selected.
                    os.killpg(child.pid, signal.SIGKILL)
                    break
                time.sleep(0.005)
            return_code = child.wait()
            elapsed = time.monotonic() - start
            stdout.seek(0)
            raw = stdout.read()
            events = []
            for line in raw.splitlines():
                try:
                    event = json.loads(line)
                    if isinstance(event, dict):
                        events.append(event)
                except ValueError:
                    pass
    result = next((event for event in reversed(events) if event.get("event") == "result"), {})
    error = next((event for event in reversed(events) if event.get("event") == "error"), {})
    sanity = next((event for event in reversed(events) if event.get("event") == "sanity_summary"), {})
    status = "memory_headroom_abort" if memory_headroom_abort else "timeout" if timed_out else "cpu_budget" if return_code == -signal.SIGXCPU else \
             result.get("status", error.get("status", "completed" if return_code == 0 else "process_failure"))
    if sanity and status not in ("memory_headroom_abort", "timeout", "cpu_budget"):
        status = "pass" if sanity.get("failures") == 0 and return_code == 0 else "FAIL"
    if return_code != 0 and status in ("pass", "completed"):
        status = "FAIL"
    return {**result, "status": status, "label": label, "command": command,
            "return_code": return_code, "worker_pid": child.pid,
            "wall_seconds": elapsed, "peak_rss_lower_bound_bytes": peak,
            "rss_measurement": "Linux /proc worker VmHWM sampled every5ms; lower bound",
            "stdout_file": str(stdout_file), "stderr_file": str(stderr_file),
            "events": events, "headroom_before_bytes": available,
            "minimum_monitored_headroom_bytes": minimum_available,
            "headroom_reserve_bytes": HEADROOM_RESERVE_BYTES,
            "sanity_summary": sanity or None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--compute-lock", type=Path, default=Path(tempfile.gettempdir()) / "mundox-native-diversity-exams" / "compute.lock")
    parser.add_argument("--suite", choices=("sanity", "scale", "all"), default="all")
    parser.add_argument("--timeout", type=float, default=45)
    parser.add_argument("--memory-mib", type=int, default=2048)
    parser.add_argument("--seed", type=int, default=20261018)
    parser.add_argument("--cpu", type=int, default=min(os.sched_getaffinity(0)))
    args = parser.parse_args()
    if not 1 <= args.timeout <= 45 or not 128 <= args.memory_mib <= 2048:
        parser.error("worker budgets must be1..45seconds and128..2048MiB")
    if args.cpu not in os.sched_getaffinity(0) or not 0 <= args.seed < 2**64:
        parser.error("invalid CPU affinity or uint64 seed")
    # The supervisor's monitor also stays on the worker's single assigned CPU.
    os.sched_setaffinity(0, {args.cpu})
    args.repo = args.repo.resolve()
    args.output_dir = args.output_dir.resolve()
    args.compute_lock = args.compute_lock.resolve()
    args.memory_bytes = args.memory_mib * 1024**2
    if args.output_dir.exists() and any(args.output_dir.iterdir()):
        parser.error("output-dir must be empty; existing evidence will not be overwritten")
    (args.output_dir / "raw").mkdir(parents=True)
    source = args.repo / "native/tests/native_original_exams.cpp"
    binary = args.output_dir / "native_original_exams"
    inputs = [source, Path(__file__).resolve(), *[args.repo / "native/include/mundo_x" / name for name in
              ("compact_core.hpp", "dense_core.hpp", "exact_t_experiment.hpp")]]
    manifest = {str(path): digest(path) for path in inputs}
    metadata = {"created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                "platform": platform.platform(), "python": sys.version,
                **git_revision(args.repo),
                "source_sha256": manifest, "seed": args.seed, "cpu_affinity": [args.cpu],
                "limits": {"address_space_bytes": args.memory_bytes, "wall_seconds": args.timeout,
                           "CPU_seconds_soft": max(1, math.ceil(args.timeout) - 1),
                           "CPU_seconds_hard": math.ceil(args.timeout), "parallel_workers": 1,
                           "exclusive_compute_lock": str(args.compute_lock),
                           "preflight_required_available_bytes": args.memory_bytes + HEADROOM_RESERVE_BYTES,
                           "continuously_monitored_headroom_reserve_bytes": HEADROOM_RESERVE_BYTES},
                "notes": ["Existing production CompactCore/ReducedExactTRepresentation only; no Factorized substitution.",
                          "No external simulator or scientific Python dependency is imported.",
                          "DenseCore64 of this repository is the small-circuit comparison, not an external independent oracle.",
                          "Small amplitude comparisons align global phase and check absolute norm-squared/L2 error1e-9.",
                          "Large Reduced cases only measure completed evolution; they are not dense correctness validations.",
                          "Peak RSS is a sampled lower bound; logical payload excludes allocator overhead and capacities.",
                          "All workers use one pinned CPU and the same exclusive lock.",
                          "Compact query validation is timed separately but shares the worker45s wall budget.",
                          "Progress IO and periodic clock checks are included in the evolution timer."]}
    save(args.output_dir / "metadata.json", metadata)
    compiler = bounded(["g++", "--version"], args, "compiler-version")
    compile_command = ["g++", "-std=c++17", "-O3", "-DNDEBUG", "-Wall", "-Wextra", "-Wpedantic",
                       "-I", str(args.repo / "native/include"), str(source), "-o", str(binary)]
    compilation = bounded(compile_command, args, "compile")
    if compilation.get("return_code") != 0:
        save(args.output_dir / "compile.json", compilation)
        raise RuntimeError("compile did not complete; see compile.json/raw")
    if any(digest(path) != manifest[str(path)] for path in inputs):
        raise RuntimeError("examined sources changed during compilation")
    save(args.output_dir / "build.json", {"compiler_version": compiler, "compile": compilation,
                                         "binary_sha256": digest(binary), "source_sha256": manifest})
    results = []
    def record(result):
        results.append(result)
        with (args.output_dir / "results.jsonl").open("a") as out:
            out.write(json.dumps(result, sort_keys=True) + "\n")
        save(args.output_dir / "summary.json", {"status_counts": dict(Counter(row["status"] for row in results)),
                                                "observations": len(results), "results": results})
        print(json.dumps({key: result[key] for key in ("label", "status", "wall_seconds", "evolution_ms", "components",
                          "peak_rss_lower_bound_bytes") if key in result}), flush=True)

    if args.suite in ("sanity", "all"):
        sanity = bounded([str(binary), "sanity"], args, "sanity")
        record(sanity)
        if sanity.get("return_code") != 0:
            raise RuntimeError("sanity checks failed; scale work was not started")
    if args.suite in ("scale", "all"):
        cases = []
        for n in (10000, 16000, 16384, 16385, 32000, 32768, 32769, 65536):
            cases.append(("compact", "ghz", n, 0))
        for n in (1024, 4096, 10000):
            cases.append(("compact", "cluster_line", n, 0))
        for n in (128, 1024, 4096):
            cases.append(("compact", "random_inverse", n, 4096))
        for family in ("product_ht", "product_t8", "connected"):
            for n in (12, 16, 20, 22):
                cases.append(("reduced", family, n, 0))
        cases += [("reduced", "boundary", 63, 0), ("reduced", "boundary", 64, 0)]
        stopped = {}
        for engine, family, n, depth in cases:
            label = f"{engine}-{family}-n{n}"
            key = (engine, family)
            # Conservative allowance for two maps/nodes plus fixed runtime
            # overhead. It is a guard, not a prediction of actual peak RSS.
            estimated = 2**n * 192 + 128 * 1024**2 if engine == "reduced" and family != "boundary" else 0
            if key in stopped:
                record({"label": label, "status": "adaptive_skip", "reason": stopped[key],
                        "core": engine, "family": family, "n": n})
                continue
            if estimated and estimated > args.memory_bytes:
                record({"label": label, "status": "estimated_memory_skip", "estimated_guard_bytes": estimated,
                        "core": engine, "family": family, "n": n})
                continue
            result = bounded([str(binary), engine, family, str(n), str(args.seed), str(depth)], args, label)
            result.update(requested_core=engine, requested_family=family, requested_n=n, seed=args.seed)
            if family == "boundary" and n == 64:
                result["status"] = "expected_rejection" if result.get("return_code") == 2 and any(
                    "1..63" in event.get("message", "") for event in result["events"]) else "FAIL"
            record(result)
            if engine == "reduced" and family != "boundary" and result["status"] in (
                    "timeout", "cpu_budget", "allocation_failure", "process_failure"):
                stopped[key] = f"stopped after{label}:{result['status']}"
        record({"label": "reduced-n24-conservative-guard", "status": "deliberate_skip", "requested_n": 24,
                "estimated_two_map_guard_bytes": 2**24 * 192 + 128 * 1024**2,
                "reason": "Not launched: transient-map guard exceeds2GiB; sizes above22 are outside this examination."})
    if any(row["status"] in ("FAIL", "exception", "process_failure") for row in results):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
