# MIL Scattering: coherent two-particle collisions (v0.1)

## Implemented model

The independent C++ module native/include/mundo_x/mil_scattering.hpp models **two distinguishable nonrelativistic quantum particles** on a one-dimensional periodic lattice (a ring), using a two-particle wavefunction psi(x1,x2,t). The particles begin as oppositely moving localized Gaussian wave packets. They interact via an on-site contact coupling.

H = -J(T1 + T1^dagger + T2 + T2^dagger) + U sum_x |x,x><x,x|, with hbar=1.

Finite-time evolution uses **symmetric Strang splitting** between position-diagonal interaction and separable momentum-diagonal kinetic operators. Every step performs a forward and inverse in-place 2D FFT (as row and column 1D FFTs). This preserves coherent complex amplitudes, relative phases and the state norm to floating-point accuracy. It does not measure or prune branches in the middle of the collision.

Strang splitting is unitary but its *time integration accuracy* is second order. Lattice spacing and periodic boundaries are explicit assumptions. The particles are distinguishable: neither bosonic nor fermionic exchange symmetry is imposed. This is a controlled interaction model, not a simulation of quarks, QCD, hadronization, a hadron collider, nuclear fusion or particle creation.

## MX -> XASM -> C++

The new physics command uses integer fixed-point couplings to preserve the existing MX count expression type:

~~~mx
let sites = 32;
let steps = 80;
scatter(sites, steps, 1000, 5000);
scatter(sites, steps, 1000, 0);
~~~

Run:

~~~sh
./build/mundo_x compile examples/mil_scatter.mx
./build/mundo_x run-mx examples/mil_scatter.mx
./build/mundo_x run examples/mil_scatter.x
~~~

The MX command scatter(sites, steps, J_milli, U_milli) compiles into the XASM line scatter sites steps J_milli U_milli. J_milli=1000 means hopping J=1.0; U_milli=5000 means contact repulsion U=5.0; dt defaults to 0.1 in dimensionless units. The standalone instruction requires no fictitious qubits declaration. Negative U is supported by the native solver, but **not yet exposed by the integer-only MX/XASM v0.1 command**.

Deterministic results provide: norm, max_norm_error, initial_contact, peak_contact, final_contact, exchanged_order, state_bytes, scratch_bytes and estimated_work. Exchanged_order is a ring-coordinate position-order probability P(x1>x2), **not** an experimentally measured scattering cross section.

## Scaling and explicit budgets

With N lattice sites **per particle**, the two-particle wavefunction has N^2 complex-double amplitudes. Propagation costs O(steps*N^2*log N). State payload is 16*N^2 bytes, plus O(N) FFT scratch. The result retains the full state for analysis, and each propagator call reuses one scratch workspace. There is no claim of constant-resource scaling to arbitrarily many particles.

- N: powers of 2 only, between 8 and 1024.
- Steps: 1..2048, under a **300-million-unit cumulative XASM work budget**.
- J: 0 < J <= 100; native |U| <= 100. MX/XASM permits 0 <= U <= 100.
- dt: 0 < dt <= 0.25 (the language interface currently fixes 0.1).
- Work-budget errors are reported; amplitudes are not silently dropped.

| Sites per particle | Complex amplitudes | State payload, not process RAM |
|--:|--:|--:|
| 32 | 1,024 | 16 KiB |
| 128 | 16,384 | 256 KiB |
| 256 | 65,536 | 1 MiB |
| 512 | 262,144 | 4 MiB |
| 1024 | 1,048,576 | 16 MiB |

For example, N=256 and 128 time steps require approximately 159 million estimated units and a 1 MiB state payload. N=1024 with many steps will usually exceed the default work budget, so **the maximum addressable lattice is not the maximum practical workload**. No FPS or Android battery/RAM guarantee is made.

## Boundaries of existing Mil Core

This is a **separate specialized quantum scattering backend driven by MX/XASM**. It does not call ReducedExactTRepresentation to execute 1000 general qubits, does not use physical quantum hardware, and is not a benchmark victory against PYTHIA, Geant4, tensor networks, or specialist lattice solvers.

The MIL radial Hamiltonian simulation with 8 logical qubits and 256 sites remains untouched. A future connector may encode a directly comparable interaction Hamiltonian in ReducedExactTRepresentation on small systems; any such result must be independently checked before being called a MIL circuit simulation.

## Tests

~~~sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --target mundo_x test_mil_scattering mundo_x_mxlang_tests --parallel 2
ctest --test-dir build --output-on-failure -R 'mil_scattering|xasm_mil_scattering|mx_mil_scattering|mxlang_compiler'
~~~

The native regression checks norm preservation, forward/backward reversibility, wavepacket contact, nonzero coupling changing probabilities, deterministic replay, payload accounting and resource rejection. Norm conservation alone does not establish predictive validity for physical hadrons.

Scientific next steps: an **independent** exact reference for small lattices, time-step convergence, quantitative entanglement observables, open-boundary wavepacket scattering, and a real MIL Reduced encoding. Particle-production dynamics would additionally require a variable-particle-number quantum field model.
