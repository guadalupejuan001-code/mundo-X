# One Mundo X program computes and edits a persistent visual scene.
# These three objects record probability snapshots at different instructions.
scene 1024 2026
qubits 1

scene_probz 496 0  # p(1)=0   -> state 0, original rule
h 0
scene_probz 497 0  # p(1)=0.5 -> state 3, raised object
z 0
h 0
scene_probz 498 0  # p(1)=1   -> state 6, higher object
scene_edit 499 7  # independently hide one object
assert one
scene_info
