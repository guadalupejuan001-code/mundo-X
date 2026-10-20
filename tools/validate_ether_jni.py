#!/usr/bin/env python3
"""Build the production JNI bridge and exercise shared Ether state on a host JVM.

Requires a Linux C++17 compiler and Java 17 with JNI headers. This deliberately
does not claim Android ABI, Activity, GPU or APK validation.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--jni-include", type=Path)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    if args.jni_include:
        headers = args.jni_include.resolve()
    else:
        settings = subprocess.run(["java", "-XshowSettings:properties", "-version"],
                                  text=True, capture_output=True, check=True)
        match = re.search(r"^\s*java.home\s*=\s*(.+)$", settings.stderr, re.MULTILINE)
        if not match:
            raise RuntimeError("Cannot identify the Java installation")
        headers = Path(match.group(1).strip()) / "include"
    if not (headers / "jni.h").is_file() or not (headers / "linux/jni_md.h").is_file():
        raise RuntimeError("Install JDK 17 headers or provide --jni-include")
    compiler = [args.cxx, "-std=c++17", "-O2", "-Wall", "-Wextra", "-fPIC", "-shared", "-pthread",
                "-I", str(root / "native/include"), "-I", str(headers), "-I", str(headers / "linux"),
                str(root / "native/src/graphics_core_bridge.cpp"),
                str(root / "native/src/infinite_bridge.cpp"),
                str(root / "graphics-android/app/src/main/cpp/graphics_jni.cpp"),
                str(root / "tests/java/ether_fusion_jni.cpp"), "-o", str(build / "libmx_graphics.so")]
    with (build / "build.log").open("w") as log:
        subprocess.run(compiler, cwd=root, check=True, stdout=log, stderr=subprocess.STDOUT)
        javac = [shutil.which("javac")] if shutil.which("javac") else ["java", "com.sun.tools.javac.Main"]
        java_dir = root / "graphics-android/app/src/main/java/com/mundox/graphics"
        sources = [java_dir / (name + ".java") for name in ("NativeCore", "InfiniteProgram", "EtherCoreNative")]
        sources += [root / "solar-android/app/src/main/java/com/mundox/solar/QuantumSolarBridge.java",
                    root / "tests/java/com/mundox/graphics/EtherFusionJni.java"]
        subprocess.run([*javac, "-d", str(build / "classes"), *map(str, sources)],
                       cwd=root, check=True, stdout=log, stderr=subprocess.STDOUT)
    run = subprocess.run(["java", "-Xcheck:jni", "-Djava.library.path=" + str(build),
                          "-cp", str(build / "classes"), "com.mundox.graphics.EtherFusionJni"],
                         cwd=root, text=True, capture_output=True)
    (build / "runtime.log").write_text(run.stdout + run.stderr)
    if run.returncode:
        raise RuntimeError("JNI validation failed:\n" + run.stdout + run.stderr)
    if "WARNING" in run.stdout or run.stderr:
        raise RuntimeError("JNI runtime reported a warning:\n" + run.stdout + run.stderr)
    result = json.loads(run.stdout)
    if result.get("status") != "passed" or result.get("checks", 0) < 50:
        raise RuntimeError("Incomplete JNI test result")
    result["javaVersion"] = subprocess.check_output(["java", "-version"], stderr=subprocess.STDOUT, text=True).splitlines()[0]
    result["compilerVersion"] = subprocess.check_output([args.cxx, "--version"], text=True).splitlines()[0]
    (build / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
