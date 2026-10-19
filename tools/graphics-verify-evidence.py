#!/usr/bin/env python3
"""Reject incomplete device evidence, even if `am instrument` exits with zero."""

import hashlib
import json
import re
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from cosmic_source_provenance import verify_integrated_core


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def main():
    directory = Path(sys.argv[1] if len(sys.argv) > 1 else "graphics-evidence")
    ultra = len(sys.argv) > 2 and sys.argv[2] == "--ultra8k"
    uhd = ultra or (len(sys.argv) > 2 and sys.argv[2] == "--uhd4k")
    require(len(sys.argv) <= 3 and (len(sys.argv) < 3 or uhd), "Unexpected verification arguments")
    output = (directory / "instrumentation.txt").read_text()
    require("MUNDOX_GRAPHICS_TESTS_PASSED" in output, "Instrumentation success marker missing")
    require("INSTRUMENTATION_CODE: -1" in output, "Instrumentation did not finish successfully")
    expected = {
        "default_1000_qubit_world", "native_1000_qubit_invariants", "portable_1000_and_legacy_expansion",
        "native_bell_invariants", "renderer_borrows_native_state", "gles_one_million_objects",
        "gles_maximum_object_limit", "render_observes_without_mutating", "gate_changes_coupled_image",
        "gate_preserves_uncoupled_image", "selection_and_individual_edits", "edit_restoration",
        "pause_resume_preserves_state",
        "portable_codec_strict_validation", "portable_desktop_fixtures_import",
        "portable_mutation_export_reimport", "portable_invalid_import_preserves_session",
        "portable_activity_recreation",
    }
    if uhd:
        expected.update({"uhd_real_framebuffer_and_legacy_return", "uhd_id_selection_and_letterbox",
                         "uhd_orientation_roundtrip", "uhd_lifecycle_snapshot_native_preserved"})
    if ultra:
        expected.update({"ultra8k_real_framebuffer_and_budget_limits",
                         "ultra8k_tiles_match_monolithic_color_and_ids",
                         "ultra8k_streamed_png_dimensions_and_rows",
                         "ultra8k_failure_preserves_scene_and_native_state"})
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
    require(metrics.get("uhd4kSuite", False) is uhd, "Wrong UHD/legacy test profile")
    require(metrics.get("ultra8kSuite", False) is ultra, "Wrong 8K test profile")
    require(metrics["androidSdk"] == 35 and "x86_64" in metrics["abis"], "Wrong emulator API or ABI")
    require("swiftshader" in metrics["glRenderer"].lower(), "GLES driver is not SwiftShader")
    require(metrics["qubits"] == 1000 and metrics["nativeStateBytes"] > 0
            and metrics["defaultQubits"] == 1000 and metrics["defaultObservables"] == 3000, "Missing live 1000-qubit state")
    native = metrics["native1000"]
    require(native["qubits"] == 1000 and native["highQubits"] == [32, 53, 59, 64, 511, 999]
            and native["ghzExpectations"] == [1, 1, 1, 1, 1, 0, 0, 0] and native["snapshotRoundtrip"] is True
            and native["stateBytes"] > 0, "GHZ1000, high-bit gates or snapshot replay failed")
    require(metrics["legacyExpansionSourceQubits"] == [8, 20, 60] and metrics["expandedProjectQubits"] == 1000
            and metrics["legacyExpansionChangedPixels"] == 0 and metrics["legacyExpansionPreservedState"] is True
            and metrics["legacyExpansionAddedQubitsZero"] is True and metrics["legacyExpansionHiddenStatePreserved"] is True
            and metrics["legacyExpansionResolutionModes"] == [0, 1, 2]
            and metrics["legacyExpansionBlockedConcurrentGate"] is True and metrics["expandedProjectRoundtrip"] is True,
            "Legacy-to-1000 project expansion lost state or scene")
    require(metrics["millionObjectCount"] == 1_000_000 and metrics["maximumObjectCount"] == 1_048_576,
            "Full scene limits were not tested")
    require(metrics["coupledChangedPixels"] > 20 and metrics["uncoupledChangedPixels"] == 0,
            "Quantum visual/control comparison failed")
    require(metrics["removedIdPixels"] == 0 and metrics["pauseResumeChangedPixels"] == 0,
            "Selection or lifecycle comparison failed")
    codec = metrics["projectCodec"]
    require(codec["passed"] is True and codec["checks"] >= 99 and codec["rejectedInvalidDocuments"] >= 83
            and codec["nativeOrGlCreated"] is False and codec["expandedQubits"] == 1000
            and codec["legacyQubitCounts"] == [2, 8, 20, 60], "Strict portable codec checks incomplete")
    fixtures = {item["fixture"]: item for item in metrics["portableFixtures"]}
    require(len(metrics["portableFixtures"]) == 2 and set(fixtures) == {"bell-observer.cosmic", "ghz-city.cosmic"},
            "Desktop fixture import inventory incomplete")
    bell, ghz = fixtures["bell-observer.cosmic"], fixtures["ghz-city.cosmic"]
    require(bell["qubits"] == 2 and bell["objects"] == 1024 and bell["observables"] == 7
            and bell["seed"] == 2026 and bell["coupling"] == .25 and bell["cameraMode"] == "lookAt",
            "Bell desktop fixture lost its native or graphic parameters")
    require(ghz["qubits"] == 3 and ghz["objects"] == 1_000_000 and ghz["observables"] == 11
            and ghz["seed"] == 4_000_000_001 and ghz["coupling"] == -.125 and ghz["cameraMode"] == "orbit",
            "GHZ fixture lost unsigned seed, negative coupling or original operators")
    require(all(item["passed"] is True and item["visiblePixels"] > 100 for item in fixtures.values()),
            "Imported fixtures did not produce real GLES output")
    require(metrics["portableRoundtripChangedPixels"] == 0 and metrics["portableRoundtripIdDifferences"] == 0
            and metrics["portableEditChangedPixels"] > 10 and metrics["portableCameraPreservedCore"] is True,
            "Portable mutation/reimport contract failed")
    require(metrics["portableRejectedActiveImports"] == 3 and metrics["portableInvalidImportChangedPixels"] == 0
            and metrics["portableRejectedRevisionOperations"] == 2 and metrics["portableRejectedTimeOperations"] == 2,
            "Invalid import or portable mutation limit altered the active state")
    require(metrics["portableRecreateChangedPixels"] == 0 and metrics["portableRecreateNewOwner"] is True
            and metrics["portableRecreateRestoredCoupling"] == -.125
            and metrics["portableFinalQubits"] == 3 and metrics["portableFinalStateBytes"] > 0,
            "Activity recreation did not preserve the imported project and custom coupling")
    expected_apks = {"COSMIC-X-Engine.apk", "COSMIC-X-Engine-tests.apk"}
    if uhd:
        expected_apks = {"COSMIC-X-Engine-4K.apk", "COSMIC-X-Engine-4K-tests.apk", "COSMIC-X-Engine-coexist.apk"}
    if ultra:
        expected_apks = {"COSMIC-X-Engine-8K.apk", "COSMIC-X-Engine-8K-tests.apk", "COSMIC-X-Engine-coexist.apk"}
    checksums = {}
    for line in (directory / "apk-SHA256SUMS.txt").read_text().splitlines():
        parts = line.split()
        require(len(parts) == 2, "Malformed APK checksum entry")
        checksum, name = parts
        require(name in expected_apks and name not in checksums and re.fullmatch(r"[0-9a-f]{64}", checksum),
                "Wrong COSMIC APK checksum inventory")
        checksums[name] = checksum
    require(set(checksums) == expected_apks, "Missing COSMIC APK checksums")
    required_images = {
        "million-color.png", "million-ids.png", "maximum-color.png", "maximum-ids.png",
        "coupling-on-before.png", "coupling-on-after.png", "coupling-off-before.png",
        "coupling-off-after.png", "selection-before.png", "selection-highlight.png",
        "selection-taller.png", "selection-removed.png", "edit-restored.png",
        "lifecycle-before.png", "lifecycle-after.png", "activity.png",
        "portable-bell-color.png", "portable-bell-ids.png", "portable-ghz-color.png", "portable-ghz-ids.png",
        "portable-roundtrip-before.png", "portable-roundtrip-after.png",
        "portable-recreate-before.png", "portable-recreate-after.png",
    }
    uhd_images = {
        "uhd-million-portrait.png", "uhd-portrait-ids.png", "uhd-selection.png", "uhd-letterbox-rejected.png",
        "uhd-portrait-before-rotation.png", "uhd-landscape.png", "uhd-portrait-after-rotation.png",
        "uhd-lifecycle-before.png", "uhd-after-resume.png", "uhd-after-recreate.png",
    }
    if uhd:
        required_images.update(uhd_images | {"uhd-legacy-before.png", "uhd-legacy-after.png",
                                            "uhd-activity-portrait.png", "uhd-activity-landscape.png"})
    require(required_images.issubset(set(report["images"])), "Framebuffer or activity evidence missing")
    manifest = {}
    for name in sorted(required_images):
        data = (directory / name).read_bytes()
        require(data[:8] == b"\x89PNG\r\n\x1a\n" and len(data) > 32, f"Invalid PNG: {name}")
        width, height = struct.unpack(">II", data[16:24])
        require(width > 0 and height > 0, f"Empty image: {name}")
        manifest[name] = {"width": width, "height": height, "sha256": hashlib.sha256(data).hexdigest()}
    (directory / "verified-images.json").write_text(json.dumps(manifest, indent=2) + "\n")
    if uhd:
        expected_package = "com.mundox.graphics.ultra" if ultra else "com.mundox.graphics.uhd"
        require(metrics["applicationId"] == expected_package, "UHD APK used the wrong application identity")
        require(metrics["uhdMillionObjectCount"] == 1_000_000 and metrics["uhdPixels"] == 8_294_400,
                "The UHD test did not retain the original million-object scene")
        require(metrics["uhdRejectedInsufficientLimits"] == 4, "A GPU limit was ignored")
        require((metrics["uhdPortraitWidth"], metrics["uhdPortraitHeight"]) == (2160, 3840)
                and (metrics["uhdLandscapeWidth"], metrics["uhdLandscapeHeight"]) == (3840, 2160),
                "Actual framebuffers are not UHD in both orientations")
        require(metrics["uhdLegacyReturnChangedPixels"] == 0 and metrics["uhdOrientationChangedPixels"] == 0
                and metrics["uhdPauseResumeChangedPixels"] == 0 and metrics["uhdRecreateChangedPixels"] == 0,
                "UHD resolution or lifecycle roundtrip changed exact pixels")
        require(metrics["uhdNativeOwnerPreserved"] is True and metrics["uhdSnapshotSerialized"] is True
                and metrics["uhdRecreatedNativeStatePreserved"] is True,
                "UHD observation changed or failed to preserve the original native core")
        require(metrics["uhdSelectedId"] == 0 and metrics["uhdLetterboxRejected"] is True,
                "UHD ID selection or letterbox mapping failed")
        for name in uhd_images:
            item = manifest[name]
            expected_size = (3840, 2160) if name == "uhd-landscape.png" else (2160, 3840)
            require((item["width"], item["height"]) == expected_size, "Wrong UHD PNG dimensions: " + name)
            samples = report["captureSamples"][name]
            require(samples["decodedPngSamplesVerified"] is True and len(samples["points"]) == 25
                    and (samples["width"], samples["height"]) == expected_size
                    and re.fullmatch(r"[0-9a-f]{64}", samples["rgbaSha256"]),
                    "The shipping PNG exporter did not preserve sampled RGBA/orientation: " + name)
        for before, after in (("uhd-portrait-before-rotation.png", "uhd-portrait-after-rotation.png"),
                              ("uhd-lifecycle-before.png", "uhd-after-resume.png"),
                              ("uhd-lifecycle-before.png", "uhd-after-recreate.png")):
            require(report["captureSamples"][before]["rgbaSha256"] == report["captureSamples"][after]["rgbaSha256"],
                    "Independent raw-RGBA checksums disagree with exact-pixel roundtrip results")
        provenance = json.loads((directory / "build-provenance.json").read_text())
        commit = (directory / "build-commit.txt").read_text().strip()
        require(provenance["passed"] is True and provenance["commit"] == commit
                and re.fullmatch(r"[0-9a-f]{40}", commit),
                "UHD build lacks exact source/native provenance")
        require(provenance.get("applicationId") == expected_package
                and provenance.get("version") == "0.4.0" and provenance.get("versionCode") == 5,
                "UHD build has the wrong package identity or version")
        expected_provenance = verify_integrated_core(Path(__file__).resolve().parents[1])
        require(all(provenance.get(name) == value for name, value in expected_provenance.items()),
                "UHD build does not match the verified integrated core and shared resolution sources")
        if ultra:
            require(provenance.get("applicationId") == expected_package
                    and provenance.get("baselineCommit") == "403bdd7311e6d6b37e849fbe2c85375a9a2cd2ac"
                    and provenance.get("originalCoreInterfacesPreserved") is True
                    and set(provenance.get("optimizedCoreFiles", {})) == {
                        "native/include/mundo_x/compact_core.hpp", "native/include/mundo_x/pauli_observer.hpp"},
                    "8K build lacks the explicit core optimization/source inventory")
        require({name: item["sha256"] for name, item in provenance["apks"].items()} == checksums,
                "Android evidence APK hashes differ from staged build provenance")
        coexist = set((directory / "coinstallation.txt").read_text().splitlines())
        require({"package:" + expected_package, "package:com.mundox.graphics"} <= coexist,
                "The normal app and UHD app were not installed together")
        print("Verified actual 3840×2160 / 2160×3840 rendering, FIT picking and 250 exported PNG pixel samples.")
    exports = report["projectExports"]
    expected_projects = {"portable-bell-android.cosmic", "portable-ghz-android.cosmic", "portable-mutated-android.cosmic"}
    require(len(exports) == 3 and {entry["file"] for entry in exports} == expected_projects,
            "Portable Android exports missing")
    project_manifest = {}
    for entry in exports:
        require(entry["stateFile"] == entry["file"] + ".state.json", "Unexpected native state evidence path")
        for name in (entry["file"], entry["stateFile"]):
            data = (directory / name).read_bytes()
            require(0 < len(data) <= 16 * 1024 * 1024, "Portable evidence file is empty or oversized")
            project_manifest[name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    (directory / "verified-project-files.json").write_text(json.dumps(project_manifest, indent=2) + "\n")
    print(f"Verified {len(tests)} Android device tests and {len(manifest)} PNG captures.")
    print(f"Driver: {metrics['glRenderer']}")
    print(f"Gate changed {metrics['coupledChangedPixels']} pixels with coupling; "
          f"{metrics['uncoupledChangedPixels']} without coupling.")
    print(f"Portable codec: {codec['checks']} checks; three projects exported with native state evidence.")
    print("Portable reimport/recreation each retained exact GLES pixels on this driver.")
    print("These are software-emulator correctness results, not Samsung A16 performance measurements.")


if __name__ == "__main__":
    main()
