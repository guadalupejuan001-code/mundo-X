# Mundo X

**Mundo X** es un prototipo experimental de un nuevo sustrato computacional.

## Orden del proyecto

1. **Quantum Core X** — montar primero un espacio de estados cuánticos válido.
2. **Transformer X** — investigar después cómo aprender/transformar dentro o alrededor de ese espacio.
3. **Interfaz X** — traducir información externa cuando el núcleo ya tenga comportamiento verificable.
4. Memoria, pliegue y modo operativo — solo después de medir las etapas anteriores.

## Quantum Core X v0.1

El núcleo actual representa estados:

```
|psi> = alpha|0> + beta|1>
```

y para varios qubits:

```
|psi> = sum(alpha_k |k>)
```

con norma total igual a 1.

La primera prueba incluye:

- superposición;
- fase;
- interferencia;
- CNOT;
- entrelazamiento de Bell;
- medición.

Ejecuta:

```bash
python experiments/quantum_interference_v0_1.py
```

## Estado científico

Quantum Core X es actualmente una **simulación clásica de matemáticas
cuánticas**. Todavía no existe hardware cuántico propio ni se ha demostrado
ventaja computacional.

Transformer X permanece en el repositorio como experimento separado, pero
no define el núcleo cuántico.
