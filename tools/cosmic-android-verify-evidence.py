#!/usr/bin/env python3
"""Fail closed on missing device cases, images, native results or installation isolation."""

import hashlib
import importlib.util
import json
import re
import struct
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXPERIMENTS = {"circuitos", "micromundo", "objetos", "ecos", "vm8", "cuantica", "visual", "gpu",
               "juego", "sincronizada", "red", "dispositivo", "rendimiento", "clasico"}
COSMIC_CASES = {
    "application_identity_and_exclusions", "new_motor_live_framebuffer", "four_panels_retain_native_owner",
    "camera_and_observer_preserve_state", "gate_changes_coupled_image", "gate_preserves_uncoupled_image",
    "saf_export_and_reimport", "invalid_saf_import_preserves_session", "background_resume_preserves_world",
    "activity_recreation_restores_project", *("experiment_" + name + "_launch" for name in EXPERIMENTS),
}
GRAPHICS_CASES = {
    "native_bell_invariants", "renderer_borrows_native_state", "gles_one_million_objects",
    "gles_maximum_object_limit", "render_observes_without_mutating", "gate_changes_coupled_image",
    "gate_preserves_uncoupled_image", "selection_and_individual_edits", "edit_restoration", "pause_resume_preserves_state",
    "portable_codec_strict_validation", "portable_desktop_fixtures_import", "portable_mutation_export_reimport",
    "portable_invalid_import_preserves_session", "portable_activity_recreation",
}
MICRO_CASES = {
    "jni_buffer_bounds_and_session_lifetime", "render_and_pause", "single_step", "particle_capacity",
    "quantum_gates_and_bell_entropy", "colony_growth_food_and_seeding", "coupling_switch_and_seed_reset",
    "background_resume_and_gl_recreation",
}


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def image_manifest(directory, names):
    manifest = {}
    for name in sorted(names):
        data = (directory / name).read_bytes()
        require(len(data) > 32 and data[:8] == b"\x89PNG\r\n\x1a\n", "Invalid PNG: " + name)
        width, height = struct.unpack(">II", data[16:24])
        require(width > 0 and height > 0, "Empty screenshot: " + name)
        manifest[name] = {"width": width, "height": height, "bytes": len(data),
                          "sha256": hashlib.sha256(data).hexdigest()}
    return manifest


def device_report(directory, expected, marker, output):
    require(marker in output and "INSTRUMENTATION_CODE: -1" in output, "Missing successful instrumentation completion")
    report = json.loads((directory / "report.json").read_text())
    require(report["schemaVersion"] == 1 and report["testsRun"] == len(expected) and report["testsFailed"] == 0,
            "Incomplete or failed device tests")
    tests = report["tests"]
    require(len(tests) == len(expected) and {item["name"] for item in tests} == expected
            and all(item["passed"] is True for item in tests), "Unexpected or failing device case inventory")
    junit = ET.parse(directory / "junit.xml").getroot()
    require(int(junit.attrib["tests"]) == len(expected) and int(junit.attrib["failures"]) == 0,
            "JUnit disagrees with the device JSON report")
    metrics = report["metrics"]
    require(metrics["androidSdk"] == 35 and "x86_64" in metrics["abis"], "Wrong device API or ABI")
    require("swiftshader" in metrics["glRenderer"].lower(), "Evidence is not from the specified GLES driver")
    require(metrics["nativeStateBytes"] > 0, "Missing authoritative native state")
    return report, metrics


def verify_cosmic(directory, output):
    report, m = device_report(directory, COSMIC_CASES, "COSMIC_UNIFIED_TESTS_PASSED", output)
    require(m["applicationId"] == "com.mundox.cosmic" and m["version"] == "0.3.0"
            and m["activityCount"] == 16 and m["windowsActivityPresent"] is False, "Wrong new app inventory")
    require(m["initialObjects"] == 16_384 and m["initialQubits"] == 20 and m["initialVisiblePixels"] > 100
            and m["initialDistinctIds"] > 0 and m["initialResolutionLimit"] == 960, "New home did not display the real default world")
    require(m["panelCount"] == 4 and m["panelsPreservedOwner"] is True
            and m["cameraPreservedOwner"] is True and m["observationWasReadOnly"] is True, "Navigation/camera altered the native core")
    require(m["coupledChangedPixels"] > 20 and m["uncoupledChangedPixels"] == 0 and m["gatePreservedOwner"] is True,
            "The new viewport failed its native quantum/control image comparison")
    require(m["safExportBytes"] > 0 and m["safExportQubits"] == 2 and m["safUsedProductionCallbacks"] is True
            and m["safRoundtripChangedPixels"] == 0 and m["invalidImportChangedPixels"] == 0
            and m["invalidImportPreservedOwner"] is True, "Real document callbacks failed to preserve the project")
    require(m["backgroundChangedPixels"] == 0 and m["backgroundPreservedOwner"] is True
            and m["recreateChangedPixels"] == 0 and m["recreateCreatedNewOwner"] is True
            and m["reopenChangedPixels"] == 0 and m["restoredEnabledCoupling"] == -.125
            and m["hiddenOrbitStatePreserved"] is True and m["diskSessionCreatedNewOwner"] is True,
            "New Activity lifecycle lost state, hidden orbit fields, negative coupling or pixels")
    experiments = {item["id"]: item for item in report["experiments"]}
    require(len(report["experiments"]) == 14 and set(experiments) == EXPERIMENTS
            and all(item["passed"] is True for item in experiments.values())
            and m["experimentReturnsPreservedProject"] == 14, "Not all included labs returned to the preserved main project")
    vm, echo = experiments["vm8"]["nativeVm"], experiments["ecos"]["nativeEcho"]
    require(vm["status"] == "PASS" and vm["reference_ok"] is True and vm["output"][0] == 55, "Restored eight-bit VM JNI did not run its sum")
    require(echo["status"] == "PASS" and echo["validation"] == "VERIFIED" and echo["reference_ok"] is True
            and abs(echo["signal_z"] + .5) < 1e-10 and abs(echo["control_z"] - 1) < 1e-10,
            "Restored echo JNI did not match its reference")
    require("tx=0B rx=0B" in experiments["sincronizada"]["rendererStatus"], "Offline smoke test exchanged network data")

    required = {"cosmic-motor.png", "cosmic-nucleo.png", "cosmic-labs.png", "cosmic-archivo.png",
                "cosmic-render-color.png", "cosmic-render-ids.png",
                "cosmic-coupled-before.png", "cosmic-coupled-after.png", "cosmic-uncoupled-before.png", "cosmic-uncoupled-after.png",
                "cosmic-saf-before.png", "cosmic-saf-after.png", "cosmic-background-before.png", "cosmic-background-after.png",
                "cosmic-recreate-before.png", "cosmic-recreate-after.png", "cosmic-reopen-after.png",
                *("experiment-" + name + ".png" for name in EXPERIMENTS)}
    require(required <= set(report["images"]), "An actual app/experiment framebuffer or screenshot is missing")
    manifest = image_manifest(directory, required)
    for pair in ("uncoupled", "saf", "background", "recreate"):
        require(manifest[f"cosmic-{pair}-before.png"]["sha256"] == manifest[f"cosmic-{pair}-after.png"]["sha256"],
                "Independent PNG comparison disagrees for " + pair)
    require(manifest["cosmic-coupled-before.png"]["sha256"] != manifest["cosmic-coupled-after.png"]["sha256"],
            "Coupled before/after PNGs are identical")
    require(manifest["cosmic-recreate-before.png"]["sha256"] == manifest["cosmic-reopen-after.png"]["sha256"],
            "Independent PNG comparison disagrees for the reopened private session")

    packages = (directory / "coexist-packages.txt").read_text()
    identities = dict(re.findall(r"package:(com\.mundox\.[^\s]+) uid:(\d+)", packages))
    require("com.mundox.cosmic" in identities and "com.mundox.graphics" in identities
            and identities["com.mundox.cosmic"] != identities["com.mundox.graphics"], "The two apps did not coexist under separate UIDs")
    require((directory / "coexist-sentinel.txt").read_text().strip() == "cosmic-unified-coexist",
            "Installing the new app overwrote the previous app's private test data")
    require(all("Status: ok" in (directory / name).read_text() for name in
                ("coexist-old-launch.txt", "coexist-new-launch.txt")), "Both application IDs did not cold-launch")
    isolation = {"passed": True, "newApplicationId": "com.mundox.cosmic", "previousApplicationId": "com.mundox.graphics",
                 "distinctUids": True, "previousPrivateDataPreserved": True, "bothColdLaunchesSucceeded": True}
    (directory / "coinstallation-verification.json").write_text(json.dumps(isolation, indent=2) + "\n")

    # Reuse the independent desktop reference implementation, not app-side state math.
    spec = importlib.util.spec_from_file_location("cosmic_portability", ROOT / "tools/cosmic-verify-portability.py")
    portability = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(portability)
    document = portability.read_project(directory / "unified-saf-export.cosmic")
    proof = portability.evidence_json(directory / "unified-saf-native.json")
    with tempfile.TemporaryDirectory(prefix="cosmic-unified-portability-") as build:
        bridge = portability.compile_bridge(build)
        native = portability.verify_native(bridge, document, proof)
    native.update({"status": "passed", "source": "Production CosmicActivity SAF export", "gpuContextCreated": False})
    (directory / "unified-portability-verification.json").write_text(json.dumps(native, indent=2) + "\n")
    return {"tests": len(COSMIC_CASES), "images": manifest, "experiments": 14,
            "signedPauliReferenceComparisons": native["signed_pauli_reference_comparisons"], "coinstallation": isolation}


def verify_graphics(directory, output):
    report, m = device_report(directory, GRAPHICS_CASES, "MUNDOX_GRAPHICS_TESTS_PASSED", output)
    require(m["qubits"] == 20 and m["millionObjectCount"] == 1_000_000 and m["maximumObjectCount"] == 1_048_576,
            "Original graphics limits were not exercised in the new APK")
    require(m["coupledChangedPixels"] > 20 and m["uncoupledChangedPixels"] == 0
            and m["removedIdPixels"] == 0 and m["pauseResumeChangedPixels"] == 0, "Original core/renderer invariants failed")
    codec = m["projectCodec"]
    require(codec["passed"] is True and codec["checks"] >= 99 and codec["rejectedInvalidDocuments"] >= 83
            and codec["nativeOrGlCreated"] is False, "Strict portable codec validation incomplete")
    fixtures = {item["fixture"]: item for item in m["portableFixtures"]}
    require(len(m["portableFixtures"]) == 2 and set(fixtures) == {"bell-observer.cosmic", "ghz-city.cosmic"}
            and all(item["passed"] is True and item["visiblePixels"] > 100 for item in fixtures.values()), "Imported fixtures failed to render")
    ghz, bell = fixtures["ghz-city.cosmic"], fixtures["bell-observer.cosmic"]
    require(ghz["qubits"] == 3 and ghz["objects"] == 1_000_000 and ghz["observables"] == 11
            and ghz["seed"] == 4_000_000_001 and ghz["coupling"] == -.125 and ghz["cameraMode"] == "orbit",
            "GHZ fixture lost its original parameters")
    require(bell["qubits"] == 2 and bell["objects"] == 1024 and bell["observables"] == 7
            and bell["seed"] == 2026 and bell["coupling"] == .25 and bell["cameraMode"] == "lookAt",
            "Bell fixture lost its original parameters")
    require(m["portableRoundtripChangedPixels"] == 0 and m["portableRoundtripIdDifferences"] == 0
            and m["portableEditChangedPixels"] > 10 and m["portableCameraPreservedCore"] is True,
            "Original project mutation/import contract failed")
    require(m["portableRejectedActiveImports"] == 3 and m["portableInvalidImportChangedPixels"] == 0
            and m["portableRejectedRevisionOperations"] == 2 and m["portableRejectedTimeOperations"] == 2,
            "Invalid project state changed an active session")
    require(m["portableRecreateChangedPixels"] == 0 and m["portableRecreateNewOwner"] is True
            and m["portableRecreateRestoredCoupling"] == -.125 and m["portableFinalQubits"] == 3
            and m["portableFinalStateBytes"] > 0, "Original lifecycle lost a portable project")
    required = {
        "million-color.png", "million-ids.png", "maximum-color.png", "maximum-ids.png",
        "coupling-on-before.png", "coupling-on-after.png", "coupling-off-before.png", "coupling-off-after.png",
        "selection-before.png", "selection-highlight.png", "selection-taller.png", "selection-removed.png", "edit-restored.png",
        "lifecycle-before.png", "lifecycle-after.png", "activity.png", "portable-bell-color.png", "portable-bell-ids.png",
        "portable-ghz-color.png", "portable-ghz-ids.png", "portable-roundtrip-before.png", "portable-roundtrip-after.png",
        "portable-recreate-before.png", "portable-recreate-after.png",
    }
    require(required <= set(report["images"]), "Original observer framebuffer evidence is incomplete")
    exports = report["projectExports"]
    expected = {"portable-bell-android.cosmic", "portable-ghz-android.cosmic", "portable-mutated-android.cosmic"}
    require(len(exports) == 3 and {item["file"] for item in exports} == expected, "Original Android exports are missing")
    for item in exports:
        require(item["stateFile"] == item["file"] + ".state.json", "Unexpected native proof filename")
        for name in (item["file"], item["stateFile"]):
            require(0 < (directory / name).stat().st_size <= 16 * 1024 * 1024, "Missing or oversized portable evidence")
    return {"tests": len(GRAPHICS_CASES), "images": image_manifest(directory, required), "portableCodecChecks": codec["checks"]}


def verify_micro(directory, output):
    require("INSTRUMENTATION_CODE: -1" in output, "MicroLab runner did not finish successfully")
    report = json.loads((directory / "micro-results.json").read_text())
    notes = report["notes"]
    require(report["passed"] == 8 and report["failed"] == 0 and len(notes) == 8
            and set(notes) == {"PASS " + case for case in MICRO_CASES}, "MicroLab JNI/GLES/control suite is incomplete")
    state = report["snapshot"]
    require(state["seed"] == 123 and state["ticks"] == 1 and state["coupled"] is False,
            "MicroLab reset, pause or coupling state was lost")
    return {"tests": len(MICRO_CASES), "images": image_manifest(directory,
            {"micro-particles.png", "micro-colony.png", "micro-quantum.png"})}


def main():
    directory = Path(sys.argv[1] if len(sys.argv) > 1 else "cosmic-evidence")
    suite = sys.argv[2] if len(sys.argv) > 2 else (directory / "suite.txt").read_text().strip()
    require(suite in {"cosmic", "graphics", "micro"}, "Unknown validation suite")
    require((directory / "suite.txt").read_text().strip() == suite, "Wrong suite evidence directory")
    output = (directory / "instrumentation.txt").read_text()
    require(not re.search(r"INSTRUMENTATION_RESULT: tests_failed=[1-9]", output), "Instrumentation reported a failure")
    audit = json.loads((directory / "apk-audit.json").read_text())
    require(audit["passed"] is True and audit["applicationId"] == "com.mundox.cosmic"
            and audit["windowsEmulatorPackaged"] is False, "APK packaging audit failed")
    commit = (directory / "build-commit.txt").read_text().strip()
    require(re.fullmatch(r"[0-9a-f]{40}", commit) and audit["buildCommit"] == commit, "Artifact/source commit mismatch")
    checksums = {}
    for row in (directory / "apk-SHA256SUMS.txt").read_text().splitlines():
        checksum, name = row.split()
        require(name not in checksums and re.fullmatch(r"[0-9a-f]{64}", checksum), "Malformed APK checksums")
        checksums[name] = checksum
    require(checksums == audit["apkSha256"], "Installed APK checksum inventory differs from audited build")
    runner = {"cosmic": "com.mundox.cosmic.CosmicInstrumentation", "graphics": "com.mundox.graphics.GraphicsInstrumentation",
              "micro": "com.mundox.lab.MicroLabInstrumentation"}[suite]
    require("com.mundox.cosmic.test/" + runner in (directory / "instrumentation-inventory.txt").read_text(),
            "Runner was not installed for the new application")
    verify = {"cosmic": verify_cosmic, "graphics": verify_graphics, "micro": verify_micro}[suite]
    result = verify(directory, output)
    result.update({"schemaVersion": 1, "status": "passed", "suite": suite, "buildCommit": commit,
                   "applicationId": "com.mundox.cosmic", "apkSha256": checksums["COSMIC-X-Engine-0.3.0.apk"],
                   "performanceScope": "Software emulator correctness; not measured phone performance"})
    (directory / "verified-evidence.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"Verified {result['tests']} {suite} device tests and {len(result['images'])} actual PNG captures on the new APK.")
    if suite == "cosmic":
        print("All 14 original Android experiments opened; the Windows emulator is excluded.")
        print("Production SAF export was replayed by the desktop core and compared with its dense reference.")
        print("The previous graphics app coinstalled under a different UID and retained its private test data.")


if __name__ == "__main__":
    main()
