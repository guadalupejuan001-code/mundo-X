# XASM v0.2 — circuitos Clifford+T programables

XASM ejecuta archivos `.x` con el runtime nativo de Mundo X. Esta versión conecta el intérprete con `ReducedExactTRepresentation` para ejecutar circuitos propios con puertas T, además del modo Clifford de Compact Core.

## Ejecución

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/mundo_x run examples/magic_t.x
./build/mundo_x run examples/clifford_t.x
```

En Windows, el ejecutable de una compilación Release se encuentra en `build\Release\mundo_x.exe`; utiliza `ctest --test-dir build -C Release --output-on-failure`.

## Selección del motor

Sin directiva `backend`, el intérprete sigue usando Compact Core. Los programas existentes `hello.x`, `bell.x` y `ghz4.x` conservan su comportamiento. También puede declararse `backend compact` explícitamente.

Para usar T, declara `backend reduced` antes de `qubits`:

```text
backend reduced
qubits 1
h 0
t 0
show
bytes
assert fidelity
assert norm
```

Este circuito prepara el estado `( |0> + exp(iπ/4)|1> ) / √2`. La evolución reducida mantiene una base estabilizadora compartida, etiquetas Pauli y coeficientes; no materializa las ramas ingenuas `2^T`. El número de componentes todavía puede crecer como `2^n`.

El modo reducido del intérprete admite **1..1.000 qubits, 128 puertas T y hasta 65.536 componentes**. X/Z se representan con máscaras Pauli multi-palabra, por lo que q999 participa en T, fases, Clifford y observables sin truncarse a una palabra. La directiva `backend` se declara como máximo una vez, antes de `qubits`. El ancho de 1.000 no garantiza costo compacto para cualquier circuito no-Clifford: el presupuesto de componentes sigue siendo la frontera práctica.

## Instrucciones

Los índices de qubit empiezan en cero. Cada instrucción ocupa una línea; `#` inicia un comentario. Los argumentos negativos, índices fuera de rango, argumentos faltantes y tokens sobrantes producen un error con el número de línea.

| Instrucción | Compact | Reduced | Función |
|---|---|---|---|
| `qubits n` | Sí | Sí, 1..1.000 | Inicializa el estado en cero; una declaración por programa |
| `h q`, `x q`, `z q`, `s q` | Sí | Sí | Puertas de un qubit |
| `cx c t`, `cz a b` | Sí | Sí | Puertas de dos qubits distintos |
| `t q` | No | Sí | Puerta de fase T; hasta 128 por programa |
| `show` | Generadores estabilizadores | Resumen de representación | Inspecciona el estado sin reconstrucción densa |
| `bytes` | `STATE_BYTES` | `LOGICAL_PAYLOAD_BYTES` | Informa la memoria del tableau o la carga lógica estimada |
| `assert one`, `assert ghz` | Sí | No | Comprobaciones estabilizadoras existentes |
| `expect PAULI` | Sí | Sí | Expectativa de una cadena I/X/Y/Z con signo opcional; sin vector denso |
| `assert expectation PAULI VALOR` | Sí | Sí | Comprueba una expectativa con tolerancia absoluta `1e-10` |
| `assert norm` | No | Sí | Comprueba la norma mediante componentes; sin vector denso |
| `assert fidelity` | No | Solo hasta 12 qubits | Compara contra la referencia densa pequeña |

`show` en modo reducido no muestra el tableau base como si representara el estado completo después de T. Informa el motor, los componentes, las puertas T y la carga lógica. La carga lógica incluye el tableau, etiquetas, coeficientes y registros de puertas; excluye la sobrecarga de contenedores, asignaciones y proceso.

## Validación y códigos de salida

`assert fidelity` compara la reconstrucción de la representación reducida con una ejecución del circuito en Dense Core. `assert norm` comprueba la norma con `sparse_norm()`, sin reconstrucción densa. Ambas comprobaciones requieren un resultado finito y una diferencia respecto a 1 de hasta `3e-5`, de acuerdo con la tolerancia del experimento existente. La reconstrucción de referencia usa amplitudes `complex<float>`, por lo que no se exige igualdad numérica exacta.

`assert fidelity` construye vectores densos y rechaza registros mayores de 12 qubits antes de asignarlos. Las puertas, `expect`, `assert expectation`, `assert norm` y las consultas `show`/`bytes` no activan esa reconstrucción. Las expectativas del backend reducido se normalizan por su norma. Las comprobaciones se ejecutan sobre el estado en el punto donde aparece cada `assert`.

El comando `run` devuelve `0` si todas las comprobaciones pasan y `1` si una falla, el archivo no existe o el programa contiene un error. Una comprobación fallida conserva `FAIL` en la salida aunque una comprobación posterior pase.

La suite CTest cubre compatibilidad de los ejemplos, circuitos con fases y entrelazamiento, comparación con la referencia densa, errores de sintaxis, límites del modo reducido y códigos de salida del ejecutable.
