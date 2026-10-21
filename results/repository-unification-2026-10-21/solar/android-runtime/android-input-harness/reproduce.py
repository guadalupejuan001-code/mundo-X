#!/usr/bin/env python3
"""Rebuild this diagnostic from unchanged production Java, without GLES/Gradle.

Supply JDK 17, the Android API 35 jar, and an existing compatible host JNI.
Generated classes and logs go only under --work, outside repository sources.
"""
import argparse
import hashlib
import json
import subprocess
import time
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("repo", "jdk", "android-jar", "jni-dir", "work"):
        p.add_argument("--" + name, type=Path, required=True)
    a = p.parse_args()
    for name in ("repo", "jdk", "android_jar", "jni_dir", "work"):
        setattr(a, name, getattr(a, name).resolve())
    here = Path(__file__).resolve().parent
    manifest = json.loads((here / "manifest.json").read_text())
    sources = []
    for rel, expected in manifest["production_sources"].items():
        src = a.repo / rel
        if sha(src) != expected["repository_sha256"]:
            raise SystemExit("Production source changed: " + rel)
        sources.append(src)
    for rel, expected in manifest["harness_files"].items():
        src = here / rel
        if sha(src) != expected:
            raise SystemExit("Harness source changed: " + rel)
        sources.append(src)
    sources.append(a.repo / "solar-android/tests/SolarOcclusion.java")
    a.work.mkdir(parents=True, exist_ok=True)
    classes = a.work / "classes"
    classes.mkdir(exist_ok=True)
    output = a.work / "output"
    compile_cmd = [str(a.jdk / "bin/javac"), "-encoding", "UTF-8", "-cp", str(a.android_jar), "-d", str(classes)] + [str(v) for v in sources]
    run_cmd = [str(a.jdk / "bin/java"), "-Xms32m", "-Xmx256m", "-XX:+UseSerialGC", "-XX:ActiveProcessorCount=1", "-XX:CompressedClassSpaceSize=64m", "-XX:ReservedCodeCacheSize=64m", "-Djava.library.path=" + str(a.jni_dir), "-cp", str(classes) + ":" + str(a.android_jar), "com.mundox.solar.AndroidInputFixture", str(a.repo / "solar-android/app/src/main/assets"), str(output)]
    record = {
        "scope": "Unchanged production Java preparation only; no GLES/Gradle/Android process",
        "jni_sha256": sha(a.jni_dir / "libmx_graphics.so"),
        "production_source_sha256": {str(v.relative_to(a.repo)): sha(v) for v in sources if v.is_relative_to(a.repo)},
        "steps": [],
    }
    for label, cmd in (("compile", compile_cmd), ("run", run_cmd)):
        begin = time.monotonic()
        result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=a.work, timeout=45)
        (a.work / (label + ".log")).write_text(result.stdout)
        record["steps"].append({"name": label, "command": cmd, "returncode": result.returncode, "wall_seconds": time.monotonic() - begin})
        if result.returncode:
            (a.work / "reproduction.json").write_text(json.dumps(record, indent=2) + "\n")
            raise SystemExit(label + " failed; inspect " + str(a.work / (label + ".log")))
    android = a.repo / "results/repository-unification-2026-10-21/solar/android-runtime/after/solar-performance/results.json"
    expected = json.loads(android.read_text())["cases"][7]["inputSha256"]
    actual = json.loads((output / "case-7-input-hashes.json").read_text())
    record["android_results_sha256"] = sha(android)
    record["case7"] = {
        "field_count": len(expected),
        "matched": [key for key in expected if actual[key] == expected[key]],
        "differed": {key: {"host": actual[key], "android": expected[key]} for key in expected if actual[key] != expected[key]},
    }
    # Report the whole comparison. The shader/overlay packet has no orbitCache
    # double array uniform: the independently hashed float lines buffer is used.
    record["case7"]["all_recorded_fields_exact"] = actual == expected
    record["case7"]["shader_and_overlay_packet_exact"] = all(actual[key] == expected[key] for key in expected if key != "orbitCache")
    record["fixture_sha256"] = sha(output / "fixture-android-case-7.json")
    record["original_fixture_sha256"] = manifest["outputs"]["output/fixture-android-case-7.json"]["sha256"]
    (a.work / "reproduction.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps({"case7_matching_fields": len(record["case7"]["matched"]), "case7_total_fields": len(expected), "case7_different_fields": list(record["case7"]["differed"]), "fixture_sha256": record["fixture_sha256"]}))
    if not record["case7"]["shader_and_overlay_packet_exact"] or record["fixture_sha256"] != record["original_fixture_sha256"]:
        raise SystemExit("The shader/overlay input packet did not reproduce exactly")


if __name__ == "__main__":
    main()
