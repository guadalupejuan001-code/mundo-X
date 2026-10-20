#!/usr/bin/env python3
"""Verify the integrated core against the exact, reviewed source revisions.

The 4K, 8K and Solar builds share the canonical cores and the fused Ether
session. They must not claim that the older 4K core is unchanged. This manifest verifies every file
in the shared native source inventory and records the actual baseline delta.
The initial InfiniteCore extension and its session/graphics integration each
have an immutable source reference. Unchanged sources retain their historical
origins; every new or revised native/JNI source is verified byte for byte.
"""

from __future__ import annotations

import hashlib
import json
import subprocess
from pathlib import Path

ORIGINAL_REFERENCE = "403bdd7311e6d6b37e849fbe2c85375a9a2cd2ac"
CORE_REFERENCE = "ef4636240af73351a18710e988346791fbdc3b58"
RESOLUTION_REFERENCE = "9fd8fc7865cfee6f91430ad4726d6ed841f2b3f6"
INFINITE_REFERENCE = "a2de628ffec3d3b3c3cf797a0bac3e42984b5471"
INTEGRATION_REFERENCE = "d53adc3dc834b4d8400a66230d24d646d9dafb32"
ETHER_REFERENCE = "d351e83ca3be47fa63a14c462b358ac0cb5d65d7"
ETHER_INTEGRATION_REFERENCE = "61917d9c00d90f384a7d98f4f83c5a81399576e8"
ETHER_FUSION_REFERENCE = "d4b9893ddec9c6fbce3911f3db349b1011686538"
AUDIT_REFERENCE = "46c78131db919e045c052987fd302c5919286560"
QUANTUM_ALGORITHMS_REFERENCE = "9ce470f30383d3fd673a45bf09c5e4265fb1ad26"
SOURCE_SCOPES = (
    "native",
    "graphics-android/app/src/main/cpp",
    "graphics-android/app/src/main/java/com/mundox/graphics/NativeCore.java",
    "graphics-android/app/src/main/java/com/mundox/graphics/InfiniteProgram.java",
    "graphics-android/app/src/main/java/com/mundox/graphics/EtherCoreNative.java",
)
ADDITIONAL_SOURCES = {
    "native/tests/ether_batch.cpp": AUDIT_REFERENCE,
    "native/tests/ether_stress.cpp": AUDIT_REFERENCE,
    "native/tests/core_performance_benchmark.cpp": RESOLUTION_REFERENCE,
    "native/src/basis_vm_main.cpp": "9d095a81b3820f4c50fc48fecd9c7ff08bd22b40",
    "native/include/mundo_x/infinite_address.hpp": INFINITE_REFERENCE,
    "native/include/mundo_x/infinite_core.hpp": AUDIT_REFERENCE,
    "native/src/infinite_runner.cpp": INTEGRATION_REFERENCE,
    "native/tests/infinite_address.cpp": INFINITE_REFERENCE,
    "native/tests/infinite_core.cpp": INFINITE_REFERENCE,
    "native/tests/infinite_scale.cpp": INFINITE_REFERENCE,
    "native/tests/infinite_tensor.cpp": INFINITE_REFERENCE,
    "native/include/mundo_x/infinite_bridge.h": INTEGRATION_REFERENCE,
    "native/include/mundo_x/infinite_bridge.hpp": INTEGRATION_REFERENCE,
    "native/include/mundo_x/infinite_program.hpp": ETHER_FUSION_REFERENCE,
    "native/include/mundo_x/infinite_session.hpp": AUDIT_REFERENCE,
    "native/src/infinite_bridge.cpp": INTEGRATION_REFERENCE,
    "native/tests/infinite_bridge.cpp": INTEGRATION_REFERENCE,
    "native/tests/infinite_session.cpp": INTEGRATION_REFERENCE,
    "graphics-android/app/src/main/java/com/mundox/graphics/InfiniteProgram.java": ETHER_FUSION_REFERENCE,
    "native/include/mundo_x/ether_core.hpp": AUDIT_REFERENCE,
    "native/tests/ether_core.cpp": ETHER_REFERENCE,
    "native/tests/ether_frame.cpp": ETHER_FUSION_REFERENCE,
    "native/tests/ether_fusion.cpp": AUDIT_REFERENCE,
    "native/tests/quantum_algorithms.cpp": QUANTUM_ALGORITHMS_REFERENCE,
    "graphics-android/app/src/main/java/com/mundox/graphics/EtherCoreNative.java": ETHER_FUSION_REFERENCE,
}
REVISED_SOURCES = {
    "native/include/mundo_x/compact_core.hpp": INTEGRATION_REFERENCE,
    "graphics-android/app/src/main/cpp/CMakeLists.txt": INTEGRATION_REFERENCE,
    "graphics-android/app/src/main/cpp/graphics_jni.cpp": ETHER_FUSION_REFERENCE,
    "graphics-android/app/src/main/java/com/mundox/graphics/NativeCore.java": ETHER_FUSION_REFERENCE,
}
RESOLUTION_SOURCE = "graphics-android/app/src/main/java/com/mundox/graphics/RenderResolution.java"


def _git(root, *arguments):
    try:
        return subprocess.check_output(["git", *arguments], cwd=root, stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as error:
        raise ValueError("Source provenance requires the complete Git history: "
                         + error.stderr.decode("utf-8", "replace").strip()) from error


def _inventory(root, reference):
    return set(_git(root, "ls-tree", "-r", "--name-only", "-z", reference,
                    "--", *SOURCE_SCOPES).decode().rstrip("\0").split("\0")) - {""}


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def verify_integrated_core(root):
    """Return verified source hashes, origins and changes; reject any extra delta."""
    root = Path(root).resolve()
    expected = {name: CORE_REFERENCE for name in _inventory(root, CORE_REFERENCE)}
    if len(expected) < len(SOURCE_SCOPES):
        raise ValueError("The 1,000-qubit reference source inventory is missing")
    for name, reference in REVISED_SOURCES.items():
        if name not in expected:
            raise ValueError("Revised source does not belong to the core baseline: " + name)
        expected[name] = reference
    for name, reference in ADDITIONAL_SOURCES.items():
        if name in expected:
            raise ValueError("Additional source unexpectedly belongs to the core baseline: " + name)
        expected[name] = reference
    actual = set(_git(root, "ls-files", "--cached", "--others", "--exclude-standard", "-z",
                      "--", *SOURCE_SCOPES).decode().rstrip("\0").split("\0")) - {""}
    if actual != set(expected):
        raise ValueError("Undeclared integrated core inventory: missing="
                         + repr(sorted(set(expected) - actual)) + "; extra="
                         + repr(sorted(actual - set(expected))))
    hashes = {}
    for name, reference in sorted(expected.items()):
        path = root / name
        if not path.is_file() or path.is_symlink():
            raise ValueError("Missing or non-regular shared core source: " + name)
        current = path.read_bytes()
        original = _git(root, "show", reference + ":" + name)
        if current != original:
            raise ValueError("Unreviewed shared core change relative to " + reference + ": " + name)
        hashes[name] = _sha(current)

    baseline = _inventory(root, ORIGINAL_REFERENCE)
    changes = {}
    for name in sorted(baseline | set(expected)):
        old_hash = _sha(_git(root, "show", ORIGINAL_REFERENCE + ":" + name)) if name in baseline else None
        new_hash = hashes.get(name)
        if old_hash != new_hash:
            changes[name] = {"baselineSha256": old_hash, "sha256": new_hash,
                             "status": "added" if old_hash is None else "removed" if new_hash is None else "modified"}

    resolution = (root / RESOLUTION_SOURCE).read_bytes()
    if resolution != _git(root, "show", RESOLUTION_REFERENCE + ":" + RESOLUTION_SOURCE):
        raise ValueError("Unreviewed shared resolution geometry change: " + RESOLUTION_SOURCE)
    return {
        "originalReferenceCommit": ORIGINAL_REFERENCE,
        "originalSourcesUnchanged": not changes,
        "coreReferenceCommit": CORE_REFERENCE,
        "coreSourcesVerified": True,
        "coreSourceHashes": hashes,
        "coreSourceReferenceCommits": dict(sorted(expected.items())),
        "infiniteCoreExtension": {
            "referenceCommit": INFINITE_REFERENCE,
            "scope": "Initial independent InfiniteCore reference. Unchanged sources retain "
                     "these pins; session and graphics deltas are declared separately.",
            "sourceReferenceCommits": {
                name: reference for name, reference in sorted(expected.items())
                if reference == INFINITE_REFERENCE
            },
        },
        "infiniteSessionIntegration": {
            "referenceCommit": INTEGRATION_REFERENCE,
            "scope": "Live shared session, exact portable state and RNG, bounded C/JNI "
                     "transport, packed stabilizer import and sparse Java input validation.",
            "sourceReferenceCommits": {
                name: reference for name, reference in sorted(expected.items())
                if reference == INTEGRATION_REFERENCE
            },
        },
        "etherStateFusion": {
            "referenceCommit": ETHER_FUSION_REFERENCE,
            "scope": "One physical state U_Ether phi, with signed global/local operations, "
                     "physical measurements, transactional shared owners, MXI1/MXI2 and RNG. "
                     "Independent complex oracle and production Java/JNI contracts.",
            "sourceReferenceCommits": {
                name: reference for name, reference in sorted(expected.items())
                if reference == ETHER_FUSION_REFERENCE
            },
        },
        "sourceChangesFromOriginal": changes,
        "sharedResolutionReferenceCommit": RESOLUTION_REFERENCE,
        "sharedResolutionGeometrySha256": _sha(resolution),
    }


if __name__ == "__main__":
    print(json.dumps(verify_integrated_core(Path(__file__).resolve().parents[1]), indent=2))
