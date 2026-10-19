#!/usr/bin/env python3
"""Reject incomplete Android runs and inventory genuine captured framebuffers."""

import hashlib
import json
import re
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def main():
    directory = Path(sys.argv[1] if len(sys.argv) > 1 else "solar-evidence")
    output = (directory / "instrumentation.txt").read_text()
    require("COSMIC_SOLAR_TESTS_PASSED" in output, "Solar instrumentation success marker missing")
    require("INSTRUMENTATION_CODE: -1" in output, "Solar instrumentation did not finish successfully")
    require((directory / "immersive-tutorial-setting.txt").read_text().strip() == "confirmed",
            "The isolated emulator full-screen tutorial was not acknowledged")
    report = json.loads((directory / "report.json").read_text())
    require(report.get("schemaVersion") == 1, "Unsupported solar evidence schema")
    tests = report["tests"]
    names = [test["name"] for test in tests]
    expected_tests = {
        "native_gles_launch_and_textures", "astronomy_and_magnetic_classification",
        "native_bell_and_readonly_observation", "quantum_coupled_and_uncoupled_frames",
        "intrinsic_and_induced_field_frames", "solar_wind_layer", "saturn_rings_and_moon_navigation",
        "forward_reverse_and_paused_time", "real_scale_earth_focus",
        "camera_rotation_zoom_pan_and_flight", "resolution_budget",
        "background_resume_preserves_state", "recreation_and_orientation_preserve_state",
    }
    require(len(tests) == len(expected_tests) and set(names) == expected_tests,
            "Incomplete or unexpected solar test inventory")
    require(report["testsRun"] == len(tests) and report["testsFailed"] == 0,
            "The solar report contains missing or failed tests")
    require(all(test["passed"] is True for test in tests), "A solar device test failed")
    junit = ET.parse(directory / "junit.xml").getroot()
    require(int(junit.attrib["tests"]) == len(expected_tests) and int(junit.attrib["failures"]) == 0,
            "The JUnit report disagrees with device JSON")
    metrics = report["metrics"]
    require(metrics["androidSdk"] == 35 and "x86_64" in metrics["abis"],
            "The device does not match the Android 15 x86_64 validation target")
    require("swiftshader" in metrics["glRenderer"].lower(), "Unexpected GLES validation driver")
    require(metrics["nativeQubits"] == 8 and metrics["nativeStateBytes"] > 0,
            "The actual COSMIC native core was not observed on Android")
    require(metrics["framesRendered"] > 0, "No actual GLES frames were rendered")
    require(metrics["initialWindowFocused"] is True, "The launched Activity did not receive input focus")
    require(metrics["bodyCount"] == 23 and metrics["planetCount"] == 8 and metrics["moonCount"] == 14,
            "Astronomical body inventory is incomplete")
    require(.98 < metrics["earthJ2000DistanceAU"] < 1.02 and 350_000 < metrics["earthMoonDistanceKm"] < 410_000,
            "Packaged astronomical geometry failed its independent scale checks")
    require(metrics["fieldTypes"] == {"earth": 1, "venus": 2, "mars": 3, "moon": 0, "ganymede": 1},
            "Intrinsic, induced and crustal magnetic environments were not distinguished")
    textures = metrics["packagedNasaTextures"]
    require(len(textures) == 3 and {entry["asset"] for entry in textures}
            == {"textures/earth.png", "textures/moon.jpg", "textures/jupiter.jpg"}
            and all(entry["width"] >= 512 and entry["height"] >= 256 for entry in textures)
            and metrics["uploadedNasaTextures"] == 3, "Packaged NASA maps were not decoded and uploaded to GLES")
    bell = metrics["bellExpectations"]
    require(len(bell) == 8 and all(abs(actual - expected) < 1e-6
            for actual, expected in zip(bell, (0, 0, 0, 1, -1, 1, 1, 1)))
            and metrics["observationPreservedNativeRevision"] is True,
            "The original native core did not preserve Bell/read-only contracts")
    require(metrics["deterministicPausedChangedPixels"] == 0 and metrics["coupledChangedPixels"] > 20
            and metrics["uncoupledChangedPixels"] == 0 and metrics["quantumPreparationOrbitalDeltaAU"] == 0,
            "Quantum visual coupling changed the wrong state or lacked causal pixel evidence")
    field_deltas = metrics["fieldToggleChangedPixels"]
    require(set(field_deltas) == {"earth", "venus", "mars"} and all(value > 20 for value in field_deltas.values())
            and metrics["windToggleChangedPixels"] > 20, "Actual magnetic/wind layer comparisons failed")
    require(metrics["saturnBrightPixels"] > 80 and metrics["moonBrightPixels"] > 80
            and metrics["navigationDestinationChangedPixels"] > 100 and metrics["systemOverviewRendered"] is True,
            "Live Saturn, Moon or overview navigation evidence is missing")
    require(metrics["forwardDaysAdvanced"] > 0 and metrics["reverseDaysAdvanced"] < 0
            and metrics["pausedDatePreserved"] is True and metrics["dateBoundsEnforced"] is True,
            "Time controls or model date validity failed")
    require(metrics["realScaleEarthBrightPixels"] > 80, "Earth disappeared when physical scale was enabled")
    camera = metrics["cameraChangedPixels"]
    require(set(camera) == {"rotation", "zoom", "pan", "flight"} and all(value > 50 for value in camera.values()),
            "Camera controls were not all exercised on actual framebuffers")
    low, high = metrics["lowResolution"], metrics["highResolution"]
    require(len(low) == len(high) == 2 and min(low) > 0 and min(high) > 0
            and max(low) <= 480 and max(high) <= 960 and max(high) > max(low),
            "Quality controls did not bound and resize the actual framebuffer")
    require(metrics["backgroundResumeChangedPixels"] == 0 and metrics["backgroundPreservedNativeOwner"] is True
            and metrics["recreationChangedPixels"] == 0 and metrics["recreationPreservedPreset"] == 3
            and metrics["orientationRoundtripPassed"] is True,
            "Android lifecycle lost the native preparation or the saved focused scene")

    expected_apks = {"COSMIC-X-Solar.apk", "COSMIC-X-Solar-tests.apk"}
    checksums = {}
    for row in (directory / "apk-SHA256SUMS.txt").read_text().splitlines():
        parts = row.split()
        require(len(parts) == 2, "Malformed APK checksum entry")
        checksum, name = parts
        require(name in expected_apks and name not in checksums and re.fullmatch(r"[0-9a-f]{64}", checksum),
                "Unexpected solar APK checksum inventory")
        checksums[name] = checksum
    require(set(checksums) == expected_apks, "Missing solar APK checksums")
    provenance = json.loads((directory / "build-provenance.json").read_text())
    require(provenance["buildCommit"] == (directory / "build-commit.txt").read_text().strip(),
            "APK and emulator source evidence disagree")
    require(all(provenance["apks"][name]["sha256"] == checksum for name, checksum in checksums.items()),
            "APK checksums disagree with source provenance")

    images = report["images"]
    expected_images = {
        "earth-surface.png", "earth-fields-base.png", "earth-fields-entangled.png",
        "venus-magnetic-environment.png", "mars-magnetic-environment.png", "sun-solar-wind.png",
        "saturn-rings.png", "moon-surface.png", "solar-system-overview.png", "earth-real-scale.png",
        "activity-portrait.png", "activity-landscape.png",
    }
    require(expected_images.issubset(set(images)) and len(set(images)) == len(images),
            "Missing or duplicate required solar framebuffers")
    image_manifest = {}
    for name in sorted(images):
        require(re.fullmatch(r"[A-Za-z0-9_-]+\.png", name), "Invalid framebuffer filename")
        data = (directory / name).read_bytes()
        require(data[:8] == b"\x89PNG\r\n\x1a\n" and len(data) > 32, "Invalid PNG: " + name)
        width, height = struct.unpack(">II", data[16:24])
        require(width >= 64 and height >= 64, "Framebuffer dimensions are insufficient: " + name)
        image_manifest[name] = {"width": width, "height": height, "bytes": len(data),
                                "sha256": hashlib.sha256(data).hexdigest()}
    require(len({item["sha256"] for item in image_manifest.values()}) >= 3,
            "Distinct scenes did not produce distinct captured images")
    portrait, landscape = image_manifest["activity-portrait.png"], image_manifest["activity-landscape.png"]
    require(portrait["height"] > portrait["width"] and landscape["width"] > landscape["height"],
            "Actual Activity screenshots do not verify both display orientations")
    (directory / "verified-images.json").write_text(json.dumps(image_manifest, indent=2) + "\n")
    print(f"Verified {len(tests)} solar Android tests and {len(images)} actual PNG captures.")
    print("Driver: " + metrics["glRenderer"])
    print(f"Native state: {metrics['nativeQubits']} qubits; {metrics['nativeStateBytes']} bytes.")
    print("These are Android software-emulator correctness results, not Samsung A16 performance measurements.")


if __name__ == "__main__":
    main()
