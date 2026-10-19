#!/usr/bin/env python3
"""Render or edit a .cosmic project with the existing core/observer pair."""
import argparse
import json
from pathlib import Path
import time

from project import ProjectSession


def gate(text):
    fields = text.upper().split(":")
    try:
        return [fields[0], *[int(q) for q in fields[1:]]]
    except ValueError as error:
        raise argparse.ArgumentTypeError("Use --gate H:0 or --gate CX:0:1") from error


def edit(text):
    try:
        fields = [int(q) for q in text.split(":")]
        if len(fields) != 2:
            raise ValueError()
        return fields
    except ValueError as error:
        raise argparse.ArgumentTypeError("Use --edit ID:STATE (state 0..7)") from error


def main():
    parser = argparse.ArgumentParser(description="COSMIC X Engine · Núcleo cuántico + Motor gráfico cuántico")
    parser.add_argument("--project", type=Path, help="Open an existing portable .cosmic project")
    parser.add_argument("--export-project", type=Path, help="Save the complete resulting .cosmic project atomically")
    parser.add_argument("--name", help="Project title")
    parser.add_argument("--objects", type=int, help="Population 1..1048576 (new project default: 1000000)")
    parser.add_argument("--qubits", type=int, help="New project qubits (default: 20)")
    parser.add_argument("--seed", type=int, help="Unsigned 32-bit procedural seed (new project default: 2026)")
    parser.add_argument("--coupling", type=float, help="Graphic coupling -.25...25 (new project default: .25)")
    parser.add_argument("--view", choices=("overview", "street"), help="Select an orbit camera")
    parser.add_argument("--gate", action="append", default=[], type=gate, help="Apply gate, e.g. H:0 or CX:0:1; repeatable")
    parser.add_argument("--edit", action="append", default=[], type=edit, help="Edit ID:STATE; states 1..6 raise,7 hide,0 restore")
    parser.add_argument("--time", type=float, help="Fixed animation time stored in the project")
    parser.add_argument("--width", type=int, default=640)
    parser.add_argument("--height", type=int, default=360)
    parser.add_argument("--output", type=Path, default=Path("cosmic-observation.png"))
    parser.add_argument("--no-render", action="store_true", help="Edit/export without creating a GLES context")
    args = parser.parse_args()
    if args.project and args.qubits is not None:
        parser.error("--qubits is for new projects; an imported state keeps its qubit count")
    if not 1 <= args.width <= 4096 or not 1 <= args.height <= 4096:
        parser.error("--width and --height must be 1..4096")
    if args.qubits is not None and not 1 <= args.qubits <= 256:
        parser.error("Portable projects support 1..256 qubits")
    started = time.perf_counter_ns()
    try:
        if args.project:
            session = ProjectSession.open(args.project)
        else:
            session = ProjectSession.create(name=args.name or "Mundo X", objects=args.objects if args.objects is not None else 1_000_000,
                                            qubits=args.qubits or 20, seed=args.seed if args.seed is not None else 2026,
                                            coupling=args.coupling if args.coupling is not None else .25)
        with session:
            if args.name is not None:
                session.name = args.name
            if args.objects is not None:
                session.scene.count = args.objects
                if session.selected_id >= args.objects:
                    session.selected_id = -1
            if args.seed is not None:
                session.scene.seed = args.seed
            if args.coupling is not None:
                session.scene.coupling = args.coupling
            if args.view:
                session.camera = {"mode": "orbit", "overview": args.view == "overview", "yaw": .67, "pitch": .65, "zoom": 1.}
            if args.time is not None:
                session.set_time(args.time)
            if args.gate:
                session.apply(args.gate)
            if args.edit:
                session.edit_many(args.edit)
            session.to_dict()  # Validate before creating output files.
            report = {"title": "COSMIC X Engine", "project": session.name, "scene": session.scene.describe()}
            if args.export_project:
                report["exported_project"] = str(session.save(args.export_project).resolve())
            if not args.no_render:
                from engine import QuantumGraphicsEngine
                from PIL import Image
                engine = QuantumGraphicsEngine(session.scene, args.width, args.height)
                try:
                    before = session.core.snapshot()
                    frame, timing = engine.observe(session.camera_for(args.width, args.height),
                                                   t=session.time, selected=session.selected_id)
                    if session.core.snapshot() != before:
                        raise RuntimeError("Observing changed the authoritative core")
                    Image.fromarray(frame).save(args.output)
                    report.update({"output": str(args.output.resolve()), "resolution": [args.width, args.height],
                                   "driver": engine.renderer.gl.info, "frame": timing, "observation_preserved_core": True})
                finally:
                    engine.close()
            report["run_with_build_and_output_ms"] = (time.perf_counter_ns() - started) / 1e6
            print(json.dumps(report, ensure_ascii=False, indent=2))
    except (ValueError, OSError, RuntimeError) as error:
        parser.exit(2, f"COSMIC X Engine: {error}\n")


if __name__ == "__main__":
    main()
