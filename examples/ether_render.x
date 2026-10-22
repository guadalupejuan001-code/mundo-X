# Ether Render X v0.1
# The world declares what can affect each screen region before pixels trace it.
# No qubits are required; this is the new render domain inside XASM.

render 320 180 32
render_camera 0 0 10 0 0 0 47

# id  x    y    z    radius   r    g    b
render_body 0   0    0    0    1.40    0.20 0.60 1.00
render_body 1   3.4  0.7 -2    0.90    1.00 0.45 0.10
render_body 2  -4   -1   -8    1.80    0.55 0.80 0.30

render_plan
# Benchmark candidate tile sizes on the current machine, then keep the fastest exact one.
render_train
render_frame

# Concrete observation of the central pixel.
render_pixel 160 90

# The optimized region path must match the brute-force oracle exactly.
render_assert_exact
render_info
