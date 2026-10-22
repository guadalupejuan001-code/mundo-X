# Ether Render X directly observing the real MIL / Reduced 1000-qubit backend.
# One non-Clifford gate changes the material; no measurement or 2^1000 buffer.
backend reduced
qubits 1000
h 999
render 96 54 16
render_camera 0 0 10 0 0 0 47
render_body 0 0 0 0 1.8 0.35 0.55 0.75
render_bind 0 X 999 0.75
render_quantum 0
render_frame
render_info
t 999
render_quantum 0
render_frame
render_assert_exact
render_info
tdg 999
render_quantum 0
render_frame
render_assert_exact
render_info
assert norm
