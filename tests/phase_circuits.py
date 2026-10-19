"""Shared, explicit input circuits for validation and rival comparisons."""
import math


def adder(m, a, b, superposition=False):
    gates = []
    for i in range(m):
        if a >> (m-1-i) & 1:
            gates.append(("X", i))
        if b >> (m-1-i) & 1:
            gates.append(("X", m+i))
    if superposition:
        gates.append(("H", 0))
    qft = []
    for j in range(m):
        qft.append(("H", m+j))
        for k in range(j+1, m):
            qft.append(("Q", m+k, m+j, math.pi/(2**(k-j))))
    gates.extend(qft)
    for j in range(m):
        for i in range(j, m):
            gates.append(("Q", i, m+j, math.pi/(2**(i-j))))
    gates.extend((g if g[0] == "H" else (*g[:3], -g[3])) for g in reversed(qft))
    return gates


def native_index(m, a, b):
    return sum(((a >> (m-1-i)) & 1) << i for i in range(m)) | sum(
        ((b >> (m-1-i)) & 1) << (m+i) for i in range(m))


def write_program(path, n, gates):
    path.write_text(str(n)+"\n"+"\n".join(" ".join(map(str,g)) for g in gates)+"\n")


def qulacs_circuit(n, gates):
    import numpy as np
    from qulacs import QuantumCircuit
    from qulacs.gate import DenseMatrix
    qc = QuantumCircuit(n)
    for g in gates:
        op, q = g[:2]
        if op in ("H", "X", "Z", "S", "T"):
            getattr(qc, "add_"+op+"_gate")(q)
        elif op == "C":
            qc.add_CNOT_gate(q, g[2])
        elif op == "D":
            qc.add_CZ_gate(q, g[2])
        else:
            target, theta = (q, g[2]) if op == "P" else (g[2], g[3])
            gate = DenseMatrix(target, np.diag([1, np.exp(1j*theta)]))
            if op == "Q":
                gate.add_control_qubit(q, 1)
            qc.add_gate(gate)
    return qc


def qiskit_circuit(n, gates):
    from qiskit import QuantumCircuit
    qc = QuantumCircuit(n)
    for g in gates:
        op, q = g[:2]
        if op in ("H", "X", "Z", "S", "T"):
            getattr(qc, op.lower())(q)
        elif op == "C": qc.cx(q, g[2])
        elif op == "D": qc.cz(q, g[2])
        elif op == "P": qc.p(g[2], q)
        elif op == "Q": qc.cp(g[3], q, g[2])
        else: raise ValueError(g)
    return qc
