# Bell: both marginals are 50/50; measuring q0 fixes q1 to the same result.
# seed controls a reproducible pseudorandom sampler, not physical randomness.
qubits 2
seed 2026
h 0
cx 0 1
assert ghz
probz 0
probz 1
measure 0
probz 1
measure 1
show
bytes
