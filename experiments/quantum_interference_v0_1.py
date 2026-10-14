import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from quantum_core import QuantumStateX, bell_state


def interference_demo():
    q = QuantumStateX(1)

    q.apply_single_qubit(q.hadamard(), 0)
    after_first_h = q.amplitudes()

    q.apply_single_qubit(q.hadamard(), 0)
    after_second_h = q.amplitudes()

    print("INTERFERENCE")
    print("after H:   ", after_first_h)
    print("after H,H: ", after_second_h)
    print("probs:     ", q.probabilities())
    print()


def phase_interference_demo():
    q = QuantumStateX(1)

    q.apply_single_qubit(q.hadamard(), 0)
    q.apply_single_qubit(q.phase(math.pi), 0)
    q.apply_single_qubit(q.hadamard(), 0)

    print("PHASE -> INTERFERENCE")
    print("amplitudes:", q.amplitudes())
    print("probs:     ", q.probabilities())
    print()


def entanglement_demo():
    q = bell_state()

    print("ENTANGLEMENT")
    print("amplitudes:", q.amplitudes())
    print("probs:     ", q.probabilities())
    print("expected: only |00> and |11>, each with probability 0.5")


if __name__ == "__main__":
    interference_demo()
    phase_interference_demo()
    entanglement_demo()
