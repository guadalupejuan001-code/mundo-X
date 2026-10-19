#!/usr/bin/env python3
"""Audit the standalone solar UHD APK and prove its native sources are unchanged."""

import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ORIGINAL = "403bdd7311e6d6b37e849fbe2c85375a9a2cd2ac"
APP = "COSMIC-X-Solar-4K.apk"
TESTS = "COSMIC-X-Solar-4K-tests.apk"
COMPANION = "COSMIC-X-Solar-coexist.apk"


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def sdk_level(manifest, names):
    labels = "|".join(re.escape(name) for name in names)
    values = re.findall(r"^(?:" + labels + r"):\s*'([0-9]+)'\s*$", manifest, re.MULTILINE)
    return int(values[0]) if len(values) == 1 else None


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def main():
    directory = Path(sys.argv[1] if len(sys.argv) > 1 else "solar-4k-dist")
    sdk = os.environ.get("ANDROID_SDK_ROOT") or os.environ.get("ANDROID_HOME")
    require(sdk, "Android SDK path missing")
    build_tools = Path(sdk) / "build-tools" / "35.0.0"
    checksums, signatures, manifests = {}, {}, {}
    for name in (APP, TESTS, COMPANION):
        data = (directory / name).read_bytes()
        checksums[name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        with zipfile.ZipFile(directory / name) as apk:
            require(apk.testzip() is None and "classes.dex" in apk.namelist(), "Invalid APK payload: " + name)
        signature = subprocess.check_output([str(build_tools / "apksigner"), "verify", "--verbose",
                                             "--print-certs", str(directory / name)], text=True)
        (directory / (name[:-4] + "-signature.txt")).write_text(signature)
        require("Verified using v2 scheme (APK Signature Scheme v2): true" in signature,
                "APK v2 signature did not verify: " + name)
        certificate = re.findall(r"^Signer #1 certificate SHA-256 digest: ([0-9a-f]{64})$", signature, re.MULTILINE)
        require(len(certificate) == 1, "Missing signing certificate: " + name)
        signatures[name] = certificate[0]
        badging = subprocess.check_output([str(build_tools / "aapt2"), "dump", "badging", str(directory / name)], text=True)
        (directory / (name[:-4] + "-manifest.txt")).write_text(badging)
        manifests[name] = badging
    require(signatures[APP] == signatures[TESTS], "App and test signing certificates differ")
    app = manifests[APP]
    require("package: name='com.cosmicx.solar.uhd' versionCode='2' versionName='1.1.0'" in app,
            "Unexpected standalone package identity or version")
    require("package: name='com.cosmicx.solar'" in manifests[COMPANION], "Legacy companion package changed")
    require("package: name='com.cosmicx.solar.uhd.test'" in manifests[TESTS], "Generated test package does not target UHD")
    require("launchable-activity: name='com.mundox.solar.SolarActivity'" in app,
            "The solar activity is not the launcher")
    require("application-label:'COSMIC X Solar 4K'" in app, "Standalone APK title differs")
    require((sdk_level(app, ("sdkVersion", "minSdkVersion")), sdk_level(app, ("targetSdkVersion",))) == (26, 35),
            "Incorrect Android SDK compatibility:\n" + app)
    require("android.permission.INTERNET" not in app, "The standalone renderer must run offline")
    test_tree = subprocess.check_output([str(build_tools / "aapt2"), "dump", "xmltree", str(directory / TESTS),
                                         "--file", "AndroidManifest.xml"], text=True)
    (directory / "test-manifest-tree.txt").write_text(test_tree)
    require('"com.mundox.solar.SolarInstrumentation"' in test_tree
            and '"com.cosmicx.solar.uhd"' in test_tree, "Instrumentation runner/target was not packaged correctly")

    expected = {f"lib/{abi}/libmx_graphics.so" for abi in ("arm64-v8a", "x86_64")}
    libraries = {}
    with zipfile.ZipFile(directory / APP) as apk:
        found = {name for name in apk.namelist() if name.endswith("/libmx_graphics.so")}
        require(found == expected, "Unexpected original native ABI inventory: " + str(found))
        require({"assets/shaders/solar.vert", "assets/shaders/solar.frag", "assets/shaders/overlay.vert",
                 "assets/shaders/overlay.frag", "assets/textures/earth.png", "assets/textures/moon.jpg",
                 "assets/textures/jupiter.jpg"} <= set(apk.namelist()),
                "Solar shader or NASA surface map is absent from the APK")
        for name in sorted(found):
            data = apk.read(name)
            require(data[:6] == b"\x7fELF\x02\x01", "Native library is not little-endian ELF64: " + name)
            require(struct.unpack_from("<H", data, 18)[0] == (183 if "arm64-v8a" in name else 62),
                    "Native ELF machine differs from the ABI folder")
            offset = struct.unpack_from("<Q", data, 32)[0]
            entry_size, count = struct.unpack_from("<HH", data, 54)
            alignments = [struct.unpack_from("<Q", data, offset + index * entry_size + 48)[0]
                          for index in range(count)
                          if struct.unpack_from("<I", data, offset + index * entry_size)[0] == 1]
            require(alignments and all(value >= 16384 for value in alignments), "Native ELF lacks 16 KB load alignment")
            libraries[name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                               "loadAlignments": alignments}

    source_paths = git("ls-tree", "-r", "--name-only", ORIGINAL, "--", "native",
                       "graphics-android/app/src/main/cpp",
                       "graphics-android/app/src/main/java/com/mundox/graphics/NativeCore.java").decode().splitlines()
    require(len(source_paths) == 46, "Unexpected original native/core source inventory")
    original_sources = {}
    for name in source_paths:
        reference = git("show", ORIGINAL + ":" + name)
        current = (ROOT / name).read_bytes()
        require(current == reference, "An original native/core source changed: " + name)
        original_sources[name] = hashlib.sha256(current).hexdigest()
    geometry = "graphics-android/app/src/main/java/com/mundox/graphics/RenderResolution.java"
    require((ROOT / geometry).read_bytes() == git("show", ORIGINAL + ":" + geometry),
            "The shared 4K pixel geometry helper was changed")
    science = (directory / "solar-science.log").read_text()
    require("SolarDataChecks PASS: 511 checks;" in science, "The original 511 scientific checks did not pass")
    commit = git("rev-parse", "HEAD").decode().strip()
    report = {"schemaVersion": 1, "passed": True, "commit": commit, "buildCommit": commit,
              "applicationId": "com.cosmicx.solar.uhd", "version": "1.1.0", "versionCode": 2,
              "minSdk": 26, "targetSdk": 35, "apks": checksums,
              "nativeLibraries": libraries, "signerCertificateSha256": signatures,
              "originalReferenceCommit": ORIGINAL, "originalSourcesUnchanged": True,
              "originalSources": original_sources, "companionIsDelivery": False,
              "sharedResolutionGeometrySha256": hashlib.sha256((ROOT / geometry).read_bytes()).hexdigest(),
              "scientificChecks": 511,
              "signing": "Android development key; v2 signature verified"}
    (directory / "build-provenance.json").write_text(json.dumps(report, indent=2) + "\n")
    (directory / "build-commit.txt").write_text(commit + "\n")
    (directory / "SHA256SUMS.txt").write_text("".join(value["sha256"] + "  " + name + "\n"
                                                      for name, value in checksums.items()))
    print(f"Verified signed UHD APK, two 64-bit ABIs and {len(original_sources)} unchanged original source files.")
    print(f"Build commit: {commit}; Android 26–35; separate identity com.cosmicx.solar.uhd.")


if __name__ == "__main__":
    main()
