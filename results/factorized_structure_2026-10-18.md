# FactorizedCore: estructura exacta y estabilidad de separaciones

## Problema y cambio

Se parte de `d4f55203032aaae5b886696b029fd87762cd9520`, sin modificar
CompactCore, ReducedExactTRepresentation ni la entrega Android.

FactorizedCore fusionaba dos factores al aplicar una puerta entre ellos aunque
esa puerta no creara entrelazamiento. También acumulaba la escala global de una
separación en un solo factor: tras sucesivas fusiones y separaciones con nuevos
compañeros, elevar sus coeficientes al cuadrado producía una norma cero por
subdesbordamiento. La representación podía fallar aun siendo un producto.

La corrección contiene tres cambios:

1. **Identidades de puertas sobre destinos independientes.** CX con destino
   `|+>` deja el estado igual; CX con destino `|->` aplica Z al control. CZ con
   destino `|0>` deja el estado igual y con destino `|1>` aplica Z al otro lado.
   El reconocimiento usa igualdad exacta de los coeficientes, conserva su fase
   compleja y funciona aunque el control pertenezca a un bloque entrelazado.
   Las puertas reconocidas no asignan memoria ni fusionan factores.
2. **Equilibrio de escala en separaciones.** Después de verificar la separación
   coeficiente por coeficiente, se proponen factores recíprocos `2^e` y `2^-e`
   para equilibrar las normas de ambos lados. Se exige reversibilidad de cada
   coeficiente y la misma reconstrucción exacta del producto. Si esto falla,
   se conserva la separación original ya verificada. Se comprueba conservar
   repetidamente tanto el lado izquierdo como el derecho.
3. **Probabilidades locales resistentes a la escala.** La consulta divide por
   el mayor componente finito antes de elevar al cuadrado. Esto evita normas
   cero o infinitas causadas solo por la escala del factor, sin alterar el estado.

Estas son identidades conocidas y una corrección de estabilidad numérica. No
constituyen una demostración de una ley física nueva ni de compresión universal.
La idea y dirección de Mundo X corresponden a su titular; esta implementación
concreta se realiza con asistencia de Codex.

## Comparación con el código anterior

El mismo programa se compila contra el header original y el modificado. Se
mantienen presupuestos, compilador y entradas. Los resultados completos,
huellas de los headers y entorno están en
[factorized_structure_2026-10-18.json](factorized_structure_2026-10-18.json).

| Circuito y comprobación | Código anterior | Código modificado |
| --- | --- | --- |
| 20 qubits, H en todos, T en control y CX a destinos `|+>` | 1 bloque, 16.777.216 bytes de amplitudes | 20 factores, 640 bytes de amplitudes |
| Memoria propia estimada, mismo circuito de 20 qubits | 16.778.880 bytes | 1.456 bytes |
| Pico RSS del proceso, mismo circuito de 20 qubits | 26.955.776 bytes | 1.785.856 bytes |
| Mismo patrón con 100.000 qubits | Rechaza al intentar incorporar el compañero 20, por el límite de bloque de 20 qubits | Completa las 99.999 CX; 3.200.000 bytes de amplitudes y 5.600.336 bytes propios estimados |
| 8.192 qubits, dos CZ y separación por cada compañero nuevo | Falla la consulta de probabilidad en el compañero 1.074 | Completa 8.191 parejas y separaciones |

En el patrón de 20 qubits, ambos programas calculan el mismo estado producto
analítico. La reducción de amplitudes almacenadas es de **26.214,4 veces**; no
es una reducción equivalente del RSS ni una comparación con otros simuladores.

La puerta T del control aporta una fase no Clifford, pero este circuito sigue
siendo separable: los destinos `|+>` son autoestados de X. El resultado no debe
presentarse como un circuito genérico de 100.000 qubits entrelazados.

El sondeo valida todas las probabilidades locales y una amplitud logarítmica
contra el estado producto analítico, sin exportar el vector denso. En el caso
de 100.000 qubits, el error máximo de probabilidad es `5,55e-17`, el error del
logaritmo de la amplitud es `1,46e-11` y el de fase es cero. En las 8.191 parejas
de CZ, el error de probabilidad es cero y el del logaritmo es `4,55e-13`.

El pico RSS se lee de `/proc/self/status: VmHWM` en cada proceso nuevo; no se
usa el máximo heredado de `getrusage`. Los tiempos son una ejecución exploratoria
por variante, sin inferencia estadística de aceleración. Son medidas en este
host Linux, no en un Samsung A16 ni en otro teléfono físico.

## Validación de corrección

Se compilaron directamente con GCC 13.3 y
`-std=c++17 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -pthread` los **13 ejecutables**
registrados para Linux en CMake; todos terminaron con código cero. CMake no
estaba instalado en el entorno de esta revisión; no se presenta esta ejecución
como una ejecución de CTest. Los resultados se registran en
[factorized_structure_tests_2026-10-18.json](factorized_structure_tests_2026-10-18.json).

- `factorized_core`: 44.814 verificaciones, incluida comparación de amplitudes
  con DenseCore64, fases, circuitos aleatorios, separación y rechazo transaccional.
- `factorized_storage`: 2.592 verificaciones y 51 fallos de asignación inyectados.
  Las nuevas identidades se ejecutan sin ninguna asignación dinámica.
- `factorized_structure`: 94.726 verificaciones. Compara amplitudes complejas
  completas en casos pequeños, comprueba retroacción de fase con un control
  entrelazado, protege rechazos para entrelazamiento real y repite 8.191
  separaciones en cada orientación. Incluye el patrón de 100.000 qubits con un
  presupuesto máximo de un qubit por bloque.
- Los otros diez ejecutables de las pruebas originales, contando
  `factorized_diversity`, conservan sus criterios y pasan. No se relajan tolerancias.

Dos preparaciones existentes que comprobaban rechazos/asignaciones se cambian
de `H` a `H; T` en el destino: un destino `|+>` ahora evita legítimamente la
fusión, mientras `H; T` sí exige entrelazarlo y conserva el objetivo de la prueba.
Permanece una advertencia previa de paréntesis en `CompactCore::visual_metrics`;
el header y las nuevas pruebas de FactorizedCore compilan sin advertencias.

## Reproducción

Desde la raíz del repositorio, con Python 3 y g++ en Linux:

```sh
python3 tools/examine_factorized_structure.py --base d4f5520
```

El script extrae exclusivamente el header anterior mediante `git show`, compila
ambas variantes en un directorio temporal y entrega JSON. No cambia las ramas.
Para la batería habitual, en un entorno con CMake:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Alcance

Se conservan puertas, API, presupuestos y comprobaciones estrictas de separación.
El código sigue usando `complex<double>` y no es aritmética simbólica exacta.
Las separaciones pueden tener falsos negativos y el reequilibrio se omite cuando
no es reversible. La nueva consulta no recupera coeficientes ya borrados por
un subdesbordamiento anterior.

No se añade una búsqueda automática de todas las particiones. Un bloque general
de k qubits sigue almacenando `2^k` amplitudes. FactorizedCore continúa separado
del núcleo Compact utilizado por la aplicación Android y del backend Reduced.
Esta mejora elimina crecimiento evitable en las familias verificadas; la
eficiencia general frente a entrelazamiento y circuitos difíciles queda abierta.
