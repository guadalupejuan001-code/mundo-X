#!/usr/bin/env python3
"""Bounded, sequential numerical stress; no source-repository mutation.

Every compiler/test/oracle process gets <=20s wall, <=1GiB virtual memory,
19s CPU, one BLAS thread, and a shared exclusive compute.lock.
Run: python3 stress.py --suite sanity --output-dir /tmp/mundox-numerics
"""
import argparse
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import random
import resource
import signal
import subprocess
import time

SOURCE=Path(__file__).resolve().parent
WORK=Path("/tmp/mundox-numerics")
LOCK=WORK.parent/"mundox-compute.lock"
LIMIT=1024**3
TOLERANCE=3e-5
ENV=dict(os.environ,OPENBLAS_NUM_THREADS="1",OMP_NUM_THREADS="1",MKL_NUM_THREADS="1")

def limits():
    resource.setrlimit(resource.RLIMIT_AS,(LIMIT,LIMIT))
    resource.setrlimit(resource.RLIMIT_CPU,(19,19))
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))

def bounded(command):
    with open(LOCK,"a") as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        start=time.monotonic()
        process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                                 text=True,env=ENV,preexec_fn=limits,start_new_session=True)
        try:
            stdout,stderr=process.communicate(timeout=20)
            return dict(returncode=process.returncode,stdout=stdout,stderr=stderr,
                        elapsed_seconds=time.monotonic()-start,timeout=False)
        except subprocess.TimeoutExpired as e:
            os.killpg(process.pid,signal.SIGKILL)
            stdout,stderr=process.communicate()
            return dict(returncode=process.returncode,stdout=stdout,stderr=stderr+str(e),
                        elapsed_seconds=time.monotonic()-start,timeout=True)

def random_gates(n,depth,seed,clifford=False):
    rng=random.Random(seed)
    options=["H","S","X","Z"]+([] if clifford else ["T"])
    if n>1: options += ["CX","CZ"]
    for _ in range(depth):
        op=rng.choice(options);a=rng.randrange(n);b=0
        if op in ("CX","CZ"): b=(a+rng.randrange(1,n))%n
        yield op,a,b

def inverse(gates):
    for op,a,b in reversed(gates):
        for _ in range(7 if op=="T" else 3 if op=="S" else 1): yield op,a,b

def deep_cases():
    out=[]
    def add(id,n,gates,kind,**extra):
        out.append(dict(id=id,n=n,gates=list(gates),kind=kind,**extra))
    for depth in [1000,10000]:
        for n in range(1,11):
            seed=2026101700+n
            add(f"random-n{n}-d{depth}",n,random_gates(n,depth,seed),"random",seed=seed)
    for n in range(1,11):
        seed=2026101700+n
        add(f"random-n{n}-d100000",n,random_gates(n,100000,seed),"random",seed=seed)
    for n in [1,4,8,10]:
        seed=810000+n
        add(f"clifford-n{n}-d100000",n,random_gates(n,100000,seed,True),"clifford",seed=seed)
    for depth in [1000,10000,100000]:
        add(f"HT-repeat-d{depth}",1,[("H",0,0),("T",0,0)]*(depth//2),"HT_repeat")
        add(f"T-repeat-d{depth}",1,[("H",0,0)]+[("T",0,0)]*depth,"T_repeat")
        add(f"HH-repeat-d{depth}",1,[("H",0,0)]*depth,"HH_identity")
        add(f"CZ-repeat-d{depth}",2,[("H",0,0),("H",1,0)]+[("CZ",0,1)]*depth,"CZ_identity")
        for n in [4,10]:
            add(f"T8-product-n{n}-d{depth}",n,
                [("H",q,0) for q in range(n)]+[("T",i%n,0) for i in range(depth)],
                "T8_product")
    for n in [1,4,8,10]:
        seed=910000+n
        fwd=list(random_gates(n,1000,seed))
        add(f"inverse-n{n}",n,fwd+list(inverse(fwd)),"inverse_identity",seed=seed)
    # Mixed Clifford circuits and forced measurements; requests on already
    # deterministic qubits intentionally exercise the documented false return.
    for n in range(1,11):
        for sample in range(2):
            seed=1100000+n*100+sample
            gates=[];rng=random.Random(seed)
            for block in range(10):
                gates.extend(random_gates(n,40,seed+block,True))
                gates.append(("M",rng.randrange(n),rng.randrange(2)))
            add(f"measurement-n{n}-s{sample}",n,gates,"compact_measurement",seed=seed)
    add("measurement-declines-impossible",1,[("M",0,1),("H",0,0),("M",0,1),("M",0,0)],
        "compact_measurement")
    return out

def sanity_cases():
    """Original 200-case independent review, with fixed seeds and gate counts."""
    out=[]
    def original_random(n,depth,seed):
        rng=random.Random(seed)
        options=["H","S","X","Z","T"]+(["CX","CZ"] if n>1 else [])
        gates=[]
        for _ in range(depth):
            op=rng.choice(options);a=rng.randrange(n)
            b=rng.choice([q for q in range(n) if q!=a]) if op in ("CX","CZ") else 0
            gates.append((op,a,b))
        return gates
    def add(id,n,gates,kind,seed=None):
        out.append(dict(id=id,n=n,gates=gates,kind=kind,seed=seed))
    for n in range(1,9):
        for sample,depth in enumerate([0,1,2,8,16,32,64,100,160,200]*2):
            seed=730000+n*1000+sample
            add(f"sanity-random-n{n}-s{seed}",n,original_random(n,depth,seed),"random",seed)
        for sample in range(2):
            seed=850000+n*1000+sample
            context=original_random(n,30,seed)
            add(f"sanity-T8-n{n}-s{seed}",n,context+[("T",q,0) for q in range(n) for _ in range(8)],"T8_identity",seed)
            forward=original_random(n,35,seed+100000)
            add(f"sanity-inverse-n{n}-s{seed}",n,forward+list(inverse(forward)),"inverse_identity",seed)
        add(f"sanity-T2-is-S-n{n}",n,[("H",q,0) for q in range(n)]+[("T",q,0) for q in range(n) for _ in range(2)],"T2_is_S_stabilizer")
    assert len(out)==200
    return out

def representative_cases():
    """Small selection of the deep corpus, preserving exactly the same gates."""
    ids={"random-n1-d100000","random-n4-d100000","random-n10-d10000",
         "clifford-n10-d100000","HT-repeat-d100000","T-repeat-d100000",
         "HH-repeat-d100000","CZ-repeat-d100000","T8-product-n10-d10000",
         "inverse-n10","measurement-n10-s0","measurement-declines-impossible"}
    return [case for case in deep_cases() if case["id"] in ids]

def metrics(values,expected):
    got=[complex(*z) for z in values];want=[complex(*z) for z in expected]
    norm=sum(abs(z)**2 for z in got);refnorm=sum(abs(z)**2 for z in want)
    overlap=sum(a.conjugate()*b for a,b in zip(want,got))
    fidelity=abs(overlap)**2/(norm*refnorm) if norm*refnorm else 0
    errors=[abs(a-b) for a,b in zip(got,want)]
    return dict(squared_norm=norm,norm_error=abs(norm-1),reference_norm=refnorm,
                fidelity=fidelity,infidelity=max(0,1-fidelity),
                max_amplitude_error=max(errors),l2_error=math.sqrt(sum(e*e for e in errors)))

def git_commit(repo):
    # A downloaded source archive has no .git directory. Header hashes still
    # identify precisely which simulator was tested.
    try:
        result=subprocess.run(["git","-C",str(repo),"rev-parse","HEAD"],
                              text=True,capture_output=True)
        return result.stdout.strip() if result.returncode==0 else None
    except FileNotFoundError:
        return None

def main():
    global WORK,LOCK,TOLERANCE
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo",type=Path,default=SOURCE.parents[1])
    parser.add_argument("--output-dir",type=Path,default=WORK)
    parser.add_argument("--lock-file",type=Path,default=None)
    parser.add_argument("--suite",choices=["sanity","representative","deep","all"],default="sanity")
    parser.add_argument("--tolerance",type=float,default=TOLERANCE,
                        help="Absolute norm/L2 and infidelity tolerance; use 1e-9 for the double candidate")
    parser.add_argument("--only",default="")
    parser.add_argument("--resume",action="store_true",help="Preserve successful recorded cases and run only missing/failed ones")
    args=parser.parse_args()
    if not 0<args.tolerance<1:parser.error("--tolerance must be between 0 and 1")
    TOLERANCE=args.tolerance
    WORK=args.output_dir.resolve()
    if WORK.exists() and any(WORK.iterdir()) and not (args.resume or args.only):
        parser.error("Output directory is not empty; choose a new path or use --resume to preserve recorded cases")
    WORK.mkdir(parents=True,exist_ok=True)
    LOCK=args.lock_file.resolve() if args.lock_file else WORK.parent/"mundox-compute.lock"
    LOCK.parent.mkdir(parents=True,exist_ok=True)
    binary=WORK/"stress_runner"
    build=bounded(["g++","-std=c++17","-O2","-I",str(args.repo/"native/include"),
                   str(SOURCE/"stress_runner.cpp"),"-o",str(binary)])
    if build["returncode"]!=0: raise RuntimeError(build)
    manifest=[];results=[]
    # A filtered rerun replaces only selected cases and preserves the rest.
    if args.only or args.resume:
        if (WORK/"manifest.json").exists():manifest=json.loads((WORK/"manifest.json").read_text())
        if (WORK/"results.json").exists():results=json.loads((WORK/"results.json").read_text())
        if (WORK/"summary.json").exists():
            previous=json.loads((WORK/"summary.json").read_text())
            current={name:hashlib.sha256((args.repo/"native/include/mundo_x"/name).read_bytes()).hexdigest() for name in ["exact_t_experiment.hpp","compact_core.hpp","dense_core.hpp"]}
            if previous.get("header_sha256",current)!=current or previous.get("tolerance",TOLERANCE)!=TOLERANCE:
                raise RuntimeError("Existing results use different headers/tolerance; choose a separate --output-dir")
    (WORK/"circuits").mkdir(exist_ok=True);(WORK/"raw").mkdir(exist_ok=True)
    selected={"sanity":sanity_cases,"representative":representative_cases,
              "deep":deep_cases,"all":lambda:sanity_cases()+deep_cases()}[args.suite]()
    for case in selected:
        if args.only and args.only not in case["id"]: continue
        if args.resume and any(r["id"]==case["id"] and r["status"]=="pass" for r in results):continue
        circuit=WORK/"circuits"/(case["id"]+".txt")
        with circuit.open("w") as f:
            f.write(f"{case['n']} {len(case['gates'])}\n")
            for gate in case["gates"]:f.write(" ".join(map(str,gate))+"\n")
        metadata={k:v for k,v in case.items() if k!="gates"}
        metadata.update(depth=len(case["gates"]),circuit=str(circuit),
                        sha256=hashlib.sha256(circuit.read_bytes()).hexdigest())
        manifest=[m for m in manifest if m["id"]!=case["id"]]
        manifest.append(metadata)
        mode="compact" if case["kind"]=="compact_measurement" else "reduced"
        run=bounded([str(binary),mode,str(circuit)])
        row=dict(metadata,runner={k:v for k,v in run.items() if k!="stdout"})
        if run["returncode"]==0:
            actual=json.loads(run["stdout"])
            raw=WORK/"raw"/(case["id"]+".actual.json");raw.write_text(run["stdout"])
            command=["python3",str(SOURCE/"oracle.py"),str(circuit)]
            if mode=="compact":command.append(str(raw))
            reference_run=bounded(command)
            row["oracle"]={k:v for k,v in reference_run.items() if k!="stdout"}
            if reference_run["returncode"]==0:
                expected=json.loads(reference_run["stdout"])
                (WORK/"raw"/(case["id"]+".oracle.json")).write_text(reference_run["stdout"])
                if mode=="compact":
                    errors=[abs(complex(*z)-1) for z in expected["generator_expectations"]]
                    row.update(max_generator_expectation_error=max(errors),
                               measurement_flags_match=actual["nondeterministic"]==expected["nondeterministic"],
                               declined_impossible=expected["declined_impossible_measurements"],
                               stabilizer_rank=expected["stabilizer_rank"],
                               generators_commute=expected["generators_commute"])
                    row["status"]="pass" if max(errors)<=TOLERANCE and row["measurement_flags_match"] and row["stabilizer_rank"]==case["n"] and row["generators_commute"] else "FAIL"
                else:
                    row.update({k:v for k,v in actual.items() if k not in ("reduced","dense_diagnostic")})
                    row["reduced"]=metrics(actual["reduced"],expected["state"])
                    row["dense_diagnostic"]=metrics(actual["dense_diagnostic"],expected["state"])
                    m=row["reduced"]
                    row["status"]="pass" if all(math.isfinite(v) for v in m.values()) and max(m["norm_error"],m["infidelity"],m["l2_error"])<=TOLERANCE else "FAIL"
            else:row["status"]="oracle_timeout" if reference_run["timeout"] else "oracle_error"
        else:row["status"]="runner_timeout" if run["timeout"] else "runner_error"
        results=[r for r in results if r["id"]!=case["id"]]
        results.append(row)
        (WORK/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
        (WORK/"results.json").write_text(json.dumps(results,indent=2)+"\n")
        print(json.dumps({"case":case["id"],"status":row["status"],
                          "seconds":run["elapsed_seconds"],"reduced":row.get("reduced")} ),flush=True)
    summary=dict(cases=len(results),statuses={s:sum(r["status"]==s for r in results) for s in sorted(set(r["status"] for r in results))},
                 commit=git_commit(args.repo),
                 tolerance=TOLERANCE,process_memory_limit_bytes=LIMIT,process_wall_timeout_seconds=20,
                 process_cpu_timeout_seconds=19,threads=1,
                 requested_suite=args.suite,
                 limitations=["Finite deterministic sample; not a universal correctness proof.",
                              "Performance cutoff is imposed by this test budget, not a mathematical limit.",
                              "DenseCore diagnostic is not the reference; oracle is independent NumPy complex128.",
                              "Measurement checks validate documented forced-branch semantics, not sampling quality."])
    summary["header_sha256"]={name:hashlib.sha256((args.repo/"native/include/mundo_x"/name).read_bytes()).hexdigest() for name in ["exact_t_experiment.hpp","compact_core.hpp","dense_core.hpp"]}
    summary["reduced_cases"]=sum("reduced" in r for r in results)
    summary["compact_measurement_cases"]=sum(r["kind"]=="compact_measurement" for r in results)
    summary["max_completed_depth"]=max((r["depth"] for r in results if r["status"]=="pass"),default=0)
    for metric in ["norm_error","infidelity","max_amplitude_error","l2_error"]:
        completed=[r for r in results if "reduced" in r]
        if completed:
            worst=max(completed,key=lambda r:r["reduced"][metric])
            summary["max_"+metric]=dict(value=worst["reduced"][metric],case=worst["id"])
    (WORK/"summary.json").write_text(json.dumps(summary,indent=2)+"\n")
    print(json.dumps(summary,indent=2),flush=True)
    if any(row["status"]!="pass" for row in results):raise SystemExit(1)

if __name__=="__main__":main()
