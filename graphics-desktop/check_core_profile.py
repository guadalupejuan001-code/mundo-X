"""Verify the 1,000-qubit profile and lossless expansion through the real ABI.

Uses the repository's CompactCore. No dense 2**1000 vector is constructed.
The CLI and portable JSON are checked without requiring a graphics context.
"""
import argparse
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import numpy as np

from core_profile import DEFAULT_QUBITS, MAX_PROJECT_QUBITS
from project import ProjectSession, read_project

ROOT = Path(__file__).resolve().parent


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def local_pauli(n, q, axis):
    return "I" * q + axis + "I" * (n - q - 1)


def run():
    report = {"profile_qubits": 1000, "backend": "compact-clifford",
              "physical_phone_benchmark": False, "gpu_context_created": False,
              "expansions": []}
    with tempfile.TemporaryDirectory(prefix="cosmic-1000-") as directory:
        directory = Path(directory)
        with ProjectSession.create(objects=1024) as initial:
            require(initial.core.n == DEFAULT_QUBITS == MAX_PROJECT_QUBITS == 1000,
                    "New project does not use the full 1,000-qubit register")
            require(len(initial.scene.observables) == 3000,
                    "Default graphic observer dropped quantum channels")
            require(initial.core.counter("state_bytes") == 257000,
                    "Unexpected compact tableau payload")
            before = initial.core.snapshot()
            last = local_pauli(1000, 999, "X")
            initial.core.query([last, "-" + last])
            require(initial.core.snapshot() == before, "Read-only high-qubit query changed history")
            path = initial.save(directory / "default.cosmic")
            document = initial.to_dict()
            with ProjectSession.open(path) as reopened:
                require(reopened.to_dict() == document, "Full default project changed on reopening")
                require(np.array_equal(initial.core.query(initial.scene.observables),
                                       reopened.core.query(reopened.scene.observables)),
                        "Full default observer changed on reopening")
            report["default"] = {"qubits": initial.core.n,
                                 "observable_channels": len(initial.scene.observables),
                                 "native_tableau_bytes": initial.core.counter("state_bytes"),
                                 "project_file_bytes": path.stat().st_size,
                                 "roundtrip_preserved": True}

        for previous in (2, 8, 20, 60):
            with ProjectSession.create(name=f"Mi mundo {previous} — X", qubits=previous,
                                       objects=4096, seed=4294967295, coupling=-.125) as source:
                source.apply([["Y", previous - 1], ["S", 0]])
                source.scene.set_observables([*source.scene.observables,
                                              "-" + "X" * previous,
                                              "+" + "Z" * previous])
                source.edit_many([[7, 6], [4095, 7]])
                source.selected_id = 7
                source.set_time(1.75)
                source.camera["zoom"] = 1.23456789
                source.resolution_limit = 320
                original = source.to_dict()
                snapshot = source.core.snapshot()
                generators = source.core.generators()
                values = source.core.query(source.scene.observables)
                old_path = source.save(directory / f"original-{previous}.cosmic")
                old_bytes = old_path.read_bytes()
                with source.expanded() as expanded:
                    expected = deepcopy(original)
                    expected["core"]["qubits"] = 1000
                    expected["graphics"]["observables"] = [p + "I" * (1000 - previous)
                                                           for p in expected["graphics"]["observables"]]
                    require(expanded.to_dict() == expected, "Expansion lost a world or project field")
                    require(np.array_equal(values, expanded.core.query(expanded.scene.observables)),
                            "Expansion changed an original Pauli expectation")
                    expanded_generators = expanded.core.generators()
                    require(expanded_generators[:previous] == [p + "I" * (1000 - previous) for p in generators],
                            "Expansion did not preserve the complete original stabilizer register")
                    for q in range(previous, 1000):
                        require(expanded_generators[q] == "+" + local_pauli(1000, q, "Z"),
                                "An added qubit was not initialized to zero")
                    path = expanded.save(directory / f"expanded-{previous}.cosmic")
                    with ProjectSession.open(path) as reopened:
                        require(reopened.to_dict() == expected, "Expanded save changed on reopening")
                    expanded.apply([["X", 999]])
                    require(expanded.core.query([local_pauli(1000, 999, "Z")])[0] == -1.0,
                            "Qubit 999 does not respond to a real native gate")
                    report["expansions"].append({"from": previous, "to": 1000,
                                                 "history_state_world_preserved": True,
                                                 "added_zero_qubits": 1000 - previous,
                                                 "high_qubit_gate_verified": True,
                                                 "roundtrip_preserved": True})
                require(source.to_dict() == original and source.core.snapshot() == snapshot,
                        "Expanded copy modified the source session")
                require(old_path.read_bytes() == old_bytes, "Expansion overwrote the old saved project")
                for invalid in (previous, previous - 1, 1001, True, 1000.0):
                    try:
                        with source.expanded(invalid):
                            pass
                    except ValueError:
                        pass
                    else:
                        raise AssertionError("Invalid expansion was accepted")
                require(source.to_dict() == original, "Rejected expansion damaged its source")

        source_path = directory / "original-20.cosmic"
        destination = directory / "cli-1000.cosmic"
        before = source_path.read_bytes()
        completed = subprocess.run([sys.executable, str(ROOT / "demo.py"),
                                    "--project", str(source_path), "--expand-qubits", "1000",
                                    "--gate", "X:999", "--no-render", "--export-project", str(destination)],
                                   capture_output=True, text=True, timeout=60)
        require(completed.returncode == 0, completed.stderr)
        cli = read_project(destination)
        require(cli["core"]["qubits"] == 1000 and cli["core"]["gates"][-1] == ["X", 999],
                "CLI did not expand and operate the high qubit")
        require(source_path.read_bytes() == before, "CLI overwrote the source file")
        report["cli_expansion_and_high_qubit_gate"] = True
    report["status"] = "passed"
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    result = run()
    text = json.dumps(result, indent=2, ensure_ascii=False) + "\n"
    if arguments.output:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(text, encoding="utf-8")
    print(text, end="")
