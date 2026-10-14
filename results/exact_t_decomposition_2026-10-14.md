# Exact T-gate decomposition experiment — 2026-10-14

## Goal

Extend the current Compact Core experiment beyond pure Clifford/stabilizer evolution
without pretending that a single stabilizer tableau can represent a general
non-Clifford state.

The exact identity used is:

```
T = alpha I + beta Z
```

with

```
alpha = (1 + exp(i*pi/4)) / 2
beta  = (1 - exp(i*pi/4)) / 2
```

Applying `T` to a stabilizer state therefore creates an exact linear
combination of two stabilizer branches.

No pruning, truncation, approximate merging or heuristic compression is used in
this first experiment.

---

## Representation

Each branch contains:

- one Compact Core stabilizer tableau;
- one complex coefficient;
- one packed bit indicating the I/Z choice for every T gate.

A shared circuit log preserves the exact circuit semantics.

This circuit log is important because Compact Core intentionally does not track a
physical state's global phase. The branch-choice history gives a phase convention
that can be replayed exactly for validation.

Dense statevectors are used only as a validation oracle for small circuits. They
are not stored inside each compact branch.

---

## Gates supported by the experiment

Clifford operations continue to update each Compact Core branch:

```
H
S
X
Z
CNOT
CZ
```

A `T` operation branches:

```
current branch
  |- alpha * I branch
  |- beta  * Z branch
```

Therefore, without branch reduction:

```
number of branches = 2^(number of T gates)
```

This is expected and is deliberately preserved as the baseline behavior.

---

## Validation strategy

For each test:

1. the original circuit is run directly with Dense Core using a real T gate;
2. every stabilizer branch is replayed with its I/Z T choices;
3. the weighted branch sum is reconstructed only for validation;
4. fidelity is compared against the direct Dense Core result;
5. every Compact Core branch tableau is checked against its corresponding
   replayed dense stabilizer branch.

The test passes only if:

- branch count is exact;
- decomposition fidelity is approximately 1;
- reconstructed norm is approximately 1;
- every branch tableau validates.

---

# GitHub Actions — Linux / Android workflow

Workflow:

```
Build Mundo X Lab APK
run #65
status: success
```

The native test stage ran before the Android build.

## 1 T gate

```
qubits=3
t_gates=1
branches=2
expected_branches=2
tableau_bytes=102
approx_explicit_bytes=368
dense_reference_bytes=64
fidelity=1.000000000
decomposition_norm=0.999999881
branch_tableaus=PASS
branches_exact=PASS
result=PASS
wall_time_ms=0.026
```

## 4 T gates

```
qubits=4
t_gates=4
branches=16
expected_branches=16
tableau_bytes=1088
approx_explicit_bytes=2994
dense_reference_bytes=128
fidelity=1.000000000
decomposition_norm=0.999999797
branch_tableaus=PASS
branches_exact=PASS
result=PASS
wall_time_ms=0.066
```

## 8 T gates

```
qubits=4
t_gates=8
branches=256
expected_branches=256
tableau_bytes=17408
approx_explicit_bytes=46278
dense_reference_bytes=128
fidelity=1.000000000
decomposition_norm=0.999999797
branch_tableaus=PASS
branches_exact=PASS
result=PASS
wall_time_ms=0.807
```

---

# Windows x64 cross-check

Workflow:

```
Build Mundo X Windows 10 x64
run #9
status: success
```

The Windows build independently passed:

```
4 qubits / 4 T
branches=16
fidelity=1.000000000
branch_tableaus=PASS
result=PASS
```

and:

```
4 qubits / 8 T
branches=256
fidelity=1.000000000
branch_tableaus=PASS
result=PASS
```

This confirms that the experiment is not dependent on one compiler/platform.

---

# What has been demonstrated

The experiment demonstrates an **exact non-Clifford T-gate representation** as a
linear combination of Compact Core stabilizer branches for the tested small
circuits.

This is a real extension beyond representing only one stabilizer state.

The total represented state can now be non-stabilizer even though each component
branch remains a stabilizer tableau.

---

# What has NOT been demonstrated

This result does **not** yet solve the non-Clifford scaling problem.

The baseline branch count is:

```
1 T -> 2 branches
4 T -> 16 branches
8 T -> 256 branches
```

so the current exact method has exponential growth in the number of T gates.

For the tiny 3–4 qubit validation cases, the branch representation is actually
larger than the dense reference. That is expected: these tests are for
correctness, not for claiming efficiency.

The next scientific target is therefore not merely "support T". That milestone
now exists experimentally.

The next target is:

> reduce / factor / merge / otherwise represent the stabilizer branch expansion
> without losing exactness, and measure whether the cost can be kept below the
> naive `2^T` growth for useful circuit families.

Any future reduction must count all supporting metadata and must not hide the
same exponential cost in another cache or table.

---

# Current interpretation

Before this experiment:

```
one Compact Core tableau
-> Clifford/stabilizer state
```

After this experiment:

```
weighted sum of Compact Core tableaux
-> exact tested non-Clifford state
```

This is an important correctness baseline, but not yet a compact universal
quantum simulator.

