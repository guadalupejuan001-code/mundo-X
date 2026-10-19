"""Check portable projects through the real shared core and GLES observer."""
import argparse
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import numpy as np

from core_runtime import Core
from engine import QuantumGraphicsEngine, QuantumScene, build_native
from project import MAX_EDITS, MAX_FILE_BYTES, MAX_GATES, MAX_REVISION, ProjectSession, decode_project, read_project, validate_project, write_project

ROOT = Path(__file__).resolve().parent


def state(core):
    return core.snapshot(), tuple(core.generators())


def render(session, width=192, height=108):
    renderer = QuantumGraphicsEngine(session.scene, width, height)
    try:
        before = state(session.core)
        image, _ = renderer.observe(session.camera_for(width, height), t=session.time, selected=session.selected_id)
        assert state(session.core) == before, "Observation changed core state/history"
        return image.copy(), renderer.renderer.gl.info
    finally:
        renderer.close()


def must_reject(call):
    try:
        call()
    except ValueError:
        return
    raise AssertionError("Malformed project accepted")


def run():
    build_native()
    report = {"title": "COSMIC X Engine", "profile": "cosmic-x-engine-project v1 / compact-clifford / opaque-grid-v1",
              "checks": {}, "cross_device_pixel_identity_claimed": False, "physical_phone_benchmark": False}
    checks = report["checks"]
    with tempfile.TemporaryDirectory(prefix="cosmic-project-check-") as folder:
        folder = Path(folder)
        cases = []
        for example in sorted((ROOT / "examples").glob("*.cosmic")):
            document = read_project(example)
            with ProjectSession(document) as first:
                before = state(first.core)
                first_image, driver = render(first)
                path = first.save(folder / example.name)
                assert first.to_dict() == document
                assert state(first.core) == before
                expectations = first.core.query(first.scene.observables).copy()
            with ProjectSession.open(path) as second:
                assert second.to_dict() == document
                assert state(second.core) == before
                assert np.array_equal(second.core.query(second.scene.observables), expectations)
                second_image, _ = render(second)
                delta = int(np.any(first_image != second_image, axis=2).sum())
                assert delta == 0, "Roundtrip changed pixels on the same renderer"
            cases.append({"project": example.name, "objects": document["graphics"]["objects"],
                          "qubits": document["core"]["qubits"], "observables": len(document["graphics"]["observables"]),
                          "state_history_observables_and_world_equal": True, "same_driver_pixel_differences": delta})
            report["driver"] = driver
        checks["two_projects_one_unmodified_engine"] = cases

        document = read_project(ROOT / "examples/bell-observer.cosmic")
        with ProjectSession(document) as session:
            engine = QuantumGraphicsEngine(session.scene, 192, 108)
            try:
                camera = session.camera_for(192, 108)
                first, _ = engine.observe(camera)
                before, epoch = state(session.core), session.core.epoch
                batches = session.scene.observable_batches
                moved = deepcopy(session.camera)
                moved["eye"][0] += 4
                session.camera = moved
                engine.observe(session.camera_for(192, 108), read=False)
                assert state(session.core) == before and session.core.epoch == epoch
                assert session.scene.observable_batches == batches
                checks["camera_preserves_state_history_and_cached_observables"] = True

                # Replacing a live state's history with the same persisted
                # revision must invalidate the borrowed observer's cache.
                saved_revision = session.core.revision
                session.core.restore([("H", 0), ("CX", 0, 1), ("Z", 0)], saved_revision)
                assert session.core.revision == saved_revision and session.core.epoch != epoch
                changed, _ = engine.observe(camera)
                delta = int(np.any(first != changed, axis=2).sum())
                assert delta > 0
                assert session.scene.observable_batches == batches + 1
                checks["same_revision_restore_invalidates_live_observer"] = {"changed_pixels": delta}

                # Coupling changes must upload even if no gate/revision changes.
                session.scene.coupling = 0.
                neutral, _ = engine.observe(camera)
                session.core.apply([("Z", 0)])
                neutral_after, _ = engine.observe(camera)
                assert np.array_equal(neutral, neutral_after)
                checks["zero_coupling_gate_control"] = {"changed_pixels": 0}
            finally:
                engine.close()

            # No per-object exception disappears when population is reduced.
            original_edits = session.scene.edits
            session.scene.count = 1
            session.selected_id = -1
            small = session.to_dict()
            assert small["graphics"]["edits"] == document["graphics"]["edits"]
            small_path = session.save(folder / "reduced-population.cosmic")
            with ProjectSession.open(small_path) as small_session:
                render(small_session, 64, 64)
                small_session.scene.count = 1024
                assert small_session.scene.edits == original_edits
            checks["lowered_population_retains_original_channels_and_inactive_edits"] = True

            # Bad imports/exports must leave an existing live project and file
            # intact. These include valid JSON with unsupported semantics.
            before = state(session.core), session.to_dict()
            rejected = 0
            mutations = [
                lambda d: d.update(version=2),
                lambda d: d.update(version=True),
                lambda d: d.update(externalState="file:///tmp/state"),
                lambda d: d["core"].update(qubits=257),
                lambda d: d["core"].update(qubits=True),
                lambda d: d["core"].update(gates=[["T", 0]]),
                lambda d: d["core"].update(gates=[["CX", 0, 0]]),
                lambda d: d["core"].update(gates=[["H", .5]]),
                lambda d: d["core"].update(gates=[["X", 2]]),
                lambda d: d["graphics"].update(seed=4_294_967_296),
                lambda d: d["graphics"].update(observables=["X!"]),
                lambda d: d["graphics"].update(edits=[[62, 1], [0, 2]]),
                lambda d: d["graphics"].update(edits=[[1, 2], [1, 3]]),
                lambda d: d["graphics"].update(coupling=float("nan")),
                lambda d: d["graphics"].update(selectedId=1024),
                lambda d: d["graphics"]["camera"].update(target=d["graphics"]["camera"]["eye"]),
                lambda d: d["graphics"]["camera"].update(eye=[1000000,1,0], target=[999999.9999,1,0]),
                lambda d: d["graphics"]["camera"].update(near=1, far=1.000000001),
                lambda d: d.update(name="\ud800"),
            ]
            for mutate in mutations:
                invalid = deepcopy(document)
                mutate(invalid)
                must_reject(lambda: ProjectSession(invalid))
                rejected += 1
                assert (state(session.core), session.to_dict()) == before
            for invalid_bytes in (
                b'{"format":"a","format":"b"}',
                b'{"number":NaN}',
                b'\xff',
                b' ' * (MAX_FILE_BYTES + 1),
            ):
                must_reject(lambda: decode_project(invalid_bytes))
                rejected += 1
            stable_file = folder / "stable.cosmic"
            write_project(stable_file, document)
            stable_bytes = stable_file.read_bytes()
            invalid = deepcopy(document)
            invalid["graphics"]["objects"] = 0
            must_reject(lambda: write_project(stable_file, invalid))
            assert stable_file.read_bytes() == stable_bytes
            checks["malformed_projects_rejected_without_active_state_or_file_mutation"] = {"rejected_cases": rejected + 1}
            unicode_project = deepcopy(document)
            unicode_project["name"] = "\U0001f680" * 128
            unicode_path = write_project(folder / "unicode.cosmic", unicode_project)
            assert read_project(unicode_path)["name"] == unicode_project["name"]
            unicode_project["name"] += "\U0001f680"
            must_reject(lambda: validate_project(unicode_project))
            checks["unicode_name_counts_codepoints_and_accepts_valid_supplementary_characters"] = True

        # Real native state at the portable revision boundary. Rejected writes
        # must preserve mathematical state, history, observer epoch and files.
        maximum_revision = deepcopy(document)
        maximum_revision["core"]["revision"] = MAX_REVISION
        with ProjectSession(maximum_revision) as session:
            before = state(session.core), session.core.epoch, session.to_dict()
            must_reject(lambda: session.apply([("X", 0)]))
            session.apply([])
            assert (state(session.core), session.core.epoch, session.to_dict()) == before
            stable = session.save(folder / "maximum-revision.cosmic")
            stable_bytes = stable.read_bytes()
            cli = subprocess.run([sys.executable, str(ROOT / "demo.py"), "--project", str(stable),
                                  "--gate", "X:0", "--export-project", str(stable), "--no-render"],
                                 text=True, capture_output=True, cwd=ROOT)
            assert cli.returncode == 2 and "revision" in cli.stderr
            assert stable.read_bytes() == stable_bytes
            # The general native wrapper still supports its broader uint64
            # range. Export rejects an out-of-profile result before replacement.
            session.core.apply([("X", 0)])
            assert session.core.revision == MAX_REVISION + 1
            must_reject(lambda: session.save(stable))
            assert stable.read_bytes() == stable_bytes
        checks["portable_revision_boundary_rejects_gate_without_mutation"] = True
        checks["desktop_cli_uses_portable_gate_guard"] = True
        checks["general_core_remains_unrestricted_and_bad_export_preserves_file"] = True

        # 100k one-qubit gates exercise the real history count boundary cheaply;
        # neither the state nor the native bridge is mocked.
        full_history = deepcopy(document)
        full_history["core"].update(qubits=1, gates=[["X", 0]] * (MAX_GATES - 1))
        full_history["graphics"]["observables"] = ["Z", "X", "Y"]
        with ProjectSession(full_history) as session:
            assert session.core.query(["Z"])[0] == -1.
            session.apply([("X", 0)])
            assert session.core.query(["Z"])[0] == 1.
            before = state(session.core), session.core.epoch
            assert len(before[0][0]["gates"]) == MAX_GATES
            must_reject(lambda: session.apply([("Z", 0)]))
            assert (state(session.core), session.core.epoch) == before
        checks["portable_gate_history_boundary_uses_real_100000_gate_state"] = True

        full_edits = deepcopy(document)
        full_edits["graphics"].update(objects=1_048_576, edits=[[i, 1] for i in range(MAX_EDITS)])
        with ProjectSession(full_edits) as session:
            session.edit(0, 2)  # Updating an existing ID does not consume another slot.
            before = session.scene.edits, session.scene.scene_revision, state(session.core)
            must_reject(lambda: session.edit(MAX_EDITS, 1))
            assert (session.scene.edits, session.scene.scene_revision, state(session.core)) == before
            session.edit_many([(0, 0), (MAX_EDITS, 1)])
            assert len(session.scene.edits) == MAX_EDITS and session.scene.edits[-1] == (MAX_EDITS, 1)
            assert session.scene.edits[0][0] == 1
            session.set_time(1_000_000.)
            before = session.time, state(session.core), session.core.epoch
            for invalid_time in (1_000_000.25, -1_000_000.25, float("nan"), float("inf"), True, "1"):
                must_reject(lambda value=invalid_time: session.set_time(value))
                assert (session.time, state(session.core), session.core.epoch) == before
            must_reject(lambda: setattr(session, "time", float("nan")))
            assert session.time == before[0]
            session.set_time(-1_000_000)
            assert session.to_dict()["graphics"]["time"] == -1_000_000
        checks["portable_edit_boundary_allows_replacement_but_rejects_growth"] = True
        checks["portable_time_boundary_rejects_without_core_or_time_mutation"] = True

        # Export a core that was owned/mutated elsewhere. Its native history is
        # authoritative; the observer's original operators must not be rebuilt
        # from the altered state's current generators during import.
        core = Core(3)
        scene = None
        try:
            core.apply([("H", 0), ("CX", 0, 1), ("CX", 1, 2)])
            scene = QuantumScene.from_core(core, count=1024)
            original_observables = scene.observables
            external = ProjectSession.from_scene(scene, name="Núcleo prestado")
            core.apply([("S", 1), ("Y", 2), ("CZ", 0, 2)])
            assert core.generators() != list(original_observables[:core.n])
            before = state(core)
            path = external.save(folder / "borrowed.cosmic")
            external.close()
            assert state(core) == before, "Closing a borrowed session closed or changed its owner"
            with ProjectSession.open(path) as imported:
                assert imported.scene.observables == original_observables
                assert state(imported.core) == before
                assert np.array_equal(imported.core.query(original_observables), core.query(original_observables))
                assert ["Y", 2] in imported.to_dict()["core"]["gates"]
            checks["external_gate_history_y_and_original_observables_survive_export"] = True
        finally:
            if scene is not None:
                scene.close()
            core.close()
    report["status"] = "passed"
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = json.dumps(run(), ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(result, encoding="utf-8")
    print(result, end="")
