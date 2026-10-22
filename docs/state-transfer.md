# Transferencia del estado producto al motor reducido

La primera ruta adaptativa almacenaba todas las puertas mientras el estado
era producto. Cuando aparecía entrelazamiento, ejecutaba ese historial otra
vez en `ReducedExactTRepresentation`. La memoria aumentaba con la profundidad
del circuito incluso cuando sus amplitudes seguían ocupando solo 32 bytes
por qubit.

Ahora `adaptive` transfiere las amplitudes ya calculadas al motor reducido y
aplica una sola vez la puerta que produce entrelazamiento. No guarda una
segunda copia de las puertas en la ruta producto. `adaptive-replay` mantiene
el comportamiento anterior, incluido reconstruir el estado desde las puertas
originales.

## Representación y precisión

`ReducedExactTRepresentation::from_product()` importa las dos amplitudes
complejas de cada qubit. Escoge entre las bases locales Z, X e Y, utilizando
la que tenga menos coeficientes exactamente distintos de cero. Los estados
producto estabilizadores se pueden transferir con una sola componente. Las
amplitudes generales se expanden en cosets de Pauli sobre la base elegida.
Se conservan la fase compleja y la norma, dentro del redondeo numérico.

La importación no descarta coeficientes por ser pequeños. Comprueba el
presupuesto antes de expandir el siguiente factor. Una transferencia que
no cabe en el presupuesto falla explícitamente y bloquea las lecturas.

La factorización previa de puertas de dos qubits sigue aceptando un residuo
relativo local de hasta `1e-14`. La transferencia conserva esa aproximación;
`product_residual_sum()` continúa mostrando los residuos aceptados. Esa suma
no es una cota rigurosa de todo el error de coma flotante. El modo de
reproducción vuelve a calcular desde las puertas originales y pone a cero
los residuos producto después de una transición exitosa.

Las dos políticas pueden dar representaciones diferentes y consumir distinto
número de componentes. La transferencia no garantiza una descomposición
mínima. El motor reducido sigue pudiendo crecer exponencialmente y mantiene
el presupuesto predeterminado de 65.536 componentes. El ancho estructural actual
es de 1.000 qubits mediante máscaras Pauli multi-palabra. La reconstrucción densa para validación permanece limitada a 23
qubits y conserva su aritmética heredada `complex64`.

La transición sigue siendo unidireccional: si posteriormente desaparece el
entrelazamiento, el estado permanece en el motor reducido. Esta revisión no
integra la ruta nueva en la interfaz Android.

## Resultados dentro de Mundo X

Referencia anterior: `40a9c8215fbe3e0bdc7a1242b3f9a982344d8d4e`.
Datos reproducibles en [state-transfer-results-2026-10-16.json](state-transfer-results-2026-10-16.json).

| Circuito | Carga lógica anterior | Carga lógica actual | Puertas repetidas por la ruta actual |
|---|---:|---:|---:|
| Sumador de 50 qubits, 1.000 puertas | 33.600 bytes | 1.600 bytes | 0 |
| Sumador con superposición de 12 qubits | 7.846 bytes | 7.150 bytes | 0 |
| GHZ de 50 qubits | 1.482 bytes | 1.476 bytes | 0 |
| 10.000 fases seguidas de entrelazamiento y su inversa, 50 qubits | 140.950 bytes | 938 bytes | 0 |

Estas cifras cuentan amplitudes, cosets, tableau y registros internos según
la ruta. Excluyen capacidad de contenedores, asignador, el circuito de entrada
que conserva el ejecutable y el resto del proceso. No son medidas de RAM total.

El caso de transición tardía comienza con H en el qubit 0, aplica 10.000 fases
de 0,001 radianes y dos CNOT entre 0 y 49. La segunda CNOT permite comprobar
la fase acumulada en X/Y locales. Su mediana de inicialización, evolución y
lectura pasó de 326,998 ms a 6,667 ms en este diagnóstico. El JSON incluye
las otras mediciones y sus repeticiones; son resultados de esta sesión,
no una garantía de rendimiento para cualquier carga.

## Verificación

- 131 circuitos independientes por `adaptive`, `adaptive-replay` y `reduced`:
  393 ejecuciones, todas aprobadas. La referencia numérica fue Qulacs FP64;
  no se realizaron nuevas comparaciones de rendimiento con otros simuladores.
- Error máximo en observables: `3,56e-15`. Error máximo de norma: `5,33e-15`.
- La reconstrucción densa heredada produjo un error de amplitud absoluta de
  `1,20e-7`; normalizando esa reconstrucción, `8,47e-16`.
- Otras 16 entradas de 50 qubits verificadas con aritmética: error máximo
  en observables menor que `1e-14`, carga producto constante de 1.600 bytes.
- Importación de los seis autoestados de Pauli con fase/norma compleja,
  70 productos aleatorios de 1 a 7 qubits, índices hasta el qubit 62,
  presupuestos, amplitudes inválidas y conservación del residuo aceptado.
- Cinco pruebas CTest: API de fases, transferencia, hello, teleport y magic.

## Reproducir

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 2
ctest --test-dir build --output-on-failure
build/mundo_x_phase examples/adder50.qx adaptive
build/mundo_x_phase examples/adder50.qx adaptive-replay
OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 QULACS_NUM_THREADS=1 \
python tests/phase_validation.py --runner build/mundo_x_phase --output validation.json
python tests/state_transfer_check.py --runner build/mundo_x_phase \
  --previous-runner /path/to/40a9c82/mundo_x_phase --output transfer-check.json
```

En C++, el cuarto argumento del constructor selecciona la política:
`AdaptivePhaseCore(n, 1e-14, 65536, AdaptivePhaseCore::Transition::Replay)`.
La política predeterminada es `Transition::Transfer`. El ejecutable informa
`history_gates`, `replayed_gates` y `transferred_qubits`.
