import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from quantum_core import QuantumStateX


def show(label, q):
    amps = q.amplitudes()
    probs = q.probabilities()

    print(f"\n{label}")
    print("amplitudes :", amps)
    print("probabilities:", probs)


def main():
    print("=== HELLO MUNDO X ===")

    # 1) Start from the simplest quantum state: |0>
    q = QuantumStateX(1)
    show("1. Initial state |0>", q)

    # 2) Hadamard: one state becomes a coherent superposition.
    q.apply_single_qubit(q.hadamard(), 0)
    show("2. Superposition H|0>", q)

    # 3) Change only relative phase.
    # Probabilities remain 50/50 here, but the state is NOT the same.
    q.apply_single_qubit(q.phase(math.pi), 0)
    show("3. Same probabilities, different phase", q)

    # 4) Interfere both amplitudes with another Hadamard.
    # The hidden phase difference becomes an observable result: |1>.
    q.apply_single_qubit(q.hadamard(), 0)
    show("4. Interference result", q)

    result = int(q.probabilities().argmax().item())

    print("\nMundo X says:", result)
    print("Expected deterministic result: 1")


if __name__ == "__main__":
    main()
