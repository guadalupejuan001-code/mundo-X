#!/usr/bin/env python3
"""Run matched Ether binaries, compare results, report *measured* regressions.

Medians of five alternating matched trials reduce cold-start and order bias.
Only source correctness is mandatory; a significantly slower candidate or no
observable improvement causes this experiment to fail rather than auto-merge.
"""
import json, statistics, subprocess, sys
from pathlib import Path
base=Path(sys.argv[1]); candidate=Path(sys.argv[2]); dest=Path(sys.argv[3])
cases=[("short",60000),("long",14000),("dense",12000),("deep",14000)]
reports={}
for case,iterations in cases:
    rows={"base":[],"candidate":[]}
    for sample in range(5):
        order=("base","candidate") if sample%2==0 else ("candidate","base")
        for name in order:
            executable=base if name=="base" else candidate
            r=subprocess.run([str(executable),case,str(iterations)],
                             capture_output=True,text=True,timeout=90,check=True)
            data=json.loads(r.stdout.strip())
            if data["queries"]!=iterations or data["mode"]!=case or data["us"]<=0:
                raise RuntimeError("Malformed timing: "+r.stdout)
            rows[name].append(data)
    before=statistics.median(x["us"] for x in rows["base"])
    after=statistics.median(x["us"] for x in rows["candidate"])
    if {x["checksum"] for a in rows.values() for x in a}.__len__()!=1:
        raise RuntimeError("Benchmark checksum diverged: "+case)
    reports[case]={"baseline_median_us":round(before,3),
                   "candidate_median_us":round(after,3),
                   "ratio_candidate_over_base":round(after/before,4),
                   "speedup":round(before/after,4),
                   "checksum":rows["base"][0]["checksum"],
                   "samples":{k:[round(x["us"],3) for x in v] for k,v in rows.items()}}
dest.parent.mkdir(parents=True,exist_ok=True)
dest.write_text(json.dumps({"schema":"ether-core-a-b-20261112","baseline":"pinned main",
                           "results":reports},indent=2)+"\n")
print(dest.read_text())
# Performance gate: distinguish useful speedup from noise. Reject major
# regressions. Tiny slowdowns on individual microbenchmarks are advisory only.
if max(x["speedup"] for x in reports.values())<1.03:
    raise SystemExit("No demonstrated 3% speedup; do not adopt")
if min(x["speedup"] for x in reports.values())<0.85:
    raise SystemExit("Severe measured regression; do not adopt")
