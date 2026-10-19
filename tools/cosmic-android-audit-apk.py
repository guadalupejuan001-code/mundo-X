#!/usr/bin/env python3
"""Audit the actual unified APK payload, application identity and signing evidence."""

import hashlib
import json
import re
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

APP = "COSMIC-X-Engine-0.3.0.apk"
TESTS = "COSMIC-X-Engine-0.3.0-tests.apk"
COMPANION = "Mundo-X-Graphics-coexist.apk"
ABIS = {"arm64-v8a", "x86_64"}
LIBRARIES = {"libmx_graphics.so", "libmundox.so", "libmundox_basis.so", "libmundox_echoes.so"}
ACTIVITIES = {
    "com.mundox.cosmic.CosmicActivity",
    "com.mundox.graphics.MainActivity",
    "com.mundox.echoes.MainActivity",
    *("com.mundox.lab." + name for name in (
        "XLabActivity", "MicroLabActivity", "ObjectLabActivity", "BasisVmActivity",
        "QuantumInterfaceActivity", "VisualExperimentActivity", "GpuVisualActivity",
        "ProceduralGameDemoActivity", "SyncedGpuActivity", "InternetSyncActivity",
        "DeviceLauncherActivity", "PerformanceLauncherActivity", "MainActivity",
    )),
}


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def dex_classes(data):
    """Read defined class descriptors, rather than treating references as packaged classes."""
    require(data[:4] == b"dex\n" and len(data) >= 112, "Unsupported DEX header")
    u32 = lambda offset: struct.unpack_from("<I", data, offset)[0]
    string_count, string_offset = u32(56), u32(60)
    type_count, type_offset = u32(64), u32(68)
    class_count, class_offset = u32(96), u32(100)
    require(string_offset + string_count * 4 <= len(data)
            and type_offset + type_count * 4 <= len(data)
            and class_offset + class_count * 32 <= len(data), "DEX table exceeds payload")

    def string(index):
        require(index < string_count, "Invalid DEX string index")
        position = u32(string_offset + index * 4)
        for _ in range(5):
            require(position < len(data), "Truncated DEX string length")
            value = data[position]
            position += 1
            if value < 128:
                break
        else:
            raise SystemExit("Oversized DEX string length")
        end = data.find(b"\0", position)
        require(end >= position, "Unterminated DEX class descriptor")
        # Class descriptors used by this Java project contain ASCII identifiers.
        return data[position:end].decode("utf-8")

    classes = set()
    for index in range(class_count):
        type_index = u32(class_offset + index * 32)
        require(type_index < type_count, "Invalid DEX class type index")
        classes.add(string(u32(type_offset + type_index * 4)))
    return classes


def main():
    directory = Path(sys.argv[1] if len(sys.argv) > 1 else "cosmic-dist")
    report = {"schemaVersion": 1, "applicationId": "com.mundox.cosmic", "version": "0.3.0"}
    checksums = {}
    signatures = {}
    for name in (APP, TESTS, COMPANION):
        data = (directory / name).read_bytes()
        checksums[name] = hashlib.sha256(data).hexdigest()
        with zipfile.ZipFile(directory / name) as apk:
            require(apk.testzip() is None, "APK ZIP integrity failure: " + name)
        signature = (directory / (name[:-4] + "-signature.txt")).read_text()
        certificates = re.findall(r"^Signer #\d+ certificate SHA-256 digest: ([0-9a-fA-F]{64})$",
                                  signature, re.MULTILINE)
        require(len(certificates) == 1 and "Verifies" in signature,
                "APK signature was not independently verified: " + name)
        signatures[name] = certificates[0].lower()
    require(signatures[APP] == signatures[TESTS], "Application and instrumentation signing certificates differ")

    badging = (directory / (APP[:-4] + "-manifest.txt")).read_text()
    require(re.search(r"package: name='com\.mundox\.cosmic' versionCode='1' versionName='0\.3\.0'", badging),
            "Wrong unified application ID or version")
    require("minSdkVersion:'26'" in badging and "targetSdkVersion:'35'" in badging,
            "Wrong supported Android SDK profile")
    require("launchable-activity: name='com.mundox.cosmic.CosmicActivity'" in badging,
            "The new CosmicActivity is not the launcher")
    require("application-label:'COSMIC X Engine'" in badging, "The application title differs")
    require("package: name='com.mundox.graphics'" in
            (directory / (COMPANION[:-4] + "-manifest.txt")).read_text(),
            "The coinstallation companion is not the original graphics application ID")

    with zipfile.ZipFile(directory / APP) as apk:
        native = {name for name in apk.namelist() if name.startswith("lib/") and name.endswith(".so")}
        required = {f"lib/{abi}/{library}" for abi in ABIS for library in LIBRARIES}
        allowed = required | {f"lib/{abi}/libc++_shared.so" for abi in ABIS}
        require(required <= native and native <= allowed, "Unexpected native libraries or ABIs: " + str(sorted(native)))
        classes = set()
        for name in apk.namelist():
            if re.fullmatch(r"classes\d*\.dex", name):
                classes.update(dex_classes(apk.read(name)))
        required_classes = {"L" + name.replace(".", "/") + ";" for name in ACTIVITIES}
        require(required_classes <= classes, "An experiment Activity is missing from the actual DEX: "
                + str(sorted(required_classes - classes)))
        forbidden = {name for name in classes if any(part in name.lower() for part in
                     ("/pc/", "/windows/", "/x86/", "pcemulator", "windowsemulator"))}
        require(not forbidden, "Windows/PC emulator classes were packaged: " + str(sorted(forbidden)))
        other_code = {name for name in classes if not name.startswith(("Lcom/mundox/", "Lj$/"))}
        require(not other_code, "An unapproved external Java package was bundled: " + str(sorted(other_code)))
        assets = {"assets/projects/bell-observer.cosmic", "assets/projects/ghz-city.cosmic",
                  "assets/shaders/common.glsl", "assets/shaders/eye.vert", "assets/shaders/eye.frag"}
        require(assets <= set(apk.namelist()), "An original graphics shader or project fixture is missing")
        report["nativeLibraries"] = {name: hashlib.sha256(apk.read(name)).hexdigest() for name in sorted(native)}
        report["definedClasses"] = len(classes)
        report["includedActivities"] = sorted(ACTIVITIES)
        report["windowsEmulatorPackaged"] = False

    app_tree = (directory / "app-manifest-tree.txt").read_text()
    require(all(name in app_tree for name in ACTIVITIES), "An included Activity is not declared in the manifest")
    test_tree = (directory / "test-manifest-tree.txt").read_text()
    for runner in ("com.mundox.cosmic.CosmicInstrumentation", "com.mundox.graphics.GraphicsInstrumentation",
                   "com.mundox.lab.MicroLabInstrumentation"):
        require(runner in test_tree, "Instrumentation runner is missing: " + runner)
    require("com.mundox.cosmic.test.projects" in test_tree, "Test-only document provider is missing")
    require("com.mundox.cosmic.test.projects" not in app_tree, "The test document provider leaked into the application")

    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    report.update({"passed": True, "buildCommit": commit, "apkSha256": checksums,
                   "certificateSha256": signatures, "experimentActivities": 14,
                   "coinstallationCompanionIsDelivery": False})
    (directory / "SHA256SUMS.txt").write_text("".join(value + "  " + name + "\n" for name, value in checksums.items()))
    (directory / "build-commit.txt").write_text(commit + "\n")
    (directory / "apk-audit.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Verified com.mundox.cosmic 0.3.0, {len(ACTIVITIES)} activities and {len(required)} native ABI/library pairs.")
    print("The Windows emulation project is absent from the actual native and DEX payloads.")
    print("The previous graphics APK is included solely for installation isolation validation.")


if __name__ == "__main__":
    main()
