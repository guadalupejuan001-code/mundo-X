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
RULE_COMPACTION_REFERENCE = "35a82f88c88e40e9c2fcba7453b33fc91f51ffca"
INTERVAL_EVALUATION_REFERENCE = "6baf22bed1f8b964de2ba24bb702c6a9a3b789d4"
OBSERVER_PIVOT_REFERENCE = "00e2f014186d9db9d2d87b3f7aabee3338d2b2a5"
MICRO_BOUNDS_REFERENCE = "201388f2c98ed64a5260abcdafa1957a125468e7"
PHYSICS_SPATIAL_REFERENCE = "59916f0ae34196d43914ce069a042ee80782287d"
RECOVERED_PHYSICS_REFERENCE = "02e69b81f18329e38d5f0e32a5a082be27bffa3c"
GRAPHICS_MANIFEST_REFERENCE = "4f04322eed3b698e4f20081921c5ed1da2c4b721"
QUANTUM_ALGORITHMS_REFERENCE = "9ce470f30383d3fd673a45bf09c5e4265fb1ad26"
ETHER_MEMORY_REFERENCE = "729994e1e044ad5a4d4ffd4c43dcea2ad638608f"
# Reviewable commit containing the new COW sources and reproducible benchmark.
ETHER_MEMORY_COW_REFERENCE = "b1279b30381974ee82b4c1198eb31c1605b92b48"
# Existing radial sources landed on main without inventory references.
MIL_RADIAL_MAIN_REFERENCE = "d4a63cafdb47dc237b07c0d48fe2cf3e254e54b7"
ETHER_MEMORY_FABRIC_REFERENCE = "8b9d5d40319ff093ea019156b8b476754ed6d504"
UNIVERSAL_1000_REFERENCE = "018176d45cf1743256b56761c43b4e8656d3848f"
UNIVERSAL_1000_EXAMS_REFERENCE = "dc38a5a76ed8a39a415d587b1ee86934063c2240"
MAGIC_GROWTH_REFERENCE = "6cc71c139394399a92d96b21e03c497c8e548d4a"
ETHER_RENDER_SOURCE_REFERENCE = "f2dee1f4f074a12b30ba8a31357790d90f934669"
ETHER_RENDER_INTEGRATION_REFERENCE = "2de68561ef0f2c9208e6f151dd163db996e2133c"
MIL_RENDER_INTEGRATION_REFERENCE = "0817158c866e5708991355b0464b961e09304307"
SPATIAL_RENDER_REFERENCE = "db79b0a1e805ccadcf3172abbf8966501ebea872"
ETHER_ENGINE_FACADE_REFERENCE = "ce6f48a1a0cad4d00bc83e79b25d7c4f2e1b0e39"
SURFACE_BUFFER_REFERENCE = "e295da32e682db8bf681598dda86a2a4a8142db6"
SOURCE_SCOPES = (
    "native",
    "graphics-android/app/src/main/cpp",
    "graphics-android/app/src/main/java/com/mundox/graphics/NativeCore.java",
    "graphics-android/app/src/main/java/com/mundox/graphics/InfiniteProgram.java",
    "graphics-android/app/src/main/java/com/mundox/graphics/EtherCoreNative.java",
)
ADDITIONAL_SOURCES = {
    "native/include/mundo_x/physics_world.hpp": RECOVERED_PHYSICS_REFERENCE,
    "native/tests/physics_world.cpp": PHYSICS_SPATIAL_REFERENCE,
    "native/include/mundo_x/rule_physics.hpp": RECOVERED_PHYSICS_REFERENCE,
    "native/tests/rule_physics.cpp": RECOVERED_PHYSICS_REFERENCE,
    "native/tests/physics_allocation.cpp": RECOVERED_PHYSICS_REFERENCE,
    "native/tests/infinite_extreme.cpp": RECOVERED_PHYSICS_REFERENCE,
    "native/tests/ether_rules.cpp": INTERVAL_EVALUATION_REFERENCE,
    "native/tests/ether_memory.cpp": ETHER_MEMORY_REFERENCE,
    "native/include/mundo_x/cow_sharded_map.hpp": ETHER_MEMORY_COW_REFERENCE,
    "native/include/mundo_x/inverse_memory.hpp": ETHER_MEMORY_COW_REFERENCE,
    "native/include/mundo_x/quantum_runtime.hpp": ETHER_MEMORY_COW_REFERENCE,
    "native/src/quantum_runner.cpp": "832f219112000e0a8d33c0482b1b341a0c5d9502",
    "native/tests/inverse_memory.cpp": ETHER_MEMORY_COW_REFERENCE,
    "native/tests/quantum_runtime.cpp": ETHER_MEMORY_COW_REFERENCE,
    "native/tests/ether_memory_copy_benchmark.cpp": ETHER_MEMORY_COW_REFERENCE,
    "native/include/mundo_x/mil_radial_evolution.hpp": MIL_RADIAL_MAIN_REFERENCE,
    "native/tests/mil_radial_evolution.cpp": MIL_RADIAL_MAIN_REFERENCE,
    "native/tests/reduced_1000.cpp": MAGIC_GROWTH_REFERENCE,
    "native/tests/ether_batch.cpp": AUDIT_REFERENCE,
    "native/tests/ether_stress.cpp": AUDIT_REFERENCE,
    "native/tests/core_performance_benchmark.cpp": RESOLUTION_REFERENCE,
    "native/src/basis_vm_main.cpp": "9d095a81b3820f4c50fc48fecd9c7ff08bd22b40",
    "native/include/mundo_x/infinite_address.hpp": INFINITE_REFERENCE,
    "native/include/mundo_x/infinite_core.hpp": INTERVAL_EVALUATION_REFERENCE,
    "native/src/infinite_runner.cpp": INTEGRATION_REFERENCE,
    "native/tests/infinite_address.cpp": INFINITE_REFERENCE,
    "native/tests/infinite_core.cpp": INFINITE_REFERENCE,
    "native/tests/infinite_scale.cpp": INFINITE_REFERENCE,
    "native/tests/infinite_tensor.cpp": INFINITE_REFERENCE,
    "native/include/mundo_x/infinite_bridge.h": INTEGRATION_REFERENCE,
    "native/include/mundo_x/infinite_bridge.hpp": INTEGRATION_REFERENCE,
    "native/include/mundo_x/infinite_program.hpp": "2ad6b51a22fd108e80b0edfb1377069e025ee861",
    "native/include/mundo_x/infinite_session.hpp": ETHER_MEMORY_FABRIC_REFERENCE,
    "native/src/infinite_bridge.cpp": INTEGRATION_REFERENCE,
    "native/tests/infinite_bridge.cpp": INTEGRATION_REFERENCE,
    "native/tests/infinite_session.cpp": INTEGRATION_REFERENCE,
    "graphics-android/app/src/main/java/com/mundox/graphics/InfiniteProgram.java": ETHER_FUSION_REFERENCE,
    "native/include/mundo_x/ether_core.hpp": INTERVAL_EVALUATION_REFERENCE,
    "native/tests/ether_core.cpp": ETHER_REFERENCE,
    "native/tests/ether_frame.cpp": ETHER_FUSION_REFERENCE,
    "native/tests/ether_fusion.cpp": AUDIT_REFERENCE,
    "native/tests/quantum_algorithms.cpp": QUANTUM_ALGORITHMS_REFERENCE,
    "native/include/mundo_x/ether_renderer.hpp": MIL_RENDER_INTEGRATION_REFERENCE,
    "native/include/mundo_x/ether_mil_bridge.hpp": MIL_RENDER_INTEGRATION_REFERENCE,
    "native/tests/ether_mil_renderer.cpp": MIL_RENDER_INTEGRATION_REFERENCE,
    "native/include/mundo_x/ether_render_spatial.hpp": SURFACE_BUFFER_REFERENCE,
    "native/include/mundo_x/ether_surface_buffer.hpp": SURFACE_BUFFER_REFERENCE,
    "native/include/mundo_x/ether_render_engine.hpp": ETHER_ENGINE_FACADE_REFERENCE,
    "native/tests/ether_render_spatial.cpp": SPATIAL_RENDER_REFERENCE,
    "native/tests/ether_surface_buffer.cpp": SURFACE_BUFFER_REFERENCE,
    "native/tests/rule_renderer.cpp": ETHER_RENDER_SOURCE_REFERENCE,
    "native/tests/xasm_renderer.cpp": MIL_RENDER_INTEGRATION_REFERENCE,
    "graphics-android/app/src/main/java/com/mundox/graphics/EtherCoreNative.java": ETHER_FUSION_REFERENCE,
}
REVISED_SOURCES = {
    "native/tests/native_original_exams.cpp": UNIVERSAL_1000_EXAMS_REFERENCE,
    "native/include/mundo_x/exact_t_experiment.hpp": MAGIC_GROWTH_REFERENCE,
    "native/include/mundo_x/adaptive_phase_core.hpp": UNIVERSAL_1000_REFERENCE,
    "native/include/mundo_x/xasm.hpp": MIL_RENDER_INTEGRATION_REFERENCE,
    "native/src/phase_runner.cpp": UNIVERSAL_1000_REFERENCE,
    "native/tests/boundaries.cpp": UNIVERSAL_1000_REFERENCE,
    "native/tests/xasm_60.cpp": UNIVERSAL_1000_REFERENCE,
    "native/src/main.cpp": ETHER_RENDER_INTEGRATION_REFERENCE,
    "graphics-android/app/src/main/cpp/PROVENANCE.json": GRAPHICS_MANIFEST_REFERENCE,
    "native/include/mundo_x/pauli_observer.hpp": OBSERVER_PIVOT_REFERENCE,
    "native/tests/core_1000.cpp": OBSERVER_PIVOT_REFERENCE,
    "native/include/mundo_x/micro_lab.hpp": MICRO_BOUNDS_REFERENCE,
    "native/include/mundo_x/compact_core.hpp": INTEGRATION_REFERENCE,
    "graphics-android/app/src/main/cpp/CMakeLists.txt": INTEGRATION_REFERENCE,
    "graphics-android/app/src/main/cpp/graphics_jni.cpp": ETHER_FUSION_REFERENCE,
    "graphics-android/app/src/main/java/com/mundox/graphics/NativeCore.java": ETHER_FUSION_REFERENCE,
}
# The canonical X Render observer adds oriented ellipsoids and a new exactness
# test in this audited commit. Unrelated native sources keep their historical pins.
ELLIPSOID_RENDER_REFERENCE = "e4a896b7473e0d7a5a2ca25afe640a4f49eace75"
# New API and exact triangle test are reviewed in a separately pinned commit.
TRIANGLE_RENDER_REFERENCE = "3b1688a572d844aaac3ef9f27a03923fd9bde0b2"
ADDITIONAL_SOURCES.update({
    "native/include/mundo_x/ether_render_spatial.hpp": TRIANGLE_RENDER_REFERENCE,
    "native/include/mundo_x/ether_render_engine.hpp": TRIANGLE_RENDER_REFERENCE,
    "native/tests/ether_ellipsoid.cpp": ELLIPSOID_RENDER_REFERENCE,
    "native/tests/ether_triangle.cpp": TRIANGLE_RENDER_REFERENCE,
})

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
