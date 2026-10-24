#!/usr/bin/env python3
"""Reject incomplete Android runs and inventory genuine captured framebuffers."""

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
    directory = Path(sys.argv[1] if len(sys.argv) > 1 else "solar-evidence")
    profile = sys.argv[2] if len(sys.argv) > 2 else ""
    uhd = profile == "--uhd4k"
    ultra = profile == "--ultra8k"
    fixed = uhd or ultra
    require(len(sys.argv) <= 3 and profile in ("", "--uhd4k", "--ultra8k"),
            "Unexpected solar verification profile")
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
        "native_gles_launch_and_textures", "ether_gpu_spatial_visibility_exact_rgba",
        "astronomy_and_magnetic_classification",
        "native_ether_fusion_and_readonly_observation", "quantum_coupled_and_uncoupled_frames",
        "intrinsic_and_induced_field_frames", "solar_wind_layer", "saturn_rings_and_moon_navigation",
        "forward_reverse_and_paused_time", "real_scale_earth_focus",
        "camera_rotation_zoom_pan_and_flight", "resolution_budget",
        "background_resume_preserves_state", "recreation_and_orientation_preserve_state",
    }
    if uhd:
        expected_tests.update({"uhd_framebuffer_return_preserves_scene", "uhd_fit_picking_preserves_scene",
                               "entry_all_planets_and_moons", "entry_atmosphere_and_grazing_frames",
                               "entry_uhd_lifecycle_and_orientation", "map_ui_planet_and_moon_selection"})
    elif ultra:
        expected_tests.update({"ultra8k_framebuffer_return_preserves_scene",
                               "ultra8k_earth_entry_atmosphere"})
    require(len(tests) == len(expected_tests) and set(names) == expected_tests,
            "Incomplete or unexpected solar test inventory")
    require(report["testsRun"] == len(tests) and report["testsFailed"] == 0,
            "The solar report contains missing or failed tests")
    require(all(test["passed"] is True for test in tests), "A solar device test failed")
    junit = ET.parse(directory / "junit.xml").getroot()
    require(int(junit.attrib["tests"]) == len(expected_tests) and int(junit.attrib["failures"]) == 0,
            "The JUnit report disagrees with device JSON")
    metrics = report["metrics"]
    require(metrics.get("uhd4kSuite", False) is uhd
            and metrics.get("ultra8kSuite", False) is ultra,
            "The Android run used the wrong legacy/4K/8K profile")
    require(metrics["androidSdk"] == 35 and "x86_64" in metrics["abis"],
            "The device does not match the Android 15 x86_64 validation target")
    require("swiftshader" in metrics["glRenderer"].lower(), "Unexpected GLES validation driver")
    require(metrics["nativeDomain"] == "q0,q1,..." and metrics["nativePreparationSites"] == 8
            and 0 <= metrics["nativeActiveSites"] <= 8 and metrics["nativeStateBytes"] >= 0,
            "The Ether domain and finite Solar preparation were not distinguished")
    require(metrics["framesRendered"] > 0, "No actual GLES frames were rendered")
    parity = metrics.get("etherGpuExactRgba")
    require(isinstance(parity, list) and len(parity) == 4,
            "Native Ether GPU must compare four real matched GLES scenes")
    require({p.get("destination") for p in parity} == {-1, 3, 6, 9},
            "Missing system/Earth/Saturn/Moon Ether GPU parity scene")
    require(all(p.get("candidateActive") is True and p.get("pixelDifferences") == 0
                and p.get("fallback") is None for p in parity),
            "Native Ether GPU changed image or silently fell back")

    require(metrics["initialWindowFocused"] is True, "The launched Activity did not receive input focus")
    require(metrics["bodyCount"] == 23 and metrics["planetCount"] == 8 and metrics["moonCount"] == 14,
            "Astronomical body inventory is incomplete")
    require(.98 < metrics["earthJ2000DistanceAU"] < 1.02 and 350_000 < metrics["earthMoonDistanceKm"] < 410_000,
            "Packaged astronomical geometry failed its independent scale checks")
    require(9.81 < metrics["earthGravityMs2"] < 9.83
            and 11.18 < metrics["earthEscapeKmS"] < 11.20
            and metrics["oneSecondFallAltitudeKm"] < 100
            and metrics["oneSecondFallVelocityKmS"] < 0,
            "Packaged JPL-GM gravity or local free-fall integration failed")
    require(metrics["fieldTypes"] == {"earth": 1, "venus": 2, "mars": 3, "moon": 0, "ganymede": 1},
            "Intrinsic, induced and crustal magnetic environments were not distinguished")
    textures = metrics["packagedNasaTextures"]
    require(len(textures) == 3 and {entry["asset"] for entry in textures}
            == {"textures/earth.png", "textures/moon.jpg", "textures/jupiter.jpg"}
            and all(entry["width"] >= 512 and entry["height"] >= 256 for entry in textures)
            and metrics["uploadedNasaTextures"] == 3, "Packaged NASA maps were not decoded and uploaded to GLES")
    bell = metrics["framedBellExpectations"]
    require(len(bell) == 8 and all(abs(actual - expected) < 1e-6
            for actual, expected in zip(bell, (0, 0, 0, 0, 0, 1, 0, 1)))
            and metrics["observationPreservedNativeRevision"] is True
            and metrics["observationPreservedNativeEpoch"] is True,
            "The Bell preparation followed by Ether rules has incorrect or mutating observations")
    ether = metrics["etherExpectations"]
    require(ether == [0, 0, 0, 0, 0] and metrics["etherRules"] == 2
            and metrics["etherObservationReadOnly"] is True,
            "The Ether view does not match Solar's shared prepared state")
    expected_preparations = (
        ([0, 0, 0, 0, 0, 0, 0, 0], [1, 1, 0, 0, 0]),
        ([0, 0, 1, 0, 0, 1, 0, 1], [0, 0, 0, 1, 1]),
        ([0, 0, 0, 0, 0, 1, 0, 1], [0, 0, 0, 0, 0]),
        ([0, -1, 0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0]),
    )
    preparations = metrics["etherFusionPresets"]
    require(len(preparations) == 4 and metrics["etherFusionSingleObservation"] is True
            and all(entry["preset"] == preset and entry["local"] == expected[0]
                    and entry["ether"] == expected[1] and entry["rules"] == 2
                    and entry["activeSites"] == (0 if preset == 0 else 8)
                    for preset, (entry, expected) in enumerate(zip(preparations, expected_preparations))),
            "Solar local and Ether coefficients lack shared-state preparation evidence")
    require(metrics["deterministicPausedChangedPixels"] == 0 and metrics["coupledChangedPixels"] > 20
            and metrics["uncoupledChangedPixels"] == 0 and metrics["coupledComparisonPresets"] == [0, 3]
            and metrics["quantumPreparationOrbitalDeltaAU"] == 0,
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
    if uhd:
        expected_apks = {"COSMIC-X-Solar-4K.apk", "COSMIC-X-Solar-4K-tests.apk", "COSMIC-X-Solar-coexist.apk"}
    elif ultra:
        expected_apks = {"COSMIC-X-Solar-8K.apk", "COSMIC-X-Solar-8K-tests.apk", "COSMIC-X-Solar-coexist.apk"}
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
        "earth-surface.png", "earth-fields-base.png", "earth-fields-phase.png",
        "venus-magnetic-environment.png", "mars-magnetic-environment.png", "sun-solar-wind.png",
        "saturn-rings.png", "moon-surface.png", "solar-system-overview.png", "earth-real-scale.png",
        "activity-portrait.png", "activity-landscape.png",
    }
    uhd_images = {
        "uhd-earth-orbit.png", "uhd-picking-before.png", "uhd-picking-earth.png", "uhd-picking-band.png",
        "uhd-entry-before.png", "uhd-entry-recreated.png", "uhd-entry-landscape.png", "uhd-entry-portrait-return.png",
    }
    if uhd:
        expected_images.update(uhd_images | {"uhd-adaptive-before.png", "uhd-adaptive-after.png",
                                            "entry-activity-portrait.png", "entry-activity-landscape.png",
                                            "map-ui-body-8.png", "map-ui-body-5.png", "map-ui-body-11.png",
                                            "map-selected-europa.png"})
        expected_images.update(f"entry-body-{body:02d}.png" for body in range(1, 23))
        expected_images.update(f"entry-{body}-{kind}.png" for body in ("earth", "venus", "jupiter", "titan", "moon")
                               for kind in ("atmosphere", "grazing"))
        expected_images.update({"entry-earth-vacuum.png", "entry-moon-vacuum.png"})
    elif ultra:
        expected_images.update({"uhd-adaptive-before.png", "uhd-adaptive-after.png",
                                "ultra8k-earth-orbit.png", "ultra8k-earth-entry.png"})
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
    if uhd:
        require(metrics["applicationId"] == "com.cosmicx.solar.uhd" and metrics["uhdPixels"] == 8_294_400,
                "The standalone package or UHD pixel count is wrong")
        require(metrics["uhdReturnChangedPixels"] == 0 and metrics["uhdNativeOwnerPreserved"] is True
                and metrics["uhdSelectedBody"] == 3 and metrics["uhdBandChangedPixels"] == 0,
                "UHD transition or FIT picking changed the wrong state")
        fit = metrics["uhdFit"]
        require((fit["left"] > 1 or fit["top"] > 1) and fit["width"] > 0 and fit["height"] > 0
                and fit["left"] + fit["width"] <= fit["surfaceWidth"]
                and fit["top"] + fit["height"] <= fit["surfaceHeight"], "The actual FIT bands were not exercised")
        entries = metrics["entryBodies"]
        require(len(entries) == 22 and {item["body"] for item in entries} == set(range(1, 23))
                and all(item["brightPixels"] > 80 for item in entries),
                "Not all eight planets and fourteen moons produced local entry frames")
        daylight_offsets = {11: -.25, 14: 6, 15: -.25, 16: -.25, 17: .125, 19: -1}
        require(all(item["julianDate"] == 2461331.0 + daylight_offsets.get(item["body"], 0)
                    for item in entries), "The local-terrain daylight fixtures were not preserved")
        require(all(item["gasGiant"] is (5 <= item["body"] <= 8)
                    and item["solidSurface"] is not item["gasGiant"] for item in entries),
                "Rocky and gas-giant descent were not distinguished")
        require(len({item["rgbaSha256"] for item in entries}) >= 10
                and metrics["entryNativePreparationPreserved"] is True,
                "Entry reused an undifferentiated image or mutated the native preparation")
        layers = metrics["entryLayerComparisons"]
        require(set(layers) == {"earth", "venus", "jupiter", "titan", "moon"}
                and all(item["descentChangedPixels"] > 50 for item in layers.values())
                and all(item["atmosphereChangedPixels"] > 20 for name, item in layers.items() if name != "moon")
                and layers["moon"]["atmosphereChangedPixels"] == 0,
                "Atmosphere/descent comparisons or airless-Moon control failed")
        require(metrics["entryRecreateChangedPixels"] == 0 and metrics["entryRotationChangedPixels"] == 0
                and metrics["entryRecreatedPreset"] == 3 and metrics["entryCameraPreserved"] is True,
                "UHD local camera or native preparation was lost through Android lifecycle")
        require(metrics["mapUiSelectedBodies"] == [8, 5, 11] and metrics["mapNavigationPreservedCoreAndDate"] is True,
                "Actual map touches did not navigate planets and a moon")
        samples = report["captureSamples"]
        for name in uhd_images:
            expected_size = (3840, 2160) if name == "uhd-entry-landscape.png" else (2160, 3840)
            item = image_manifest[name]
            require((item["width"], item["height"]) == expected_size, "Not a true UHD PNG: " + name)
            proof = samples[name]
            require(proof["decodedPngSamplesVerified"] is True and len(proof["points"]) == 25
                    and (proof["width"], proof["height"]) == expected_size
                    and re.fullmatch(r"[0-9a-f]{64}", proof["rgbaSha256"]),
                    "Shipping PNG export lacks exact channel/orientation samples: " + name)
        for before, after in (("uhd-picking-earth.png", "uhd-picking-band.png"),
                              ("uhd-entry-before.png", "uhd-entry-recreated.png"),
                              ("uhd-entry-before.png", "uhd-entry-portrait-return.png"),
                              ("uhd-adaptive-before.png", "uhd-adaptive-after.png")):
            require(samples[before]["rgbaSha256"] == samples[after]["rgbaSha256"],
                    "Raw RGBA checksums disagree with exact transition assertions")
        expected_provenance = verify_integrated_core(Path(__file__).resolve().parents[1])
        require(provenance["passed"] is True and provenance["scientificChecks"] >= 511
                and all(provenance.get(name) == value for name, value in expected_provenance.items()),
                "Solar UHD evidence does not match the verified integrated core, resolution and science sources")
        coexist = set((directory / "coinstallation.txt").read_text().splitlines())
        require({"package:com.cosmicx.solar", "package:com.cosmicx.solar.uhd"} <= coexist,
                "The original and UHD solar packages did not coexist")
        print("Verified real UHD, FIT picking, 22 local environments, atmosphere controls and live map touches.")
    elif ultra:
        require(metrics["applicationId"] == "com.cosmicx.solar.ultra"
                and metrics["uhdPixels"] == 33_177_600,
                "The standalone Solar 8K package or pixel count is wrong")
        require(metrics["uhdReturnChangedPixels"] == 0
                and metrics["uhdNativeOwnerPreserved"] is True,
                "Solar 8K transition changed astronomical or native state")
        samples = report["captureSamples"]
        for name in ("ultra8k-earth-orbit.png", "ultra8k-earth-entry.png"):
            item = image_manifest[name]
            require((item["width"], item["height"]) == (4320, 7680),
                    "Not a true portrait 8K PNG: " + name)
            proof = samples[name]
            require(proof["decodedPngSamplesVerified"] is True and len(proof["points"]) == 25
                    and (proof["width"], proof["height"]) == (4320, 7680)
                    and re.fullmatch(r"[0-9a-f]{64}", proof["rgbaSha256"]),
                    "Solar 8K PNG lacks exact channel/orientation proof: " + name)
        entry = metrics["ultra8kEntry"]
        require((entry["width"], entry["height"]) == (4320, 7680)
                and 0 <= entry["altitudeKm"] <= 1000 and entry["brightPixels"] > 80,
                "Solar 8K atmospheric entry evidence is incomplete")
        require(provenance["applicationId"] == "com.cosmicx.solar.ultra"
                and provenance["version"] == "1.3.0" and provenance["versionCode"] == 4
                and provenance["scientificChecks"] >= 511,
                "Solar 8K APK provenance is incomplete")
        expected_provenance = verify_integrated_core(Path(__file__).resolve().parents[1])
        require(all(provenance.get(name) == value for name, value in expected_provenance.items()),
                "Solar 8K evidence does not match the verified integrated core")
        coexist = set((directory / "coinstallation.txt").read_text().splitlines())
        require({"package:com.cosmicx.solar", "package:com.cosmicx.solar.ultra"} <= coexist,
                "The original and Solar 8K packages did not coexist")
        print("Verified real 7680x4320-class Solar target, atmospheric entry and exact PNG evidence.")
    (directory / "verified-images.json").write_text(json.dumps(image_manifest, indent=2) + "\n")
    print(f"Verified {len(tests)} solar Android tests and {len(images)} actual PNG captures.")
    print("Driver: " + metrics["glRenderer"])
    print(f"Ether domain: {metrics['nativeDomain']}; {metrics['nativePreparationSites']} preparation sites; "
          f"{metrics['nativeActiveSites']} active sites; {metrics['nativeStateBytes']} tableau payload bytes.")
    print("These are Android software-emulator correctness results, not Samsung A16 performance measurements.")


if __name__ == "__main__":
    main()
