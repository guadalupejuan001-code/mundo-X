# Inverse Ether Memory: reglas -> diferencias -> proyección clásica

mem_stats
mem_read 1000000000000000000000000000000

# Una diferencia clásica no materializa qubits.
mem_store 1000000000000000000000000000000 37
mem_read 1000000000000000000000000000000

# Un enlace cuántico es explícito; no existe mapeo global byte->8 qubits.
mem_bind 7 100
x 100
x 102
mem_read 7

# Convertir q100 en superposición vuelve cuántica esa vista de byte.
h 100
mem_read 7
mem_stats
