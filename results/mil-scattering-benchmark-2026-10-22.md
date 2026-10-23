# MIL Scattering: FFT plan and separable-state optimization benchmark

Benchmark obtained October 22, 2026 local ET / October 23 UTC, in a Debian 13 Linux x86_64 environment, g++ 14.2.0 -O3 -std=c++17. Results are **exploratory local CPU numbers**, not Android measurements, and do not compare against a physics-equivalent commercial simulator.

## Method

- Fixed physics: two distinguishable quantum particles on a periodic 1D lattice; J=1, time-step=0.1, 30 steps for each case.
- Interacting branch: U=5. Free product-control branch: U=0.
- Five runs per implementation and problem size. Table entries are wall-clock **medians** in milliseconds. No affinity/thermal control and no statistical confidence interval.
- Old implementation: 2D FFT without preplanned bit reversal and twiddles, and direct N*N Gaussian/phase initial-state calculation (the first PR #49 solver).
- Optimized implementation: preplanned FFT roots and bit reversal, fused inverse normalization, O(N) Gaussian/phase initialization; when U=0 and state is known separable, two independent 1D FFTs and final N*N output reconstruction.

| N sites / particle | Old U=5 | Optimized U=5 | Speedup U=5 | Old U=0 | Optimized U=0 | Speedup U=0 |
|---:|---:|---:|---:|---:|---:|---:|
| 32 | 1.5922 ms | 0.8357 ms | 1.91x | 1.4272 ms | 0.0638 ms | 22.4x |
| 64 | 6.4585 ms | 3.9607 ms | 1.63x | 6.2406 ms | 0.0865 ms | 72.1x |
| 128 | 29.2769 ms | 18.7265 ms | 1.56x | 32.3447 ms | 0.2318 ms | 139.5x |
| 256 | 152.3902 ms | 112.0901 ms | 1.36x | 147.1510 ms | 0.7223 ms | 203.7x |
| 512 | 710.9063 ms | 484.5938 ms | 1.47x | 667.5646 ms | 2.4344 ms | 274.2x |

The very large U=0 gain comes from eliminating **unnecessary 2D evolution** of a state that is mathematically known to factorize. It does not generalize to U!=0 entangled collisions or to QCD. The U!=0 improvement is from reusing FFT plans and eliminating repeated initialization work; it retains O(steps*N^2 log N) complexity.

## Numerical regression

- Independent C++ simulation of two-particle dynamics with a **sparse-Hamiltonian RK4** reference at N=8 confirms temporal convergence of the symmetric split-step method, as documented by PR #49 tests.
- Normalization and forward/reverse time evolution tests pass in local experiments for N=32 and N=128.
- For N=32, U=0, 80 steps, the optimized factorized result agrees with the full 2D FFT result with maximum complex amplitude error **4.35e-16** on the local x86 reference. This checks **amplitude and phase**, not only final probabilities.
- Free propagation of a deliberately entangled initial state continues through the unrestricted two-body path: the shortcut is allowed only when the initial state is a known product.
- Local N=1024, U=0, 100 steps, produced norm approximately 1 with N*N=1,048,576 complex-double amplitudes in the final state, 16 MiB state payload. It is not 1024 physical qubits, nor a Hadron/QCD collision.

## Memory and budgets

- 2-body output state allocation is **16*N^2 bytes**; optimization does not change this public result contract.
- Planned 2D FFT scratch is approximately 48*N bytes (twiddles/bit reversal/kinetic/column); free-product path plus two factors is approximately 80*N bytes, excluding allocator overhead.
- Default work limit 300 million estimated work units remains enforced. Estimates distinguish O(steps*N^2 logN) interacting evolution from O(steps*N logN + N^2) free evolution. The latter still materializes a full N^2 final state.
- No Android GPU/FPS claim, physical quantum advantage, or performance advantage against PYTHIA/Geant4 is established.

## Reproduce on your machine

~~~sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --target mil_scattering_benchmark test_mil_scattering mundo_x --parallel 2
./build/mil_scattering_benchmark
ctest --test-dir build --output-on-failure -R 'mil_scattering|xasm_mil_scattering|mx_mil_scattering'
~~~

The in-tree benchmark source is for the current interacting solver. The medians comparing earlier vs optimized and U=0 used separate local baseline/optimized binaries compiled with the same flags; rerunning the current target alone does **not** reproduce those A/B numbers without checking out the earlier PR #49 commit as a baseline. Report hardware, compiler, load, actual errors and a matched physical workload when repeating the comparison.
