# Rule: Bell pair (2k, 2k+1) for every nonnegative k.
background bell
assert expect 1 X:1000000000000000000 X:1000000000000000001
assert expect -1 Y:1000000000000000000 Y:1000000000000000001
assert expect 1 Z:1000000000000000000 Z:1000000000000000001
assert probz 1000000000000000000 0.5
# Reading the rule leaves zero active qubits.
stats
z 1000000000000000000
assert expect -1 X:1000000000000000000 X:1000000000000000001
stats
z 1000000000000000000
compact
stats
# Measuring one member preserves the exact conditional state of its partner.
postselect 1000000000000000000 1
assert probz 1000000000000000001 1
assert expect 1 Z:1000000000000000000 Z:1000000000000000001
stats
