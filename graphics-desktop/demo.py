#!/usr/bin/env python3
"""Render or edit a .cosmic project with the existing core/observer pair."""
import argparse
import json
from pathlib import Path
import time

from project import ProjectSession
from core_profile import DEFAULT_QUBITS, MAX_PROJECT_QUBITS
from infinite_format import address, default_observables


def gate(text):
    fields = text.upper().split(":")
    try:
        return [fields[0], *[address(q) for q in fields[1:]]]
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
    parser.add_argument("--qubits", type=int, help=f"New project qubits (default: {DEFAULT_QUBITS})")
    parser.add_argument("--infinite", choices=("zero", "bell"), help="Create an Infinite project with this rule background")
    parser.add_argument("--address", help="First decimal address in the Infinite graphic program")
    parser.add_argument("--partner", help="Second decimal address; default is the Bell partner of --address")
    parser.add_argument("--command", action="append", default=[], help="Atomic .xi request on Infinite; repeatable")
    parser.add_argument("--expand-qubits", type=int,
                        help="Expand an imported project, adding |0> qubits while preserving its state and world")
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
    if args.infinite and (args.project or args.qubits is not None or args.expand_qubits is not None):
        parser.error("--infinite creates a new addressed project; omit --project/--qubits/--expand-qubits")
    if args.partner is not None and args.address is None:
        parser.error("--partner requires --address")
    if args.project and args.qubits is not None:
        parser.error("--qubits is for new projects; an imported state keeps its qubit count")
    if args.expand_qubits is not None and not args.project:
        parser.error("--expand-qubits requires --project; use --qubits for a new project")
    if args.expand_qubits is not None and not 1 <= args.expand_qubits <= MAX_PROJECT_QUBITS:
        parser.error(f"--expand-qubits must be 1..{MAX_PROJECT_QUBITS} and greater than the imported register")
    if not 1 <= args.width <= 4096 or not 1 <= args.height <= 4096:
        parser.error("--width and --height must be 1..4096")
    if args.qubits is not None and not 1 <= args.qubits <= MAX_PROJECT_QUBITS:
        parser.error(f"Portable projects support 1..{MAX_PROJECT_QUBITS} qubits")
    started = time.perf_counter_ns()
    try:
        if args.project:
            if args.expand_qubits is not None:
                with ProjectSession.open(args.project) as original:
                    session = original.expanded(args.expand_qubits)
            else:
                session = ProjectSession.open(args.project)
        elif args.infinite:
            session = ProjectSession.create_infinite(name=args.name or "Mundo X · Infinito",
                       objects=args.objects if args.objects is not None else 1_000_000,
                       background=args.infinite, first=args.address or "0", second=args.partner,
                       seed=args.seed if args.seed is not None else 2026,
                       coupling=args.coupling if args.coupling is not None else .25)
        else:
            session = ProjectSession.create(name=args.name or "Mundo X", objects=args.objects if args.objects is not None else 1_000_000,
                                            qubits=args.qubits if args.qubits is not None else DEFAULT_QUBITS,
                                            seed=args.seed if args.seed is not None else 2026,
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
            if args.address is not None:
                if session.core.backend != "infinite":
                    raise ValueError("--address requires an Infinite project")
                session.scene.set_observables(default_observables(args.address,args.partner))
            if args.gate:
                gates = args.gate if session.core.backend == "infinite" else [
                    [row[0], *[int(q) for q in row[1:]]] for row in args.gate]
                session.apply(gates)
            command_results = [session.execute(command) for command in args.command]
            if args.edit:
                session.edit_many(args.edit)
            session.to_dict()  # Validate before creating output files.
            report = {"title": "COSMIC X Engine", "project": session.name, "scene": session.scene.describe()}
            if command_results:
                report["command_results"] = command_results
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
