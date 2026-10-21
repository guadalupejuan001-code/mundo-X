#!/usr/bin/env python3
"""Replay Android .cosmic exports using the desktop build of the same core.

Standard library only: no NumPy, Pillow, Android SDK or GPU context is needed.
The geometric images are verified by Android instrumentation. This independent
host step checks source fixtures, native history, generators and observables,
then compares the compact state with the repository's dense reference.
"""
import argparse
import ctypes as ct
import hashlib
from itertools import product
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from infinite_device_evidence import GRAPHICS_CASES as INFINITE_GRAPHICS_CASES

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "graphics-desktop"))
from project import MAX_FILE_BYTES, read_project

GATE_IDS = {"H": 0, "S": 1, "X": 2, "Z": 3, "CX": 4, "CZ": 5, "Y": 6}
EXPECTED_EXPORTS = {
    "portable-bell-android.cosmic": ("bell-observer.cosmic", True, 2),
    "portable-ghz-android.cosmic": ("ghz-city.cosmic", True, 3),
    "portable-mutated-android.cosmic": ("bell-observer.cosmic", False, 2),
}
LEGACY_DEVICE_TESTS = {
    "default_ether_world_and_finite_compatibility", "native_1000_qubit_invariants", "portable_1000_and_legacy_expansion",
    "native_ether_contracts",
    "native_bell_invariants", "renderer_borrows_native_state", "gles_one_million_objects",
    "gles_maximum_object_limit", "render_observes_without_mutating", "gate_changes_coupled_image",
    "gate_preserves_uncoupled_image", "selection_and_individual_edits", "edit_restoration",
    "pause_resume_preserves_state", "frame_reuse_exact_inputs_and_invalidation", "portable_codec_strict_validation",
    "portable_desktop_fixtures_import", "portable_mutation_export_reimport",
    "portable_invalid_import_preserves_session", "portable_activity_recreation",
}
LEGACY_DEVICE_TESTS |= INFINITE_GRAPHICS_CASES
UHD_DEVICE_TESTS = {
    "uhd_real_framebuffer_and_legacy_return", "uhd_id_selection_and_letterbox",
    "uhd_orientation_roundtrip", "uhd_lifecycle_snapshot_native_preserved",
}
ULTRA_DEVICE_TESTS = {
    "ultra8k_real_framebuffer_and_budget_limits",
    "ultra8k_tiles_match_monolithic_color_and_ids",
    "ultra8k_streamed_png_dimensions_and_rows",
    "ultra8k_failure_preserves_scene_and_native_state",
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def unique_object(pairs):
    value = {}
    for key, item in pairs:
        require(key not in value, "Duplicate evidence JSON field: " + key)
        value[key] = item
    return value


def evidence_json(path):
    with Path(path).open("rb") as stream:
        data = stream.read(MAX_FILE_BYTES + 1)
    require(len(data) <= MAX_FILE_BYTES, "Oversized evidence JSON")
    return json.loads(data.decode("utf-8"), object_pairs_hook=unique_object,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError("Nonfinite JSON: " + value)))


def native_rows(document):
    rows = []
    for gate in document["core"]["gates"]:
        rows.extend((GATE_IDS[gate[0]], gate[1], gate[2] if len(gate) == 3 else 0))
    return rows


class NativeBridge:
    """Small test adapter over the existing public C ABI; no state math here."""
    def __init__(self, binary):
        self.lib = ct.CDLL(str(binary))
        lib = self.lib
        lib.mx_last_error.restype = ct.c_char_p
        lib.mx_create.argtypes = [ct.c_int, ct.c_size_t]
        lib.mx_create.restype = ct.c_void_p
        lib.mx_destroy.argtypes = [ct.c_void_p]
        lib.mx_destroy.restype = None
        lib.mx_restore.argtypes = [ct.c_void_p, ct.POINTER(ct.c_uint64), ct.c_size_t, ct.c_uint64]
        lib.mx_restore.restype = ct.c_int
        lib.mx_query.argtypes = [ct.c_void_p, ct.POINTER(ct.c_char_p), ct.c_size_t, ct.POINTER(ct.c_double)]
        lib.mx_query.restype = ct.c_int
        lib.mx_generator.argtypes = [ct.c_void_p, ct.c_size_t, ct.c_void_p, ct.c_size_t]
        lib.mx_generator.restype = ct.c_int
        lib.mx_history.argtypes = [ct.c_void_p, ct.POINTER(ct.c_uint64), ct.c_size_t]
        lib.mx_history.restype = ct.c_int
        lib.mx_history_count.argtypes = [ct.c_void_p]
        lib.mx_history_count.restype = ct.c_size_t
        lib.mx_revision.argtypes = [ct.c_void_p]
        lib.mx_revision.restype = ct.c_uint64
        lib.mx_state_bytes.argtypes = [ct.c_void_p]
        lib.mx_state_bytes.restype = ct.c_size_t
        lib.mxi_create.argtypes = [ct.c_int]
        lib.mxi_create.restype = ct.c_void_p
        lib.mxi_destroy.argtypes = [ct.c_void_p]
        lib.mxi_destroy.restype = None
        lib.mxi_last_error.argtypes = []
        lib.mxi_last_error.restype = ct.c_char_p
        lib.mxi_restore.argtypes = [ct.c_void_p, ct.c_char_p]
        lib.mxi_restore.restype = ct.c_int
        lib.mxi_snapshot.argtypes = [ct.c_void_p, ct.POINTER(ct.c_void_p)]
        lib.mxi_snapshot.restype = ct.c_int
        lib.mxi_execute.argtypes = [ct.c_void_p, ct.c_char_p, ct.POINTER(ct.c_void_p)]
        lib.mxi_execute.restype = ct.c_int
        lib.mxi_query.argtypes = [ct.c_void_p, ct.POINTER(ct.c_char_p), ct.c_size_t,
                                 ct.POINTER(ct.c_double), ct.POINTER(ct.c_uint64), ct.POINTER(ct.c_uint64)]
        lib.mxi_query.restype = ct.c_int
        lib.mxi_free_string.argtypes = [ct.c_void_p]
        lib.mxi_free_string.restype = None

    def check(self, status):
        if status:
            raise ValueError("Shared native core: " + self.lib.mx_last_error().decode("utf-8", "replace"))

    def replay(self, kind, document):
        core = document["core"]
        handle = self.lib.mx_create(kind, core["qubits"])
        if not handle:
            self.check(-1)
        rows = native_rows(document)
        data = (ct.c_uint64 * len(rows))(*rows)
        try:
            self.check(self.lib.mx_restore(handle, data, len(rows) // 3, core["revision"]))
        except Exception:
            self.lib.mx_destroy(handle)
            raise
        return handle

    def query(self, handle, operators):
        encoded = [value.encode("ascii") for value in operators]
        strings = (ct.c_char_p * len(encoded))(*encoded)
        output = (ct.c_double * len(encoded))()
        self.check(self.lib.mx_query(handle, strings, len(encoded), output))
        return list(output)

    def generators(self, handle, qubits):
        output = []
        for row in range(qubits):
            value = ct.create_string_buffer(qubits + 2)
            self.check(self.lib.mx_generator(handle, row, value, len(value)))
            output.append(value.value.decode("ascii"))
        return output

    def history(self, handle):
        count = self.lib.mx_history_count(handle)
        output = (ct.c_uint64 * (count * 3))()
        self.check(self.lib.mx_history(handle, output, count))
        return list(output)

    def infinite_check(self, status):
        if status:
            raise ValueError("Infinite native core: " + self.lib.mxi_last_error().decode("utf-8", "replace"))

    def infinite_restore(self, document):
        require(document["version"] == 2 and document["core"]["backend"] == "infinite-clifford",
                "Infinite portability requires a v2 document")
        handle = self.lib.mxi_create(0)
        if not handle:
            self.infinite_check(-1)
        try:
            self.infinite_check(self.lib.mxi_restore(handle, document["core"]["snapshot"].encode("ascii")))
            return handle
        except Exception:
            self.lib.mxi_destroy(handle)
            raise

    def infinite_string(self, name, handle, *args):
        out = ct.c_void_p()
        try:
            self.infinite_check(getattr(self.lib, "mxi_" + name)(handle, *args, ct.byref(out)))
            require(bool(out.value), "Missing native string result")
            return ct.string_at(out).decode("ascii")
        finally:
            if out.value:
                self.lib.mxi_free_string(out)

    def infinite_query(self, handle, observables):
        encoded = [value.encode("ascii") for value in observables]
        strings = (ct.c_char_p * len(encoded))(*encoded)
        output = (ct.c_double * len(encoded))()
        revision, epoch = ct.c_uint64(), ct.c_uint64()
        self.infinite_check(self.lib.mxi_query(handle, strings, len(encoded), output,
                                             ct.byref(revision), ct.byref(epoch)))
        return list(output), revision.value, epoch.value


def compile_bridge(directory):
    source = ROOT / "native/src/graphics_core_bridge.cpp"
    infinite_source = ROOT / "native/src/infinite_bridge.cpp"
    binary = Path(directory) / "libcosmic-portability.so"
    result = subprocess.run(["c++", "-std=c++17", "-O2", "-shared", "-fPIC", "-pthread",
                             "-I", str(ROOT / "native/include"), str(source), str(infinite_source), "-o", str(binary)],
                            capture_output=True, text=True, timeout=60)
    require(result.returncode == 0, "Cannot build the canonical desktop bridge:\n" + result.stderr)
    return NativeBridge(binary)


def verify_infinite_native(bridge, document, proof):
    """Import Android's actual state and reproduce subsequent random outcomes."""
    require(type(proof.get("schemaVersion")) is int and proof["schemaVersion"] == 2,
            "Unknown Infinite proof schema")
    require(proof["snapshot"] == document["core"]["snapshot"], "Infinite proof lost its saved current state")
    observables = document["graphics"]["observables"]
    require(proof["observables"] == observables, "Infinite proof changed the original graphic program")
    require(any(len(term.split(":", 1)[-1]) >= 101 for pauli in observables
                for term in pauli.split() if ":" in term), "Infinite export did not preserve a 101-digit address")
    continuation = proof["continuationProgram"]
    require(type(continuation) is str and len(continuation) <= 65536 and "\0" not in continuation
            and any(line.strip().startswith("measure ") for line in continuation.splitlines()),
            "Missing bounded measurement continuation")
    handle = bridge.infinite_restore(document)
    try:
        saved = bridge.infinite_string("snapshot", handle)
        require(saved == proof["snapshot"], "Desktop and Android current-state snapshots differ")
        values, revision, _ = bridge.infinite_query(handle, observables)
        require(values == proof["expectations"], "Desktop and Android Infinite observables differ")
        require(bridge.infinite_string("snapshot", handle) == saved, "Observation changed the imported Infinite state or RNG")
        output = bridge.infinite_string("execute", handle, continuation.encode("ascii"))
        require(output == proof["continuationOutput"], "Desktop and Android measurement outcomes differ after restore")
        require(bridge.infinite_string("snapshot", handle) == proof["continuationSnapshot"],
                "Desktop and Android current state or RNG diverged after continuation")
        return {"backend": "infinite-clifford", "observables": len(observables), "revision": revision,
                "current_state_and_observables_equal": True, "measurement_and_rng_continuation_equal": True,
                "observations_preserved_state": True, "numeric_address_conversion": False}
    finally:
        bridge.lib.mxi_destroy(handle)


def verify_native(bridge, document, proof):
    core, graphics = document["core"], document["graphics"]
    require(type(proof.get("schemaVersion")) is int and proof["schemaVersion"] == 1, "Unknown native proof schema")
    require(proof["qubits"] == core["qubits"] and proof["revision"] == core["revision"],
            "Android native proof disagrees with the portable core")
    require(proof["nativeGateHistory"] == native_rows(document), "Android native history differs from saved gate program")
    require(proof["observables"] == graphics["observables"], "Android native proof lost original visual operators")
    require(len(proof["generators"]) == core["qubits"] and len(proof["expectations"]) == len(graphics["observables"]),
            "Incomplete Android native observations")
    require(1 <= core["qubits"] <= 8, "Evidence reference comparison requires a small checked project")
    compact = dense = None
    try:
        compact = bridge.replay(0, document)
        dense = bridge.replay(1, document)
        require(bridge.lib.mx_revision(compact) == core["revision"], "Desktop lost the Android revision")
        require(bridge.history(compact) == proof["nativeGateHistory"], "Desktop replay changed authoritative history")
        generators = bridge.generators(compact, core["qubits"])
        require(generators == proof["generators"], "Desktop and Android native generators differ")
        values = bridge.query(compact, graphics["observables"])
        require(values == proof["expectations"], "Desktop and Android graphic observables differ")
        require(proof["nativeStateBytes"] > 0 and bridge.lib.mx_state_bytes(compact) > 0, "Missing native state storage")
        operators = ["".join(value) for value in product("IXYZ", repeat=core["qubits"])]
        operators += ["-" + value for value in operators.copy()]
        compact_values = bridge.query(compact, operators)
        dense_values = bridge.query(dense, operators)
        error = max(abs(a - b) for a, b in zip(compact_values, dense_values))
        require(error < 1e-12, "Portable state disagrees with dense reference")
        require(bridge.lib.mx_revision(compact) == core["revision"] and bridge.history(compact) == proof["nativeGateHistory"]
                and bridge.generators(compact, core["qubits"]) == generators, "Reference observations mutated the replayed core")
        return {"qubits": core["qubits"], "gates": len(core["gates"]), "observables": len(graphics["observables"]),
                "objects": graphics["objects"], "history_generators_and_expectations_equal": True,
                "signed_pauli_reference_comparisons": len(operators), "compact_dense_maximum_absolute_error": error,
                "observations_preserved_state": True}
    finally:
        if dense:
            bridge.lib.mx_destroy(dense)
        if compact:
            bridge.lib.mx_destroy(compact)


def verify_directory(directory):
    directory = Path(directory)
    report = evidence_json(directory / "report.json")
    uhd = report.get("metrics", {}).get("uhd4kSuite", False)
    ultra = report.get("metrics", {}).get("ultra8kSuite", False)
    mirrors = report.get("metrics", {}).get("mirrorSuite", False)
    require(type(mirrors) is bool, "Invalid mirror test profile")
    require(type(uhd) is bool and type(ultra) is bool and (not ultra or uhd), "Unknown Android test profile")
    expected = LEGACY_DEVICE_TESTS | (UHD_DEVICE_TESTS if uhd else set()) | (ULTRA_DEVICE_TESTS if ultra else set())
    if mirrors:
        expected.add("mirrors_selection_snapshot_and_tiled_export")
    tests = report["tests"]
    require(report["testsRun"] == len(expected) and report["testsFailed"] == 0,
            "Portable device tests did not all pass")
    require(len(tests) == len(expected) and {test["name"] for test in tests} == expected
            and all(test["passed"] is True for test in tests),
            "Android test inventory does not match the declared legacy/UHD profile")
    exports = report["projectExports"]
    require(len(exports) == 3 and {item["file"] for item in exports} == set(EXPECTED_EXPORTS),
            "Unexpected Android export inventory")
    results = []
    infinite_results = []
    with tempfile.TemporaryDirectory(prefix="cosmic-portability-") as native_directory:
        bridge = compile_bridge(native_directory)
        for item in sorted(exports, key=lambda entry: entry["file"]):
            filename = item["file"]
            source_file, equivalent, qubits = EXPECTED_EXPORTS[filename]
            require(item["stateFile"] == filename + ".state.json" and item["sourceFixture"] == source_file
                    and item["sourceEquivalent"] is equivalent, "Unexpected portable evidence relationship")
            document = read_project(directory / filename)
            proof = evidence_json(directory / item["stateFile"])
            require(proof["projectFile"] == filename and document["core"]["qubits"] == qubits,
                    "Native proof does not belong to this project")
            source = read_project(ROOT / "graphics-desktop/examples" / source_file)
            if equivalent:
                require(document == source, "Android changed desktop project fields while importing/exporting " + filename)
            else:
                require(document["core"]["gates"] == source["core"]["gates"] + [["Y", 0]]
                        and document["core"]["revision"] == source["core"]["revision"] + 1,
                        "Mutated Android export did not preserve and extend its source history")
                graphics = document["graphics"]
                require(graphics["observables"] == source["graphics"]["observables"] and graphics["camera"]["mode"] == "orbit"
                        and graphics["time"] == 1.75 and graphics["resolutionLimit"] == 320,
                        "Mutated export lost camera/time/original observable changes")
                require(dict(graphics["edits"]).get(report["metrics"]["portableEditedId"]) == 7,
                        "Edited object missing from Android export")
            result = verify_native(bridge, document, proof)
            result.update({"file": filename, "sourceFixture": source_file, "unchanged_source_fields_equal": equivalent,
                           "sha256": hashlib.sha256((directory / filename).read_bytes()).hexdigest()})
            results.append(result)
        infinite_exports = report.get("infiniteProjectExports", [])
        require(len(infinite_exports) == 1 and infinite_exports[0]["file"] == "portable-infinite-android.cosmic"
                and infinite_exports[0]["stateFile"] == "portable-infinite-android.cosmic.state.json",
                "Missing Infinite Android project export")
        for item in infinite_exports:
            document = read_project(directory / item["file"])
            proof = evidence_json(directory / item["stateFile"])
            result = verify_infinite_native(bridge, document, proof)
            result.update({"file": item["file"],
                           "sha256": hashlib.sha256((directory / item["file"]).read_bytes()).hexdigest()})
            infinite_results.append(result)
    return {"schemaVersion": 1, "status": "passed", "projects": results, "stdlib_only": True,
            "core_source": "native/src/graphics_core_bridge.cpp", "infinite_projects": infinite_results,
            "infinite_source": "native/src/infinite_bridge.cpp", "gpu_context_created": False,
            "cross_driver_pixel_identity_required": False,
            "scope": "Android exports replayed by the desktop native core; geometry checked separately by device GLES tests"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, nargs="?", default=Path("graphics-evidence"))
    args = parser.parse_args()
    try:
        report = verify_directory(args.directory)
        destination = args.directory / "portability-verification.json"
        destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    except (ValueError, OSError, KeyError, TypeError, subprocess.SubprocessError) as error:
        parser.exit(1, "COSMIC portability verification failed: " + str(error) + "\n")
    comparisons = sum(item["signed_pauli_reference_comparisons"] for item in report["projects"])
    print(f"Verified {len(report['projects'])} Android .cosmic exports with desktop CompactCore and {comparisons} signed Pauli dense-reference comparisons.")
    print("History, generators and original visual expectations match across Android and desktop.")
    print("No GPU or cross-driver pixel equality was required by this portability check.")


if __name__ == "__main__":
    main()
