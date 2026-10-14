# Mundo X native benchmark — 2026-10-13

Environment:
- Linux x86_64
- g++ (Debian 14.2.0-19) 14.2.0
- compiled with -O3

Commands and observed outputs:

## Hello

./mundo_x hello

HELLO_MUNDO_X dense=PASS compact=PASS

## 24-qubit GHZ — Compact only

/usr/bin/time -v ./mundo_x bench 24

COMPACT qubits=24 state_bytes=408 state_mib=0.000 ghz=PASS ms=0.004
Maximum resident set size: 1664 kB

## 24-qubit GHZ — Compact + Dense

/usr/bin/time -v ./mundo_x bench 24 --dense

COMPACT qubits=24 state_bytes=408 state_mib=0.000 ghz=PASS ms=0.004
DENSE qubits=24 state_bytes=134217728 state_mib=128.000 ghz=PASS ms=516.590
Maximum resident set size: 132608 kB

Note: the process RSS above contains both compact and dense runs because the command executes both in one process. The compact-only process measured 1664 kB peak RSS.

## 10,000-qubit GHZ — Compact

/usr/bin/time -v ./mundo_x bench 10000

COMPACT qubits=10000 state_bytes=25130000 state_mib=23.966 ghz=PASS ms=684.724
Maximum resident set size: 26112 kB

Interpretation:
- DenseCore stores a full complex64 state vector: O(2^n).
- CompactCore uses a bit-packed stabilizer-tableau representation: O(n^2) bits.
- These compact results apply to the supported Clifford/stabilizer family (H, S, X, Z, CNOT), not arbitrary quantum states or arbitrary non-Clifford circuits.
