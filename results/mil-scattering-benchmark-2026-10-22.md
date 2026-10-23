# MIL scattering scalability: local x86 reference (2026-10-22 ET / 2026-10-23 UTC)

Prototype measured by running the same two-body FFT propagator as mil_scattering.hpp with G++ 14.2.0, C++17, -O3, on a Debian 13 x86_64 container reporting AMD EPYC 9V74; single runs, no CPU pinning or thermal isolation. All cases use 30 steps, hopping J=1, contact U=5, dt=0.1. Measurements are **exploratory**, not stable benchmark medians, and **not measurements on the user's Samsung A16**.

| Sites per particle | Time (single sample) | Wavefunction state bytes | Final norm |
|---:|---:|---:|---:|
| 32 | 1.413 ms | 16,384 | 0.999999999999969 |
| 64 | 7.328 ms | 65,536 | 1.000000000000049 |
| 128 | 37.214 ms | 262,144 | 1.000000000000095 |
| 256 | 173.717 ms | 1,048,576 | 1.000000000000294 |
| 512 | 741.915 ms | 4,194,304 | 0.999999999999630 |

The file native/tests/mil_scattering_benchmark.cpp provides a repeatable command-line experiment:

~~~sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target mil_scattering_benchmark --parallel 2
./build/mil_scattering_benchmark
~~~

These timings show finite O(N² log N) behavior for a two-particle model. They are **not proof of speedup over PYTHIA/Geant4**, nor of constant-memory, unlimited qubits, precision for real hadrons, or any Android GPU/FPS acceleration. To claim superiority, measure against an appropriate, independently implemented and physics-matched baseline at the same error tolerance and resource budget.
