#!/usr/bin/env python3
"""Gate Solar high-resolution timing stability against the exact full reference.

The inputs are reports produced by solar_shader_check.py. This is intentionally a
relative same-run gate: it does not claim physical-device FPS. Exact pixel
comparison remains mandatory in the producer report.
"""
from __future__ import annotations

import argparse
import json
import statistics
import sys
from pathlib import Path


def sample_stats(values):
    values=[float(v) for v in values]
    if not values:
        raise ValueError("timing sample is empty")
    median=statistics.median(values)
    mean=statistics.fmean(values)
    stdev=statistics.pstdev(values) if len(values)>1 else 0.0
    return {
        "samples": values,
        "median_ms": median,
        "mean_ms": mean,
        "stdev_ms": stdev,
        "cv": 0.0 if mean==0 else stdev/mean,
        "spread_ratio": 0.0 if median==0 else (max(values)-min(values))/median,
        "min_ms": min(values),
        "max_ms": max(values),
    }


def verify(report_path):
    report=json.loads(Path(report_path).read_text())
    differences=report.get("different_pixels",{})
    if isinstance(differences,dict) and any(int(v) for v in differences.values()):
        raise AssertionError(f"{report_path}: exact image comparison failed: {differences}")
    repeats=report.get("repeat_different_pixels",{})
    if isinstance(repeats,dict) and any(int(v) for v in repeats.values()):
        raise AssertionError(f"{report_path}: repeated image changed: {repeats}")

    cases=[]
    failed=[]
    for case in report.get("results",[]):
        samples=case["samples_ms"]
        before=sample_stats(samples["before"])
        after=sample_stats(samples["after"])
        ratio=after["median_ms"]/before["median_ms"] if before["median_ms"] else float("inf")
        # Software GLES hosts are noisy. The gate rejects a material slowdown,
        # while stability is compared both to an absolute ceiling and the paired
        # reference from the same process.
        allowed_spread=max(0.12,before["spread_ratio"]*1.75+0.02)
        stable=after["spread_ratio"]<=allowed_spread
        not_slower=ratio<=1.03
        row={
            "scene":case["scene"],
            "resolution":report.get("resolution"),
            "before":before,
            "after":after,
            "median_ratio_after_over_before":ratio,
            "speedup_before_over_after":(1.0/ratio if ratio else None),
            "allowed_after_spread_ratio":allowed_spread,
            "stable":stable,
            "not_slower":not_slower,
        }
        cases.append(row)
        if not stable or not not_slower:
            failed.append(row)
    if not cases:
        raise AssertionError(f"{report_path}: no timing cases")
    return {"report":str(report_path),"cases":cases,"failed":failed}


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("reports",nargs="+",type=Path)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    summaries=[verify(path) for path in args.reports]
    failed=[case for summary in summaries for case in summary["failed"]]
    result={
        "schema_version":1,
        "scope":"Paired host software GLES timing; exact frames; not Samsung Galaxy A16 FPS",
        "reports":summaries,
        "passed":not failed,
        "failed_cases":failed,
    }
    args.output.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({
        "passed":result["passed"],
        "cases":sum(len(s["cases"]) for s in summaries),
        "failed":len(failed),
        "speedups":[round(c["speedup_before_over_after"],3)
                    for s in summaries for c in s["cases"]],
        "spread_ratios":[round(c["after"]["spread_ratio"],4)
                         for s in summaries for c in s["cases"]],
    }))
    return 0 if result["passed"] else 1


if __name__=="__main__":
    raise SystemExit(main())
