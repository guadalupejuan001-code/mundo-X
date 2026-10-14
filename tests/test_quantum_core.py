import math
import sys
from pathlib import Path

import torch

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from quantum_core import QuantumStateX, bell_state


def test_hadamard_twice_returns_zero():
    q = QuantumStateX(1)
    q.apply_single_qubit(q.hadamard(), 0)
    q.apply_single_qubit(q.hadamard(), 0)

    expected = torch.tensor([1.0 + 0.0j, 0.0 + 0.0j], dtype=torch.complex64)
    assert torch.allclose(q.amplitudes(), expected, atol=1e-5)


def test_phase_controls_interference():
    q = QuantumStateX(1)
    q.apply_single_qubit(q.hadamard(), 0)
    q.apply_single_qubit(q.phase(math.pi), 0)
    q.apply_single_qubit(q.hadamard(), 0)

    probs = q.probabilities()
    assert probs[1] > 0.999


def test_bell_state():
    q = bell_state()
    probs = q.probabilities()

    expected = torch.tensor([0.5, 0.0, 0.0, 0.5])
    assert torch.allclose(probs, expected, atol=1e-5)
