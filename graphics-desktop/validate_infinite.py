"""Verify live Infinite state, its GLES effect and portable current-state saves."""
import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import numpy as np

from engine import QuantumGraphicsEngine, QuantumScene, build_native, street
from infinite_runtime import InfiniteCore, default_observables, partner_address
from project import ProjectSession, decode_project, encode_project

ROOT = Path(__file__).resolve().parent
A = "1" + "0" * 100
B = partner_address(A)
PROGRAM = default_observables(A, B)


def reject(call):
    try:
        call()
    except (ValueError, RuntimeError):
        return
    raise AssertionError("Invalid request was accepted")


def run():
    build_native()
    checks = {}
    report = {"status": "passed", "checks": checks,
              "scope": "Same InfiniteSession and existing GLES renderer; .cosmic v2 current state",
              "physical_phone_benchmark": False, "cross_driver_pixel_identity_claimed": False}
    with InfiniteCore("bell") as core:
        original = core.snapshot()
        assert original.startswith("MXI1:")
        stats = core.stats()
        assert stats["background"] == "bell" and stats["active_qubits"] == 0
        scene = QuantumScene.from_infinite_core(core, observables=PROGRAM)
        assert scene._core is core and core.snapshot() == original
        assert scene.compile().coefficients.tolist() == [1, -1, 1, 0, 0, 0, 0]
        assert core.stats()["active_qubits"] == 0
        checks["implicit_101_digit_bell_observed_without_materializing"] = True
        engine = QuantumGraphicsEngine(scene, 192, 108)
        cam = street(192, 108)
        try:
            before, _ = engine.observe(cam)
            assert core.snapshot() == original
            # The shader also consumes ray parameters outside the VP matrix.
            changed_camera = dict(cam)
            changed_camera["tan_half_fov"] *= .7
            cached, camera_metrics = engine.observe(changed_camera)
            exact, _ = engine.observe(changed_camera, force=True)
            assert not camera_metrics["reused"]
            assert np.array_equal(cached, exact)
            assert not np.array_equal(before, exact)
            assert core.snapshot() == original
            checks["ray_projection_change_invalidates_frame_cache_exactly"] = True
            batches = scene.observable_batches
            engine.observe(street(192, 108, .2), read=False)
            assert scene.observable_batches == batches and core.snapshot() == original
            checks["camera_and_render_preserve_state_and_rng"] = True

            core.apply([("Z", A)])
            changed_state = core.snapshot()
            changed, _ = engine.observe(cam)
            delta = int(np.any(before != changed, axis=2).sum())
            assert delta > 0 and core.snapshot() == changed_state
            assert scene.compile().coefficients.tolist() == [-1, 1, 1, 0, 0, 0, 0]
            assert core.stats()["active_qubits"] == 2
            checks["gate_on_same_owner_changes_real_pixels"] = {"changed_pixels": delta, "resolution": [192, 108]}

            core.apply([("Z", A)])
            recovered, _ = engine.observe(cam)
            assert np.array_equal(before, recovered)
            checks["second_z_restores_exact_same_driver_image"] = True

            scene.coupling = 0
            neutral, _ = engine.observe(cam)
            values = scene.compile().coefficients.copy()
            core.apply([("Z", A)])
            neutral_after, _ = engine.observe(cam)
            assert np.array_equal(neutral, neutral_after)
            assert not np.array_equal(values, scene.compile().coefficients)
            checks["zero_coupling_control"] = {"changed_pixels": 0, "observable_changed": True}
            scene.coupling = .25

            core.restore(changed_state)
            first_epoch = core.epoch
            first_image, _ = engine.observe(cam)
            with InfiniteCore("bell") as other:
                other.apply([("X", A)])
                assert other.revision == core.revision
                core.restore(other.snapshot())
            assert core.epoch > first_epoch
            other_image, _ = engine.observe(cam)
            assert not np.array_equal(first_image, other_image)
            checks["same_revision_restore_invalidates_visual_epoch"] = True

            stable_state, previous_epoch = core.snapshot(), core.epoch
            scene.set_observables([f"Z:{A}", f"X:{A} X:{B}"])
            assert len(scene.compile().coefficients) == 2
            assert core.snapshot() == stable_state and core.epoch == previous_epoch
            previous = scene.compile()
            reject(lambda: scene.set_observables([f"X:{A} Z:0{A}"]))
            assert scene.compile() is previous and core.snapshot() == stable_state
            scene.set_observables(PROGRAM)
            checks["program_change_invalidates_coefficients_without_changing_core"] = True
            report["driver"] = engine.renderer.gl.info
        finally:
            engine.close()
            scene.close()
        assert core.query([f"Z:{A} Z:{B}"]).tolist() == [-1]
        checks["closing_borrowed_scene_preserves_owner"] = True

        stable_state, stable_stats = core.snapshot(), core.stats()
        invalid = [
            lambda: core.apply([("X", A), ("T", "0")]),
            lambda: core.execute(f"z {A}\nassert expect 1 Z:{A} Z:{B}\n"),
            lambda: core.execute("h 5\nmeasure 5\nunknown\n"),
            lambda: core.execute("x 1\0\nx 2"),
            lambda: core.restore(stable_state[:-1]),
            lambda: core.restore("MXI9:" + stable_state[5:]),
            lambda: core.query(["X:0 Z:00"]),
            lambda: core.query(["X:" + "0" * 4097]),
            lambda: core.apply([("X", 18446744073709551617)]),
            lambda: core.apply([("CX", "00", "0")]),
        ]
        for call in invalid:
            reject(call)
            assert core.snapshot() == stable_state and core.stats() == stable_stats
        checks["invalid_requests_preserve_state_rng_epoch"] = {"cases": len(invalid)}

    with tempfile.TemporaryDirectory(prefix="infinite-graphics-") as folder:
        folder = Path(folder)
        with ProjectSession.create_infinite(first=A, background="bell", objects=1024) as session:
            # Commit a genuinely random measurement before saving. Replaying
            # just the gates or just a seed cannot reproduce this checkpoint.
            session.execute(f"seed 123456789\nmeasure {A}\nh 42\n")
            session.scene.restore_edits([[0, 2], [900, 7]])
            session.time = 1.75
            session.camera = {"mode": "orbit", "overview": False, "yaw": .67, "pitch": .65, "zoom": 1.}
            document = session.to_dict()
            assert document["version"] == 2
            assert set(document["core"]) == {"backend", "snapshot"}
            assert A in document["graphics"]["observables"][0]
            saved = session.save(folder / "infinite.cosmic")
            snapshot = session.core.snapshot()
            expected = session.core.query(session.scene.observables)
            engine = QuantumGraphicsEngine(session.scene, 192, 108)
            try:
                before, _ = engine.observe(session.camera_for(192, 108), t=session.time)
                before = before.copy()
            finally:
                engine.close()
            assert session.core.snapshot() == snapshot
            continuation = "measure 42\nh 43\nmeasure 43\nh 44\nmeasure 44\n"
            expected_results = session.execute(continuation)
            expected_continuation = session.core.snapshot()
        with ProjectSession.open(saved) as loaded:
            assert loaded.to_dict() == document and loaded.core.snapshot() == snapshot
            assert np.array_equal(loaded.core.query(loaded.scene.observables), expected)
            engine = QuantumGraphicsEngine(loaded.scene, 192, 108)
            try:
                after, _ = engine.observe(loaded.camera_for(192, 108), t=loaded.time)
                assert np.array_equal(before, after)
            finally:
                engine.close()
            assert loaded.execute(continuation) == expected_results
            assert loaded.core.snapshot() == expected_continuation
            checks["post_measurement_project_preserves_state_rng_world_and_image"] = True

            before_file = saved.read_bytes()
            before_state = loaded.core.snapshot()
            corruptions = [
                lambda d: d.update(version=1),
                lambda d: d["core"].update(qubits=1000),
                lambda d: d["core"].update(snapshot="MXI1:00"),
                lambda d: d["graphics"].update(observables=["XX"]),
                lambda d: d["graphics"].update(observables=[f"X:{A} Z:0{A}"]),
            ]
            for mutate in corruptions:
                invalid_document = deepcopy(document)
                mutate(invalid_document)
                reject(lambda: ProjectSession(invalid_document))
                assert loaded.core.snapshot() == before_state and saved.read_bytes() == before_file
            reject(lambda: loaded.expanded())
            checks["failed_v2_import_preserves_existing_project"] = len(corruptions)

        encoded = encode_project(document)
        assert decode_project(encoded) == document
        checks["v2_codec_preserves_full_decimal_strings"] = True
        command = [sys.executable, str(ROOT / "demo.py"), "--project", str(saved),
                   "--gate", "Z:" + A, "--no-render", "--export-project", str(folder / "cli.cosmic")]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, result.stderr
        cli_report = json.loads(result.stdout)
        assert cli_report["scene"]["backend"] == "infinite-clifford"
        with ProjectSession.open(folder / "cli.cosmic") as cli_session:
            assert cli_session.core.revision > 0
        checks["desktop_cli_edits_and_exports_101_digit_address"] = True
        report["checkpoint_sha256"] = hashlib.sha256(document["core"]["snapshot"].encode("ascii")).hexdigest()
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = run()
    text = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")
