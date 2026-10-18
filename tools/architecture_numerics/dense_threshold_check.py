#!/usr/bin/env python3
"""Scan DenseCore float32 HT for first norm-tolerance crossing; validate prefix."""
import json
import argparse
import sys
from pathlib import Path
sys.dont_write_bytecode=True
import stress

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--repo",type=Path,default=stress.SOURCE.parents[1])
parser.add_argument("--output-dir",type=Path,default=stress.WORK)
parser.add_argument("--lock-file",type=Path)
args=parser.parse_args()
work=args.output_dir.resolve()
if work.exists() and any(work.iterdir()):
    parser.error("Output directory is not empty; choose a new path to preserve previous evidence")
work.mkdir(parents=True,exist_ok=True)
(work/"circuits").mkdir(exist_ok=True)
stress.WORK=work
stress.LOCK=args.lock_file.resolve() if args.lock_file else work.parent/"mundox-compute.lock"
stress.LOCK.parent.mkdir(parents=True,exist_ok=True)
binary=work/"dense_threshold"
build=stress.bounded(["g++","-std=c++17","-O2","-I",str(args.repo/"native/include"),
                      str(stress.SOURCE/"dense_threshold.cpp"),"-o",str(binary)])
assert build["returncode"]==0,build
run=stress.bounded([str(binary)])
assert run["returncode"]==0,run
scan=json.loads(run["stdout"])
prefix=work/"circuits"/"HT-first-dense-norm-crossing.txt"
depth=scan["first_gate"]
with prefix.open("w") as f:
    f.write(f"1 {depth}\n")
    for step in range(depth):f.write("H 0 0\n" if step%2==0 else "T 0 0\n")
oracle=stress.bounded(["python3",str(stress.SOURCE/"oracle.py"),str(prefix)])
assert oracle["returncode"]==0,oracle
ref=json.loads(oracle["stdout"])
runner=work/"stress_runner"
build=stress.bounded(["g++","-std=c++17","-O2","-I",str(args.repo/"native/include"),
                      str(stress.SOURCE/"stress_runner.cpp"),"-o",str(runner)])
assert build["returncode"]==0,build
actual=stress.bounded([str(runner),"reduced",str(prefix)])
assert actual["returncode"]==0,actual
state=json.loads(actual["stdout"])
result=dict(first_crossing_scan=scan,
            dense_diagnostic=stress.metrics(scan["state"],ref["state"]),
            reduced_at_same_prefix=stress.metrics(state["reduced"],ref["state"]),
            cause="DenseCore uses complex<float> and float H/T coefficients; repeated non-Clifford HT rotations accumulate norm error. The reduced representation remains accurate on this prefix; its base precision depends on the selected source revision.",
            scope="First crossing along this one deterministic HT prefix at norm tolerance3e-5, not a universal circuit-depth limit.",
            circuit=str(prefix))
(work/"dense_threshold_results.json").write_text(json.dumps(result,indent=2)+"\n")
print(json.dumps(result,indent=2))
