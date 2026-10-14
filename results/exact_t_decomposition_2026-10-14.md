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


---

# Android physical device confirmation

The exact T experiment was executed from the Mundo X Android APK on a physical
Android device.

Observed configuration:

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
```

Two consecutive executions were captured:

```
run A:
wall_time_ms=8.863
Android process PSS ~= 93275 kB

run B:
wall_time_ms=3.918
Android process PSS ~= 86685 kB
```

Interpretation:

- the exact 8-T decomposition also reproduces correctly on physical Android
  hardware;
- timing varies between runs, so these two measurements are not treated as a
  controlled performance benchmark;
- fidelity and branch validation are the primary correctness result;
- process PSS is total Android process memory and is not the branch-state size;
- the explicit branch representation for this tiny 4-qubit case is larger than
  the dense reference, so this result demonstrates correctness, not memory
  advantage.


---

# Branch redundancy probe

The naive exact representation creates `2^T` stabilizer branches. A diagnostic
was added to determine how many of those branches are actually distinct.

Two independent counts are reported:

- `unique_tableaus`: distinct Compact Core generator tableaus;
- `oracle_unique_physical_states`: distinct stabilizer states up to global
  phase, checked with Dense Core only as a small-circuit oracle.

The oracle also combines equivalent states with their correct relative phase and
reconstructs the total state again. This grouped reconstruction must retain
fidelity 1.

## Four-qubit circuit

### 4 T

```
branches=16
unique_tableaus=16
oracle_unique_physical_states=16
oracle_mergeable_branches=0
oracle_reduction_factor=1
oracle_grouped_fidelity=1.000000000
```

### 8 T

```
branches=256
unique_tableaus=16
oracle_unique_physical_states=16
oracle_mergeable_branches=240
oracle_reduction_factor=16
oracle_grouped_fidelity=1.000000000
```

### 10 T

```
branches=1024
unique_tableaus=16
oracle_unique_physical_states=16
oracle_mergeable_branches=1008
oracle_reduction_factor=64
oracle_grouped_fidelity=1.000000000
```

### 12 T

```
branches=4096
unique_tableaus=16
oracle_unique_physical_states=16
oracle_mergeable_branches=4080
oracle_reduction_factor=256
oracle_grouped_fidelity=1.000000000
```

For four qubits, the observed distinct-state count saturated at:

```
16 = 2^4
```

while the naive branch expansion continued to `4096 = 2^12`.

## Six-qubit cross-check

A second circuit was tested to check whether the four-qubit result was merely a
small-circuit coincidence.

```
qubits=6
t_gates=12
branches=4096
unique_tableaus=64
oracle_unique_physical_states=64
oracle_mergeable_branches=4032
oracle_reduction_factor=64
oracle_grouped_fidelity=1.000000000
result=PASS
wall_time_ms=150.465
```

The distinct-state count saturated at:

```
64 = 2^6
```

This matches the expected size of the Pauli orbit of a pure n-qubit stabilizer
state up to stabilizer equivalence.

## Interpretation

For the tested Clifford+T construction:

```
naive expansion:       2^T branches
observed distinct:     <= 2^n stabilizer states
```

This is not yet an implementation that stores only `2^n` states. The current
runtime still materializes all naive branches and the dense oracle is used to
identify exact phase-correct equivalences after the fact.

Therefore this result demonstrates **measurable exact redundancy**, not yet a
new compact universal simulator.

The next implementation target is to exploit this structure during evolution,
rather than generate `2^T` branches and merge them afterward.

A likely exact route is to share the common stabilizer structure and track only
the Pauli differences / state labels plus complex coefficients, while preserving
all relative phases. That implementation must be validated without using a dense
statevector as part of its runtime representation.


---

# Reduced representation during evolution

A second implementation now exploits the measured redundancy **while the circuit
is evolving**.

Instead of storing one Compact Core tableau for every naive T branch, it stores:

```
one shared Compact Core stabilizer tableau
+ canonical Pauli-difference labels
+ complex coefficients
```

Equivalent Pauli labels are canonicalized modulo the current stabilizer group and
their coefficients are combined immediately.

Dense Core is not used by the evolution itself. It is invoked only after the
experiment to validate the final state for small qubit counts.

## Linux / GitHub Actions run #77

### 4 qubits / 12 T

Naive exact representation:

```
branches=4096
approx_explicit_bytes=737562
wall_time_ms=27.868
```

Reduced representation:

```
naive_branches=4096
reduced_components=16
component_upper_bound=16
base_tableau_bytes=68
logical_payload_bytes=1072
dense_reference_bytes=128
fidelity=1.000000000
reduced_norm=0.999999797
result=PASS
wall_time_ms=0.106
```

Compared with the project's own naive branch implementation:

```
logical payload reduction ~= 688x
end-to-end experiment time reduction ~= 263x
```

The timing comparison includes each implementation's validation overhead and
must not be interpreted as quantum hardware speedup.

### 6 qubits / 12 T

Naive exact representation:

```
branches=4096
approx_explicit_bytes=876844
wall_time_ms=108.897
```

Reduced representation:

```
naive_branches=4096
reduced_components=64
component_upper_bound=64
base_tableau_bytes=102
logical_payload_bytes=2678
dense_reference_bytes=512
fidelity=1.000000000
reduced_norm=0.999999797
result=PASS
wall_time_ms=0.247
```

Compared with the project's naive branch implementation:

```
logical payload reduction ~= 327x
end-to-end experiment time reduction ~= 441x
```

### 8 qubits / 24 T

The naive expansion would contain:

```
16,777,216 branches
```

The reduced implementation never materialized them:

```
reduced_components=256
component_upper_bound=256
base_tableau_bytes=136
logical_payload_bytes=9324
dense_reference_bytes=2048
fidelity=1.000000000
result=PASS
wall_time_ms=1.753
```

### 8 qubits / 64 T

The naive expansion would contain:

```
2^64 branches
```

Those branches were **not materialized**.

Observed:

```
reduced_components=256
component_upper_bound=256
base_tableau_bytes=136
logical_payload_bytes=10764
dense_reference_bytes=2048
fidelity=1.000000000
result=PASS
wall_time_ms=4.736
```

## Windows x64 cross-check

The same reduced representation passed independently on Windows.

Examples:

```
4q / 12T:
reduced_components=16
fidelity=1.000000000
result=PASS
wall_time_ms=0.292
```

```
8q / 64T:
naive_branches=2^64 (not materialized)
reduced_components=256
fidelity=1.000000000
result=PASS
wall_time_ms=8.531
```

## Scientific interpretation

This is materially different from the earlier diagnostic.

Earlier:

```
generate 2^T branches
-> detect duplicates afterward
```

Now:

```
shared stabilizer state
-> T creates Pauli alternatives
-> canonicalize modulo stabilizer
-> merge coefficients immediately
-> never create duplicate full tableaus
```

For the tested circuit family, the number of retained components saturated at
`2^n` rather than `2^T`.

This removes the branch-count dependence on T count for these experiments.

It does **not** remove the general exponential dependence on qubit count. In the
saturated tests, the reduced representation is still larger than a dense
statevector for the same small number of qubits because every retained component
stores a Pauli label plus a complex coefficient and a shared stabilizer tableau.

Therefore the demonstrated result is:

> exact elimination of the naive `2^T` branch explosion for the tested
> Clifford+T construction, while preserving fidelity 1 against Dense Core.

It is not yet a demonstration of asymptotic superiority over dense statevectors
in the worst case.


---

# Internal coefficient factorization probe

After eliminating the naive `2^T` branch explosion, the next question was
whether the remaining coefficient space could also be represented with a simple
low-rank sequential factorization.

The reduced coefficient map was converted into coordinates of the stabilizer
quotient space. For every bipartition of those coordinates, the numerical
complex matrix rank was measured.

This diagnostic does not alter the simulation. It asks whether a straightforward
tensor-chain style factorization would use fewer complex scalars than the flat
coefficient vector.

## Results

### 4 qubits / 12 T

```
active_coefficients=16
full_coefficient_space=16
coefficient_cut_ranks=2,4,2
max_cut_rank=4
factorized_complex_scalars=40
dense_coefficient_scalars=16
```

### 6 qubits / 12 T

```
active_coefficients=62
full_coefficient_space=64
coefficient_cut_ranks=2,4,8,4,2
max_cut_rank=8
factorized_complex_scalars=168
dense_coefficient_scalars=64
```

### 8 qubits / 24 T

```
active_coefficients=256
full_coefficient_space=256
coefficient_cut_ranks=2,4,8,16,8,4,2
max_cut_rank=16
factorized_complex_scalars=680
dense_coefficient_scalars=256
```

### 8 qubits / 64 T

```
active_coefficients=256
full_coefficient_space=256
coefficient_cut_ranks=2,4,8,16,8,4,2
max_cut_rank=16
factorized_complex_scalars=680
dense_coefficient_scalars=256
fidelity=1.000000000
result=PASS
```

## Interpretation

For this circuit family and this quotient-coordinate ordering, the coefficient
tensor reaches the maximum possible bipartition ranks.

Therefore a straightforward sequential low-rank factorization is **not a
compression** here:

```
8q flat coefficient vector:       256 complex scalars
8q factorized estimate:           680 complex scalars
```

This is a negative result and is intentionally retained.

It means the next reduction cannot simply assume low tensor rank in the current
basis. Any further exact compression must exploit a different structure, basis,
symbolic relation, decision structure, or query-specific computation.

No claim is made that all possible basis choices have been exhausted.


---

# Gate-by-gate growth trace

A gate-by-gate trace was added to determine exactly when the reduced
representation loses compacity.

Test:

```
8 qubits
12 T gates
same reduced Clifford+T circuit family
GitHub Actions run #84
```

The trace records after every gate:

- retained component count;
- active coefficient count;
- maximum coefficient cut rank;
- logical payload bytes.

## Main observation

During the initial Clifford preparation:

```
components=1
active=1
max_cut_rank=1
```

The first eight T gates then populate the coefficient space one qubit at a time:

```
T0 -> 2 components
T1 -> 4
T2 -> 8
T3 -> 16
T4 -> 32
T5 -> 64
T6 -> 128
T7 -> 256
```

At that point:

```
components=256 = 2^8
active=256
```

After saturation, additional T gates no longer increase component count:

```
T0 -> 256
T1 -> 256
T2 -> 256
T3 -> 256
```

The final state still validates:

```
fidelity=1.000000000
reduced_norm=0.999999797
result=PASS
```

## Important distinction discovered

Two different kinds of growth are now visible.

### 1. Support growth

The T gates are what populate new Pauli-equivalence components:

```
1 -> 2 -> 4 -> 8 -> ... -> 256
```

Once the full 8-qubit quotient support is occupied, more T gates do not create
additional components.

### 2. Internal correlation / rank growth

The component count can remain fixed at 256 while the coefficient structure
continues becoming harder to factor.

Observed:

```
after T7:
components=256
max_cut_rank=2

after H7:
components=256
max_cut_rank=8

later after H2:
components=256
max_cut_rank=16
```

CNOT and S gates in the traced locations preserved the component count and did
not cause these rank jumps.

Therefore, for this circuit, the first loss of sparsity is driven by T gates,
while later loss of simple factorability is driven by Hadamard mixing after the
support has already saturated.

This separates two problems that were previously mixed together:

```
T gates        -> fill the support
Hadamard gates -> mix/correlate the occupied coefficients
```

This result does not prove that every Clifford+T circuit behaves this way, but it
identifies a concrete mechanism to investigate next.

