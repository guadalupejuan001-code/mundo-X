#!/usr/bin/env python3
"""Compare unchanged baseline and current FactorizedCore with identical probes."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="d4f5520")
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    header_path = "native/include/mundo_x/factorized_core.hpp"
    base_sha = subprocess.check_output(
        ["git", "rev-parse", "--verify", args.base + "^{commit}"], cwd=root, text=True
    ).strip()
    baseline = subprocess.check_output(["git", "show", base_sha + ":" + header_path], cwd=root)
    current = (root / header_path).read_bytes()
    flags = ["-std=c++17", "-O3", "-DNDEBUG", "-Wall", "-Wextra", "-Wpedantic"]
    result = {
        "recorded_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "baseline_commit": base_sha,
        "baseline_header_sha256": hashlib.sha256(baseline).hexdigest(),
        "current_header_sha256": hashlib.sha256(current).hexdigest(),
        "compiler": subprocess.check_output([args.compiler, "--version"], text=True).splitlines()[0],
        "platform": platform.platform(),
        "flags": flags,
        "repetitions": 1,
        "notes": [
            "Same probe and budgets for both headers; no dense export.",
            "Fanout uses H on every qubit, T on control, CX to plus-state targets.",
            "Gauge uses H on every qubit, S on control, two CZ and a strict split per fresh partner.",
            "Resource figures are for this host, not Android; timings are exploratory single runs.",
            "RSS is Linux /proc/self/status VmHWM in bytes; logical payload and owned storage are separate quantities.",
            "All local probabilities and one logarithmic amplitude have analytic product-state references.",
        ],
        "runs": [],
    }
    with tempfile.TemporaryDirectory(prefix="mundo-x-structure-") as temporary:
        temporary = Path(temporary)
        old_include = temporary / "baseline/include"
        old_header = old_include / "mundo_x/factorized_core.hpp"
        old_header.parent.mkdir(parents=True)
        old_header.write_bytes(baseline)
        for label, include in [("baseline", old_include), ("current", root / "native/include")]:
            executable = temporary / (label + "-probe")
            subprocess.run([
                args.compiler, *flags, "-I" + str(include),
                str(root / "native/tests/factorized_structure_probe.cpp"),
                "-o", str(executable),
            ], check=True, timeout=120)
            for mode, n in [("fanout", 20), ("fanout", 100000), ("gauge", 8192)]:
                run = subprocess.run([str(executable), mode, str(n)], text=True,
                                     capture_output=True, timeout=60)
                if run.returncode not in (0, 1):
                    raise RuntimeError(run.stderr or "probe terminated abnormally")
                record = json.loads(run.stdout)
                record["variant"] = label
                record["exit_code"] = run.returncode
                result["runs"].append(record)
    # Baseline failures are expected evidence; every changed-core case must pass.
    if not all(run["success"] for run in result["runs"] if run["variant"] == "current"):
        raise RuntimeError("current implementation failed a probe")
    serialized = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_text(serialized)
    else:
        print(serialized, end="")


if __name__ == "__main__":
    main()
