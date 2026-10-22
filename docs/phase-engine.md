# Fases generales y ruta adaptativa de Mundo X

La revisión f858053 no podía ejecutar el sumador de 50 qubits usado en la
comparación anterior porque el motor reducido no exponía fases generales.
Esta ampliación agrega `phase(q, theta)` y `controlled_phase(c, t, theta)`
a `ReducedExactTRepresentation`, manteniendo su descomposición en cosets de
Pauli. Agrega también observables X/Y/Z calculados directamente en esa
representación, sin expandir el vector de amplitudes.

`AdaptivePhaseCore` es una ruta opcional. Conserva un par de amplitudes por
qubit mientras el estado sea producto. Cada puerta de dos qubits se aplica
primero a sus cuatro amplitudes; se comprueba si el resultado sigue siendo
separable. Si aparece entrelazamiento por encima de la tolerancia, se
transfiere el estado producto al motor reducido. La reproducción del historial
original queda disponible como `adaptive-replay`. No se reconoce una
suma por su nombre ni se sustituye el circuito por aritmética clásica.

Los motores anteriores permanecen disponibles. El nuevo ejecutable es
`mundo_x_phase`; esta ampliación no cambia automáticamente el motor de la
interfaz Android.

La [transferencia de estado](state-transfer.md) documenta la implementación
actual y sus comprobaciones. Los resultados de las secciones siguientes son
históricos: corresponden a `40a9c8215fbe3e0bdc7a1242b3f9a982344d8d4e`, antes
de eliminar el historial de la ruta producto predeterminada.

## Resultado y decisión de la primera versión

Se conserva la ruta opcional: pasó la prueba principal, superó el umbral
de mejora del 15% frente a Aer MPS y las cinco cargas anteriores quedaron
dentro del margen de regresión del 15% establecido antes de medir.

Medianas, CPU con un hilo, inicialización + evolución + lectura de X/Y/Z
en cada qubit. La columna Aer usa el tiempo de ejecución comunicado por
su motor, excluyendo la sobrecarga de su API Python.

| Circuito | Mundo X adaptativo | Aer MPS | Aer / Mundo X |
|---|---:|---:|---:|
| Sumador 12 qubits, entrada de base | 3,175 µs | 424,490 µs | 133,70 |
| Sumador 20 qubits, entrada de base | 8,192 µs | 1211,285 µs | 147,86 |
| Sumador 50 qubits, entrada publicada | 46,689 µs | 11731,368 µs | 251,27 |
| Sumador 12 qubits, con superposición | 2571,502 µs | 419,161 µs | 0,163 |

**El último caso es una derrota:** Aer resultó aproximadamente 6,13 veces
más rápido. La mejora se conserva para la clase de estados producto donde
se midió la ventaja, sin sustituir las rutas anteriores ni declarar una
ventaja universal. La expansión reducida sigue pudiendo crecer mucho con
estados entrelazados y fases generales.

El sumador de 50 qubits ejecutó sus 1000 puertas unitarias y devolvió
21.346.502 + 12.207.929 = 33.554.431. La carga lógica de la ruta nueva fue
33.600 bytes, incluyendo historial y amplitudes producto. El máximo RSS del
proceso observado fue 2,00 MiB; el tiempo mediano del proceso completo fue
1,915 ms, incluyendo arranque, lectura del programa y salida. El RSS incluye
el runtime y no es una comparación aislada entre representaciones.

La ventaja de tiempo se refiere a estas implementaciones, esta carga y este
entorno. No se ensayaron todos los simuladores existentes, no se ejecutó
JUPITER y no se midió energía ni costo monetario. El sumador con entradas
clásicas mantiene un estado producto: este ensayo no mide una gran cantidad
de entrelazamiento.

## Validación

- 131 circuitos contra Qulacs FP64, ejecutados con ambas rutas: 262 pruebas.
- Incluyen las 84 entradas de base de sumadores de 1, 2 y 3 bits por registro,
  circuitos aleatorios, superposiciones y transiciones tardías al motor reducido.
- Error máximo en los observables compactos: 3,22e-15.
- Error máximo de amplitud normalizada: 8,35e-16. La reconstrucción densa
  heredada usa complex64: su error absoluto sin normalizar llegó a 1,20e-7.
- Otras 16 parejas de enteros de 25 bits comprobadas contra la suma esperada
  y Aer MPS, además de la pareja publicada.
- Cinco cargas anteriores medidas alternando versión base y candidata:
  GHZ 24/1024, cluster 1024, estructurada 20 con 64 T y producto 16 con T.
  Las diferencias de mediana estuvieron entre -0,11% y +4,21%, dentro del
  margen establecido. Su carga lógica reportada se mantuvo igual.
- Pruebas de errores de entrada, límite de componentes, signo de Y,
  reproducción de fases y rechazo de lecturas tras una ejecución fallida.

Se usaron 35 mediciones por caso y rival para la comparación principal,
distribuidas en cinco grupos, con calentamiento. Se probaron las dos
direcciones de intercambio de Aer MPS y se seleccionó la más rápida.
El rival no tuvo límite impuesto a la dimensión de enlace.

## Precisión y límites

La ruta producto admite un residuo relativo local de factorización de
`1e-14`, para absorber ruido de coma flotante. Es una tolerancia explícita;
`product_residual_sum()` informa la suma de residuos aceptados. No es una
cota rigurosa de todo el error numérico. En `adaptive-replay`, pasar al motor
reducido reproduce las puertas originales y descarta las aproximaciones
producto. En `adaptive`, la transferencia conserva las amplitudes calculadas
y el registro de sus residuos aceptados; no borra ese historial de error.

El motor reducido no poda componentes por ser pequeños. La ruta adaptativa
usa por defecto un presupuesto de 65.536 componentes, comprobado al final
de cada puerta pública; los mapas temporales dentro de una puerta pueden
ser mayores. Superarlo provoca un error y bloquea las lecturas del estado.
La API admite de 1 a 1.000 qubits mediante máscaras Pauli multi-palabra; la reconstrucción densa para validar sigue
limitada a 23. No se garantiza ejecución eficiente para cualquier circuito.

## Uso

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 2
ctest --test-dir build --output-on-failure
build/mundo_x_phase examples/adder50.qx adaptive 35
```

El archivo empieza con el número de qubits. Una puerta por línea:
`H q`, `X q`, `Z q`, `S q`, `T q`, `P q theta`, `C control target`
(CNOT), `D control target` (CZ), `Q control target theta` (fase controlada).
Los ángulos están en radianes. Los índices de amplitud usan el qubit 0 como
bit menos significativo. El sumador asigna el bit más significativo del
entero al primer qubit de cada registro.

La salida es JSON con tiempos, observables ordenados X/Y/Z por qubit,
modo usado, norma y carga lógica. El argumento final `dump` añade todas
las amplitudes para circuitos pequeños. `reduced` selecciona directamente
el motor de cosets para diagnóstico; no es la ruta rápida del ensayo grande.
`adaptive-replay` conserva la transición anterior. Los contadores
`history_gates`, `replayed_gates` y `transferred_qubits` explican qué transición
ocurrió. La validación actual ejecuta tres rutas: 393 comprobaciones.

Pruebas independientes, con Python 3.12 y `tests/phase_requirements.txt`:

```bash
OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 QULACS_NUM_THREADS=1 \
python tests/phase_validation.py --runner build/mundo_x_phase --output validation.json
OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 \
python tests/phase_benchmark.py --runner build/mundo_x_phase --output-dir benchmark-output
python tests/phase_regression.py --old-repo ../mundo-x-base \
  --new-repo . --output regression.json
```

La referencia de regresión debe ser el commit
`f858053bf0a47e6c760428f3526ec74ff89e40c3`. Los resultados resumidos están
en `phase-results-2026-10-16.json`. Entorno: AMD EPYC 9V74, GCC 13.3,
8 GiB de límite de memoria, contenedor compartido.

Fuentes de la carga y del rival:

- [Artículo y figura del sumador](https://arxiv.org/pdf/2511.03359v3).
- [Opciones de Aer MPS](https://qiskit.github.io/qiskit-aer/stubs/qiskit_aer.AerSimulator.html).
- [Lectura de valores esperados](https://qiskit.github.io/qiskit-aer/stubs/qiskit_aer.library.save_expectation_value.html).

El sumador se reconstruyó desde la figura del artículo: no se dispuso del
archivo de entrada original de JUPITER. Los dos simuladores de esta
comparación sí recibieron exactamente la misma lista de puertas.
