#!/usr/bin/env python3
"""Audit the 8K APK, preserve the shared session interfaces and identify exact core changes."""

import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import zipfile
from pathlib import Path
from cosmic_source_provenance import verify_integrated_core

ROOT = Path(__file__).resolve().parents[1]
ORIGINAL = "403bdd7311e6d6b37e849fbe2c85375a9a2cd2ac"
APP = "COSMIC-X-Engine-8K.apk"
TESTS = "COSMIC-X-Engine-8K-tests.apk"
COMPANION = "COSMIC-X-Engine-coexist.apk"


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
    directory = Path(sys.argv[1] if len(sys.argv) > 1 else "graphics-8k-dist")
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
    require("package: name='com.mundox.graphics.ultra' versionCode='5' versionName='0.4.0'" in app,
            "Unexpected standalone package identity or version")
    require("package: name='com.mundox.graphics'" in manifests[COMPANION], "Legacy companion package changed")
    require("package: name='com.mundox.graphics.ultra.test'" in manifests[TESTS], "Generated test package does not target UHD")
    require("launchable-activity: name='com.mundox.graphics.MainActivity'" in app,
            "The original graphics activity is not the launcher")
    require("application-label:'COSMIC X Engine 8K'" in app, "Standalone APK title differs")
    require((sdk_level(app, ("sdkVersion", "minSdkVersion")), sdk_level(app, ("targetSdkVersion",))) == (26, 35),
            "Incorrect Android SDK compatibility:\n" + app)
    require("android.permission.INTERNET" not in app, "The standalone renderer must run offline")
    test_tree = subprocess.check_output([str(build_tools / "aapt2"), "dump", "xmltree", str(directory / TESTS),
                                         "--file", "AndroidManifest.xml"], text=True)
    (directory / "test-manifest-tree.txt").write_text(test_tree)
    require('"com.mundox.graphics.GraphicsInstrumentation"' in test_tree
            and '"com.mundox.graphics.ultra"' in test_tree, "Instrumentation runner/target was not packaged correctly")

    expected = {f"lib/{abi}/libmx_graphics.so" for abi in ("arm64-v8a", "x86_64")}
    libraries = {}
    with zipfile.ZipFile(directory / APP) as apk:
        found = {name for name in apk.namelist() if name.endswith("/libmx_graphics.so")}
        require(found == expected, "Unexpected original native ABI inventory: " + str(found))
        require({"assets/shaders/common.glsl", "assets/shaders/eye.vert", "assets/shaders/eye.frag",
                 "assets/projects/bell-observer.cosmic", "assets/projects/ghz-city.cosmic"} <= set(apk.namelist()),
                "Original renderer shader or portable fixture is absent from the APK")
        require(all(name.rsplit("/", 1)[-1] in {"libmx_graphics.so", "libc++_shared.so"}
                    for name in apk.namelist() if name.endswith(".so")), "Unexpected external native engine in APK")
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

    provenance = verify_integrated_core(ROOT)
    optimized_names = {"native/include/mundo_x/compact_core.hpp", "native/include/mundo_x/pauli_observer.hpp"}
    optimized_sources = {name: {key: provenance["sourceChangesFromOriginal"][name][key]
                                for key in ("baselineSha256", "sha256")}
                         for name in optimized_names if name in provenance["sourceChangesFromOriginal"]}
    require(set(optimized_sources) == optimized_names, "Core optimization source inventory is incomplete")
    commit = git("rev-parse", "HEAD").decode().strip()
    report = {"schemaVersion": 1, "passed": True, "commit": commit,
              "applicationId": "com.mundox.graphics.ultra", "version": "0.4.0", "versionCode": 5,
              "minSdk": 26, "targetSdk": 35, "apks": checksums,
              "nativeLibraries": libraries, "signerCertificateSha256": signatures,
              "baselineCommit": ORIGINAL, "originalCoreInterfacesPreserved": True,
              "optimizedCoreFiles": optimized_sources, "companionIsDelivery": False,
              "signing": "Android development key; v2 signature verified"}
    report.update(provenance)
    (directory / "build-provenance.json").write_text(json.dumps(report, indent=2) + "\n")
    (directory / "build-commit.txt").write_text(commit + "\n")
    (directory / "SHA256SUMS.txt").write_text("".join(value["sha256"] + "  " + name + "\n"
                                                      for name, value in checksums.items()))
    print(f"Verified signed 8K APK, two 64-bit ABIs and {len(provenance['coreSourceHashes'])} declared integrated core sources.")
    print(f"Build commit: {commit}; Android 26–35; separate identity com.mundox.graphics.ultra.")


if __name__ == "__main__":
    main()
