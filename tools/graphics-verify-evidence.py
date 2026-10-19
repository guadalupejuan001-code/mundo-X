#!/usr/bin/env python3
"""Reject incomplete device evidence, even if `am instrument` exits with zero."""

import hashlib
import json
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def main():
    directory = Path(sys.argv[1] if len(sys.argv) > 1 else "graphics-evidence")
    output = (directory / "instrumentation.txt").read_text()
    require("MUNDOX_GRAPHICS_TESTS_PASSED" in output, "Instrumentation success marker missing")
    require("INSTRUMENTATION_CODE: -1" in output, "Instrumentation did not finish successfully")
    expected = {
        "native_bell_invariants", "renderer_borrows_native_state", "gles_one_million_objects",
        "gles_maximum_object_limit", "render_observes_without_mutating", "gate_changes_coupled_image",
        "gate_preserves_uncoupled_image", "selection_and_individual_edits", "edit_restoration",
        "pause_resume_preserves_state",
    }
    report = json.loads((directory / "report.json").read_text())
    tests = report["tests"]
    require(report["schemaVersion"] == 1, "Unsupported report schema")
    require(report["testsRun"] == len(expected) and report["testsFailed"] == 0, "Incomplete or failed tests")
    require(len(tests) == len(expected) and {test["name"] for test in tests} == expected,
            "Unexpected device test inventory")
    require(all(test["passed"] is True for test in tests), "Device tests contain failures")
    xml = ET.parse(directory / "junit.xml").getroot()
    require(int(xml.attrib["tests"]) == len(expected) and int(xml.attrib["failures"]) == 0,
            "JUnit report disagrees with JSON results")
    metrics = report["metrics"]
    require(metrics["androidSdk"] == 35 and "x86_64" in metrics["abis"], "Wrong emulator API or ABI")
    require("swiftshader" in metrics["glRenderer"].lower(), "GLES driver is not SwiftShader")
    require(metrics["qubits"] == 20 and metrics["nativeStateBytes"] > 0, "Missing live native state")
    require(metrics["millionObjectCount"] == 1_000_000 and metrics["maximumObjectCount"] == 1_048_576,
            "Full scene limits were not tested")
    require(metrics["coupledChangedPixels"] > 20 and metrics["uncoupledChangedPixels"] == 0,
            "Quantum visual/control comparison failed")
    require(metrics["removedIdPixels"] == 0 and metrics["pauseResumeChangedPixels"] == 0,
            "Selection or lifecycle comparison failed")
    required_images = {
        "million-color.png", "million-ids.png", "maximum-color.png", "maximum-ids.png",
        "coupling-on-before.png", "coupling-on-after.png", "coupling-off-before.png",
        "coupling-off-after.png", "selection-before.png", "selection-highlight.png",
        "selection-taller.png", "selection-removed.png", "edit-restored.png",
        "lifecycle-before.png", "lifecycle-after.png", "activity.png",
    }
    require(required_images.issubset(set(report["images"])), "Framebuffer or activity evidence missing")
    manifest = {}
    for name in sorted(required_images):
        data = (directory / name).read_bytes()
        require(data[:8] == b"\x89PNG\r\n\x1a\n" and len(data) > 32, f"Invalid PNG: {name}")
        width, height = struct.unpack(">II", data[16:24])
        require(width > 0 and height > 0, f"Empty image: {name}")
        manifest[name] = {"width": width, "height": height, "sha256": hashlib.sha256(data).hexdigest()}
    (directory / "verified-images.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Verified {len(tests)} Android device tests and {len(manifest)} PNG captures.")
    print(f"Driver: {metrics['glRenderer']}")
    print(f"Gate changed {metrics['coupledChangedPixels']} pixels with coupling; "
          f"{metrics['uncoupledChangedPixels']} without coupling.")
    print("These are software-emulator correctness results, not Samsung A16 performance measurements.")


if __name__ == "__main__":
    main()
