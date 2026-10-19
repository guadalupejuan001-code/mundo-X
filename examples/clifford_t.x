# Circuito entrelazado con puertas Clifford y T intercaladas.
backend reduced
qubits 3
h 0
cx 0 1
cx 1 2
t 0
h 1
t 1
cz 1 2
s 2
x 0
t 2
cx 2 0
z 1
h 2
show
bytes
assert fidelity
assert norm
