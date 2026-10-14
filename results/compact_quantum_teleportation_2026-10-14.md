# Compact quantum teleportation — 2026-10-14

Mundo X now includes a reproducible quantum-state teleportation experiment
implemented directly on Compact Core.

## What is teleported

The experiment teleports the **quantum state of q0** to **q2**.

It does **not** move a physical particle or matter. The source state is consumed
by the Bell measurement and reconstructed at the destination after classical
feed-forward corrections.

Protocol:

```
source: q0
Bell pair: q1-q2
destination: q2

prepare source
H(q1)
CNOT(q1,q2)
CNOT(q0,q1)
H(q0)
measure Z(q0) -> m0
measure Z(q1) -> m1
X(q2)^m1
Z(q2)^m0
```

Compact Core gained projective Z measurement for explicit reproducible branches,
including stabilizer row reduction and phase tracking.

## CI verification

GitHub Actions run #33 executed:

```
./native-build/mundo_x teleport
```

Result:

```
|0>   branches 00 01 10 11 -> +Z PASS
|1>   branches 00 01 10 11 -> -Z PASS
|+>   branches 00 01 10 11 -> +X PASS
|->   branches 00 01 10 11 -> -X PASS
|+i>  branches 00 01 10 11 -> +Y PASS
|-i>  branches 00 01 10 11 -> -Y PASS

passed=24/24
compact_state_bytes=51
result=PASS
```

This covers the six single-qubit stabilizer eigenstates and all four Bell
measurement branches.

## Scope

This is a correct teleportation test inside the current
Clifford/stabilizer domain of Compact Core.

It demonstrates:

- entanglement resource creation;
- Bell-measurement circuit;
- projective measurement branches;
- classical conditional correction;
- preservation of the input stabilizer state at the destination;
- compact state representation throughout the experiment.

It does not demonstrate:

- transport of matter;
- physical quantum hardware;
- arbitrary non-Clifford state teleportation.

The next scientific extension would be to support a state outside the stabilizer
octahedron and verify teleportation without falling back to a dense statevector.
