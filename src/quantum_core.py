import math
import torch


class QuantumStateX:
    """Minimal state-vector quantum simulator."""

    def __init__(self, num_qubits: int, state: torch.Tensor | None = None):
        if num_qubits < 1:
            raise ValueError("num_qubits must be >= 1")

        self.num_qubits = int(num_qubits)
        dim = 2 ** self.num_qubits

        if state is None:
            state = torch.zeros(dim, dtype=torch.complex64)
            state[0] = 1.0 + 0.0j
        else:
            state = state.to(torch.complex64).flatten()
            if state.numel() != dim:
                raise ValueError(f"state must have {dim} amplitudes")

        self.state = self._normalize(state)

    @staticmethod
    def _normalize(state: torch.Tensor) -> torch.Tensor:
        norm = torch.linalg.vector_norm(state)
        if norm == 0:
            raise ValueError("quantum state cannot have zero norm")
        return state / norm

    @staticmethod
    def hadamard() -> torch.Tensor:
        s = 1.0 / math.sqrt(2.0)
        return torch.tensor([[s, s], [s, -s]], dtype=torch.complex64)

    @staticmethod
    def pauli_x() -> torch.Tensor:
        return torch.tensor([[0, 1], [1, 0]], dtype=torch.complex64)

    @staticmethod
    def phase(theta: float) -> torch.Tensor:
        return torch.tensor(
            [
                [1.0 + 0.0j, 0.0 + 0.0j],
                [0.0 + 0.0j, complex(math.cos(theta), math.sin(theta))],
            ],
            dtype=torch.complex64,
        )

    def apply_single_qubit(self, gate: torch.Tensor, qubit: int):
        if not 0 <= qubit < self.num_qubits:
            raise ValueError("invalid qubit index")

        gate = gate.to(torch.complex64)
        if gate.shape != (2, 2):
            raise ValueError("single-qubit gate must be 2x2")

        psi = self.state.reshape([2] * self.num_qubits)
        psi = torch.movedim(psi, qubit, 0)
        original_shape = psi.shape
        psi = psi.reshape(2, -1)
        psi = gate @ psi
        psi = psi.reshape(original_shape)
        psi = torch.movedim(psi, 0, qubit)

        self.state = psi.reshape(-1)
        return self

    def apply_cnot(self, control: int, target: int):
        if control == target:
            raise ValueError("control and target must differ")
        if not (0 <= control < self.num_qubits and 0 <= target < self.num_qubits):
            raise ValueError("invalid qubit index")

        out = torch.zeros_like(self.state)

        for index, amp in enumerate(self.state):
            bits = list(format(index, f"0{self.num_qubits}b"))
            if bits[control] == "1":
                bits[target] = "0" if bits[target] == "1" else "1"
            new_index = int("".join(bits), 2)
            out[new_index] += amp

        self.state = out
        return self

    def amplitudes(self) -> torch.Tensor:
        return self.state.clone()

    def probabilities(self) -> torch.Tensor:
        return self.state.abs().square()

    def measure_once(self, generator=None) -> int:
        probs = self.probabilities()
        index = torch.multinomial(probs, 1, generator=generator).item()

        collapsed = torch.zeros_like(self.state)
        collapsed[index] = 1.0 + 0.0j
        self.state = collapsed
        return index

    def fidelity(self, other: "QuantumStateX") -> float:
        if self.num_qubits != other.num_qubits:
            raise ValueError("states must have same number of qubits")

        overlap = torch.vdot(self.state, other.state)
        return float((overlap.abs() ** 2).real)


def bell_state() -> QuantumStateX:
    """Create (|00> + |11>) / sqrt(2)."""
    q = QuantumStateX(2)
    q.apply_single_qubit(q.hadamard(), 0)
    q.apply_cnot(0, 1)
    return q
