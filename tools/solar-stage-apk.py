#!/usr/bin/env python3
"""Verify the packaged ABIs and record the exact APK/source provenance."""

import hashlib
import json
import re
import subprocess
import sys
import zipfile
from pathlib import Path


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def sdk_level(manifest, names):
    # Modern aapt2 calls this minSdkVersion; older aapt used sdkVersion.
    labels = "|".join(re.escape(name) for name in names)
    values = re.findall(r"^(?:" + labels + r"):\s*'([0-9]+)'\s*$", manifest, re.MULTILINE)
    return int(values[0]) if len(values) == 1 else None


def main():
    directory = Path(sys.argv[1] if len(sys.argv) > 1 else "solar-dist")
    root = Path(__file__).resolve().parent.parent
    names = ("COSMIC-X-Solar.apk", "COSMIC-X-Solar-tests.apk")
    expected_libraries = {
        "lib/arm64-v8a/libmx_graphics.so",
        "lib/x86_64/libmx_graphics.so",
    }
    with zipfile.ZipFile(directory / names[0]) as apk:
        found = {name for name in apk.namelist() if name.endswith("/libmx_graphics.so")}
        require(found == expected_libraries, "Missing or unexpected COSMIC native ABI: " + str(found))
        require(apk.testzip() is None, "APK archive integrity failure")
        require("classes.dex" in apk.namelist(), "APK has no compiled Android classes")
        libraries = {name: {"bytes": len(apk.read(name)),
                            "sha256": hashlib.sha256(apk.read(name)).hexdigest()}
                     for name in sorted(expected_libraries)}
    with zipfile.ZipFile(directory / names[1]) as test_apk:
        require(test_apk.testzip() is None, "Test APK archive integrity failure")
    manifest = (directory / "apk-manifest.txt").read_text()
    require("package: name='com.cosmicx.solar'" in manifest, "Unexpected application ID")
    require("launchable-activity: name='com.mundox.solar.SolarActivity'" in manifest,
            "Solar activity is not the launcher")
    min_sdk = sdk_level(manifest, ("minSdkVersion", "sdkVersion"))
    target_sdk = sdk_level(manifest, ("targetSdkVersion",))
    require((min_sdk, target_sdk) == (26, 35),
            f"Unexpected Android API compatibility: minSdk={min_sdk}, targetSdk={target_sdk}\n"
            + "Actual aapt2 badging:\n" + manifest)
    require("android.permission.INTERNET" not in manifest, "The solar app must run fully offline")
    signatures = {}
    for name in ("apk-signature.txt", "test-apk-signature.txt"):
        signature = (directory / name).read_text()
        require("Verified using v2 scheme (APK Signature Scheme v2): true" in signature,
                "APK v2 signature was not verified: " + name)
        digest = re.search(r"Signer #1 certificate SHA-256 digest: ([0-9a-f]{64})", signature)
        require(digest is not None, "No signing certificate digest: " + name)
        signatures[name] = digest.group(1)
    require(len(set(signatures.values())) == 1, "App and test APKs use different signing certificates")
    sha = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    files = {name: {"bytes": (directory / name).stat().st_size,
                    "sha256": hashlib.sha256((directory / name).read_bytes()).hexdigest()}
             for name in names}
    reused_paths = (
        "graphics-android/app/src/main/java/com/mundox/graphics/NativeCore.java",
        "graphics-android/app/src/main/cpp/graphics_jni.cpp",
        "native/src/graphics_core_bridge.cpp",
    )
    reused = {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in reused_paths}
    (directory / "SHA256SUMS.txt").write_text("".join(
        entry["sha256"] + "  " + name + "\n" for name, entry in files.items()))
    (directory / "build-commit.txt").write_text(sha + "\n")
    provenance = {"schemaVersion": 1, "applicationId": "com.cosmicx.solar",
                  "minSdk": min_sdk, "targetSdk": target_sdk,
                  "buildCommit": sha, "signing": "Android debug certificate; APK v2 verified",
                  "signerCertificateSha256": next(iter(signatures.values())),
                  "apks": files, "nativeLibraries": libraries, "originalCoreSources": reused}
    (directory / "build-provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    print(f"Verified Android API compatibility: minSdk={min_sdk}, targetSdk={target_sdk}")
    print("Verified signed arm64-v8a and x86_64 solar APKs at commit " + sha)


if __name__ == "__main__":
    main()
