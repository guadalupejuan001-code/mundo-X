# One physical state: global Ether rules and finite local changes.
background zero
h_all
cz_neighbors
assert expect 1 X:0 Z:1
assert expect 1 Z:0 X:1 Z:2
z 0
assert expect -1 X:0 Z:1
assert expect 1 Z:0 X:1 Z:2
# A global layer now acts on that already modified state.
cz_neighbors
assert expect -1 X:0
# Physical measurement and finite corrections remain in the same session.
postselect 0 1
assert expect -1 Z:0
stats
