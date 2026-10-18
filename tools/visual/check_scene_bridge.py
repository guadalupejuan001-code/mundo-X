#!/usr/bin/env python3
"""Compare real Java/C++ scene models, streaming every binary32 transform bit.

Uses only Python's standard library plus a C++17 compiler and a Java compiler.
The small host probes call production models; this file does not reimplement
the scene hash or transform mathematics. Temporary binary streams are compared
in 64 KiB chunks, never loaded as population-sized arrays in Python or a model.
"""

import argparse
import hashlib
import json
import math
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass


ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent
MAGIC = b"MXBRIDGE1\n"
MAX_VERSION = (1 << 63) - 1
CHUNK_BYTES = 65536


def scene_text(count, seed, version, edits):
    lines = ["MUNDOX_OBJECTS_V1", "count=" + str(count), "seed=" + str(seed),
             "version=" + str(version), "edits=" + str(len(edits))]
    lines.extend(str(key) + ":" + str(value) for key, value in sorted(edits.items()))
    return "\n".join(lines) + "\n"


@dataclass
class Fixture:
    name: str
    count: int
    seed: int
    initial: str
    canonical_initial: str
    operations: str
    responses: str
    final: str
    final_edit_count: int


def fixture(name, count, seed, version=0, edits=None, operations=(), responses=(),
            final_edits=None, final_version=None, input_text=None):
    edits = {} if edits is None else edits
    final_edits = edits if final_edits is None else final_edits
    final_version = version if final_version is None else final_version
    canonical = scene_text(count, seed, version, edits)
    assert len(operations) == len(responses), name
    return Fixture(name, count, seed, canonical if input_text is None else input_text,
                   canonical, "".join(line + "\n" for line in operations),
                   "".join(line + "\n" for line in responses),
                   scene_text(count, seed, final_version, final_edits), len(final_edits))


def fixtures():
    maximum_edits = {i: 1 + (i % 7) for i in range(4096)}
    return [
        fixture("singleton_restore_and_noop", 1, 0,
                operations=["edit 0 1", "edit 0 1", "edit 0 7", "edit 0 0", "clear"],
                responses=["changed", "unchanged", "changed", "changed", "unchanged"],
                final_version=3),
        fixture("nonsquare_negative_seed", 1003, -2026, 8, {0: 7, 11: 6, 1002: 1},
                operations=["edit 0 0", "edit 11 7", "edit 12 2"],
                responses=["changed"] * 3,
                final_edits={11: 7, 12: 2, 1002: 1}, final_version=11),
        fixture("before_16bit_boundary", 65535, -2147483648, 4,
                {0: 1, 16383: 6, 16384: 7, 65534: 3}),
        fixture("at_16bit_boundary", 65536, 2147483647, 4,
                {0: 7, 16383: 2, 16384: 5, 65535: 6}),
        fixture("after_16bit_boundary", 65537, -1, 4,
                {0: 6, 16384: 3, 65535: 1, 65536: 7},
                operations=["edit 65536 4", "edit 65535 0"],
                responses=["changed", "changed"],
                final_edits={0: 6, 16384: 3, 65536: 4}, final_version=6),
        fixture("million_every_id", 1048576, 2026, 6,
                {0: 7, 16383: 6, 16384: 1, 65535: 7, 65536: 3, 1048575: 6},
                operations=["edit 0 0", "edit 65535 5", "edit 1048575 7"],
                responses=["changed"] * 3,
                final_edits={16383: 6, 16384: 1, 65535: 5, 65536: 3, 1048575: 7},
                final_version=9),
        fixture("exhausted_revision_keeps_state", 129, -2147483648, MAX_VERSION,
                {0: 1, 64: 7},
                operations=["edit 0 1", "edit 2 0", "edit 0 6", "edit 64 0",
                            "edit 2 3", "clear"],
                responses=["unchanged", "unchanged"] + ["rejected"] * 4),
        fixture("exhausted_revision_empty_noops", 1, 2147483647, MAX_VERSION,
                operations=["edit 0 0", "clear", "edit 0 1"],
                responses=["unchanged", "unchanged", "rejected"]),
        fixture("sparse_capacity_remove_reuse_clear", 100003, -2026, 4096, maximum_edits,
                operations=["edit 4096 7", "edit 2 0", "edit 4096 7", "edit 0 6", "clear", "clear"],
                responses=["rejected", "changed", "changed", "changed", "changed", "unchanged"],
                final_edits={}, final_version=4100),
        fixture("accepted_decimal_canonicalization", 17, 0, 12, {0: 1, 16: 7},
                input_text="MUNDOX_OBJECTS_V1\ncount=00017\nseed=-0\nversion=00012\nedits=0002\n000:01\n016:07\n"),
        fixture("optional_final_newline", 257, -2147483648, 1, {256: 7},
                input_text=scene_text(257, -2147483648, 1, {256: 7}).rstrip("\n")),
    ]


def run_checked(command, **kwargs):
    result = subprocess.run(command, check=False, stderr=subprocess.PIPE, timeout=120, **kwargs)
    if result.returncode:
        raise RuntimeError("command failed: " + str(command[0]) + "\n" +
                           result.stderr.decode("utf-8", errors="replace")[-6000:])
    return result


def compile_probes(build, args):
    compiler = shutil.which(args.cxx)
    java = shutil.which(args.java)
    if not compiler or not java:
        raise RuntimeError("a C++17 compiler and Java runtime must be on PATH")
    native = build / ("scene-bridge.exe" if sys.platform == "win32" else "scene-bridge")
    run_checked([compiler, "-std=c++17", "-O3", "-Wall", "-Wextra", "-Wpedantic",
                 "-I", str(ROOT / "native/include"), str(HERE / "scene_bridge_probe.cpp"),
                 "-o", str(native)], stdout=subprocess.PIPE)
    classes = build / "classes"
    classes.mkdir()
    sources = [str(ROOT / "app/src/main/java/com/mundox/lab/ObjectSceneModel.java"),
               str(HERE / "SceneBridgeProbe.java")]
    if args.ecj:
        ecj = pathlib.Path(args.ecj).resolve(strict=True)
        java_compile = [java, "-jar", str(ecj), "-8", "-proc:none", "-warn:none"]
        java_compiler = "ECJ " + ecj.name
    else:
        javac = shutil.which(args.javac)
        if not javac:
            raise RuntimeError("javac is unavailable; supply --ecj /path/to/ecj.jar")
        java_compile = [javac, "-source", "8", "-target", "8", "-Xlint:-options"]
        java_compiler = "javac"
    run_checked(java_compile + ["-d", str(classes)] + sources, stdout=subprocess.PIPE)
    return [str(native)], [java, "-cp", str(classes), "SceneBridgeProbe"], java_compiler


def read_exact(stream, size):
    data = stream.read(size)
    if len(data) != size:
        raise AssertionError("truncated probe stream: expected " + str(size) + " bytes")
    return data


def read_blob(stream):
    size = struct.unpack(">I", read_exact(stream, 4))[0]
    if size > 65536:
        raise AssertionError("unexpected oversized probe header")
    return read_exact(stream, size)


def compare_case(case, native_command, java_command, build):
    source = build / "scene.mxscene"
    source.write_text(case.initial, encoding="utf-8", newline="")
    operations = build / "operations.txt"
    operations.write_text(case.operations, encoding="utf-8", newline="")
    binary_paths = [build / "cpp.bin", build / "java.bin"]
    for command, destination in zip([native_command, java_command], binary_paths):
        with destination.open("wb") as output:
            run_checked(command + [str(source), str(operations)], stdout=output)

    digests = [hashlib.sha256(), hashlib.sha256()]
    with binary_paths[0].open("rb") as cpp, binary_paths[1].open("rb") as java:
        for stream in (cpp, java):
            if read_exact(stream, len(MAGIC)) != MAGIC:
                raise AssertionError("invalid probe protocol")
            for expected, name in [(case.canonical_initial, "initial canonical export"),
                                   (case.responses, "mutation/rejection results"),
                                   (case.final, "final canonical export")]:
                actual = read_blob(stream).decode("utf-8")
                if actual != expected:
                    raise AssertionError(case.name + ": " + name + " differs from fixture")
            count, side, logical_bytes = struct.unpack(">IIQ", read_exact(stream, 16))
            expected_side = math.isqrt(case.count - 1) + 1
            if (count, side, logical_bytes) != (case.count, expected_side, 32 + 8 * case.final_edit_count):
                raise AssertionError(case.name + ": metadata mismatch")
        offset = 0
        total = case.count * 16
        while offset < total:
            size = min(CHUNK_BYTES, total - offset)
            left, right = read_exact(cpp, size), read_exact(java, size)
            if left != right:
                first = next(i for i, pair in enumerate(zip(left, right)) if pair[0] != pair[1])
                position = offset + first
                component = ["x", "z", "height", "tone"][(position // 4) % 4]
                start = (first // 4) * 4
                raise AssertionError("{}: ID {} {} differs: C++={} Java={}".format(
                    case.name, position // 16, component,
                    left[start:start + 4].hex(), right[start:start + 4].hex()))
            digests[0].update(left)
            digests[1].update(right)
            offset += size
        if cpp.read(1) or java.read(1):
            raise AssertionError(case.name + ": unexpected trailing probe bytes")
    return {"name": case.name, "count": case.count, "seed": case.seed,
            "checked_ids": case.count, "checked_float32_values": case.count * 4,
            "transform_bytes": total, "different_bytes": 0,
            "transform_sha256_cpp": digests[0].hexdigest(),
            "transform_sha256_java": digests[1].hexdigest(),
            "canonical_scene_sha256": hashlib.sha256(case.final.encode("utf-8")).hexdigest(),
            "operations": len(case.operations.splitlines()), "passed": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ecj", help="compile Java with this Eclipse compiler JAR instead of javac")
    parser.add_argument("--cxx", default="g++", help="C++17 compiler, default: g++")
    parser.add_argument("--java", default="java", help="Java runtime, default: java")
    parser.add_argument("--javac", default="javac", help="Java compiler when --ecj is absent")
    parser.add_argument("--output", type=pathlib.Path, help="write the JSON evidence report here")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="mundox-visual-check-") as temporary:
        build = pathlib.Path(temporary)
        native, java, java_compiler = compile_probes(build, args)
        results = []
        for case in fixtures():
            result = compare_case(case, native, java, build)
            results.append(result)
            print("PASS {}: {} IDs, exact float32 bits".format(case.name, case.count), file=sys.stderr)
    sources = ["native/include/mundo_x/visual_scene.hpp",
               "app/src/main/java/com/mundox/lab/ObjectSceneModel.java"]
    report = {"format": "MUNDOX_VISUAL_BRIDGE_CHECK_V1", "passed": True,
              "comparison": "all transform bytes, plus canonical imports/exports and expected edit results",
              "float_stream_format": "x,z,height,tone per ID in ascending order, IEEE754 binary32 big-endian",
              "memory_method": "four-float probes, temporary disk streams, 64 KiB comparison chunks",
              "cxx": args.cxx, "java_compiler": java_compiler,
              "source_sha256": {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in sources},
              "cases": results, "checked_ids_total": sum(item["checked_ids"] for item in results),
              "different_bytes_total": 0,
              "scope": "scene representation compatibility; no GPU, Android-device or performance claim"}
    output = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf-8")
    print(output, end="")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AssertionError, OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print("FAIL scene bridge: " + str(error), file=sys.stderr)
        sys.exit(1)
