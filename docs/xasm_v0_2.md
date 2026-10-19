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

El modo reducido del intérprete está limitado a **1..12 qubits y 128 puertas T**, igual que el experimento reducido existente. La API C++ tiene otros límites; este intérprete mantiene un alcance experimental acotado. La directiva `backend` se declara como máximo una vez, antes de `qubits`.

## Instrucciones

Los índices de qubit empiezan en cero. Cada instrucción ocupa una línea; `#` inicia un comentario. Los argumentos negativos, índices fuera de rango, argumentos faltantes y tokens sobrantes producen un error con el número de línea.

| Instrucción | Compact | Reduced | Función |
|---|---|---|---|
| `qubits n` | Sí | Sí, 1..12 | Inicializa el estado en cero; una declaración por programa |
| `h q`, `x q`, `z q`, `s q` | Sí | Sí | Puertas de un qubit |
| `cx c t`, `cz a b` | Sí | Sí | Puertas de dos qubits distintos |
| `t q` | No | Sí | Puerta de fase T; hasta 128 por programa |
| `show` | Generadores estabilizadores | Resumen de representación | Inspecciona el estado sin reconstrucción densa |
| `bytes` | `STATE_BYTES` | `LOGICAL_PAYLOAD_BYTES` | Informa la memoria del tableau o la carga lógica estimada |
| `assert one`, `assert ghz` | Sí | No | Comprobaciones estabilizadoras existentes |
| `assert fidelity`, `assert norm` | No | Sí | Validación densa explícita del circuito reducido |

`show` en modo reducido no muestra el tableau base como si representara el estado completo después de T. Informa el motor, los componentes, las puertas T y la carga lógica. La carga lógica incluye el tableau, etiquetas, coeficientes y registros de puertas; excluye la sobrecarga de contenedores, asignaciones y proceso.

## Validación y códigos de salida

`assert fidelity` compara la reconstrucción de la representación reducida con una ejecución del circuito en Dense Core. `assert norm` comprueba la norma de la reconstrucción reducida. Ambas comprobaciones requieren un resultado finito y una diferencia respecto a 1 de hasta `3e-5`, de acuerdo con la tolerancia del experimento existente. La reconstrucción de referencia usa amplitudes `complex<float>`, por lo que no se exige igualdad numérica exacta.

Estas instrucciones construyen vectores densos para validar circuitos pequeños. Las puertas y las consultas `show`/`bytes` no activan esa reconstrucción. Las comprobaciones se ejecutan sobre el estado en el punto donde aparece cada `assert`.

El comando `run` devuelve `0` si todas las comprobaciones pasan y `1` si una falla, el archivo no existe o el programa contiene un error. Una comprobación fallida conserva `FAIL` en la salida aunque una comprobación posterior pase.

La suite CTest cubre compatibilidad de los ejemplos, circuitos con fases y entrelazamiento, comparación con la referencia densa, errores de sintaxis, límites del modo reducido y códigos de salida del ejecutable.
