# InfiniteCore: primera etapa ejecutable y evidencia

**Resultado:** diez pruebas CTest aprobadas y siete casos de escala aprobados en
Linux. El nuevo módulo trabaja con un fondo definido por reglas, direcciones
decimales y componentes Clifford finitos, ejecutados por el `CompactCore` propio.

- Rama: `codex/infinite-core-rules-20261019`.
- Código nativo y pruebas medidos: [`a2de628ffec3d3b3c3cf797a0bac3e42984b5471`](https://github.com/guadalupejuan001-code/mundo-X/commit/a2de628ffec3d3b3c3cf797a0bac3e42984b5471).
- Base integrada: `7fd001c524ca234d98f07ba46fb074f9f4572a97`.
- [Resultados completos, salidas de pruebas y hashes SHA-256](infinite_core_2026-10-19.json).
- [Contrato, comandos, límites y compilación](../docs/infinite_core.md).

El trabajo comenzó sobre `e06ba2a`. Durante su desarrollo, `main` incorporó la
integración de 1.000 qubits y los proyectos gráficos mediante PR #16. Se actualizó
**esta rama** a esa base, preservando las nuevas operaciones empaquetadas y el
observador existente, y se recompilaron y ejecutaron las pruebas afectadas. La
rama de pruebas `codex/core-1000-qubits-20261019` permaneció en
`ef4636240af73351a18710e988346791fbdc3b58` durante estas comprobaciones. Este trabajo
no fusiona InfiniteCore con `main` ni cambia el APK que se está probando.

## Qué se implementó

`InfiniteCore` define el fondo `zero` sobre todas las direcciones no negativas o
el fondo `bell` con una pareja por cada `(2k, 2k+1)`. Una consulta finita a una
región intacta evalúa esa regla sin materializarla. Las operaciones concretas
crean los componentes necesarios; en Bell se incluyen siempre ambos miembros.

Los componentes se unen mediante la nueva operación aditiva
`CompactCore::tensor_product`, que concatena sus estados actuales con sus signos.
El módulo no conserva un historial de puertas para reconstruir esas uniones.
La medición conserva las correlaciones y confirma el estado y el generador
aleatorio conjuntamente. Los lotes rechazados no confirman cambios parciales.
La compactación libera un componente únicamente cuando sus generadores
demuestran que volvió exactamente al fondo.

El ejecutable `mundo_x_infinite`, dos programas `.xi`, pruebas independientes,
un probe de escala y un workflow propio completan esta primera implementación.
La sesión gráfica y sus formatos continúan siendo componentes separados.

La verificación de procedencia aprobó **60 fuentes byte por byte**. Siete archivos
nativos nuevos y la única cabecera revisada están fijados al commit de código
indicado; los otros 52 orígenes y los registros históricos permanecen intactos.
Los manifiestos registran la adición de `tensor_product` y conservan el rechazo
de cualquier fuente o modificación que no esté declarada.

## Pruebas de corrección y compatibilidad

Compilación local: GCC 13.3, C++17, CMake Release y opciones de advertencias.
Los valores exactos del compilador y de sus opciones están en el JSON. Permanece
un aviso de paréntesis en `CompactCore::visual_metrics` que ya estaba en la base;
la expresión no fue modificada.

| Prueba | Resultado y cobertura medida |
|---|---|
| `infinite_address` | 494 comprobaciones; 0 fallos. Incluye direcciones superiores a 64 bits, normalización y entradas inválidas |
| `infinite_core` | 220.377 comprobaciones; 146.438 comparaciones con referencia; 3.456 puertas; 202 mediciones |
| `infinite_tensor` | 106.805 comprobaciones, 1.534 comparaciones densas, 115 productos; cruces de posiciones 63/64/65/127 |
| `infinite_example_zero` | Pareja Bell entre 0 y `10^100`, correlaciones correctas e inversa con liberación exacta |
| `infinite_example_bell` | Fondo implícito, cambio y recuperación de fase, proyección con pareja correlacionada |
| `infinite_cli_contract` | 51 casos y 125 comprobaciones del proceso, incluidas entradas, errores, lotes y ejemplos |
| `compact_measurement` existente | 8.317 comprobaciones y 502 mediciones |
| `engine_session` existente | 87.296 comparaciones Pauli con signo; error máximo absoluto `3,44169e-15` |
| `core_1000` existente | 30.721 comprobaciones y 11.304 comparaciones con referencia; GHZ, puente de snapshots y límites aprobados |
| `native_core_and_xasm` existente | Los seis grupos de compatibilidad, T, Clifford+T, parser y rechazos aprobaron |

La referencia del nuevo núcleo usa circuitos pequeños de `DenseCore64` y cálculos
independientes de matrices Pauli y proyecciones sobre vectores complejos. Incluye
90 circuitos aleatorios de uno a seis qubits y un recorrido que intercaló puertas
y mediciones. Para Bell, el universo de referencia incluye las parejas completas.
Las comparaciones flotantes usan tolerancia `1e-10`; la representación por
estabilizadores y los resultados Pauli del módulo son exactos en su dominio.

Se provocaron **35 fallos de asignación** recorriendo puntos sucesivos de
operaciones de lote y medición. En esos casos se conservaron estadísticas,
observables y futuras secuencias de medición. También se comprobaron rechazos por
presupuesto, entradas inválidas, ramas imposibles, copias independientes y
compactación que no descarta cambios de fase o coherencia.

Estos ensayos sustentan los contratos cubiertos; no constituyen una demostración
formal para todos los programas posibles. El registro local tampoco representa
una ejecución de la nueva extensión en Android.

## Direcciones remotas y almacenamiento

Se ejecutó el mismo circuito `H(0); CX(0, destino)` con cuatro destinos. Todas las
variantes produjeron XX=`+1`, YY=`-1`, ZZ=`+1` y probabilidad Z de `0,5` en ambos
extremos, sin materializar direcciones intermedias.

| Destino | Dígitos del destino | Qubits activos | Bytes de tableau | Bytes de texto de direcciones |
|---|---:|---:|---:|---:|
| `1` | 1 | 2 | 34 | 2 |
| `10^9` | 10 | 2 | 34 | 11 |
| `10^100` | 101 | 2 | 34 | 102 |
| `10^1000` | 1.001 | 2 | 34 | 1.002 |

**El número de la dirección no es una cantidad de qubits procesados.** La prueba
muestra que el hueco entre direcciones no se enumera. El texto necesario para
identificarlas sí crece con sus dígitos. La regla sigue definida sobre un dominio
sin un último índice; los programas y presupuestos de cada ejecución son finitos.

En el fondo Bell se consultaron 1.000 parejas con direcciones de 1.001 dígitos:
**4.001 consultas correctas**, incluida una de 2.000 términos. Antes y después
había cero qubits activos y cero bytes de tableau materializado. El tiempo de
ese caso fue de aproximadamente 51,84 ms en esta muestra del anfitrión.

En otro caso, 10.000 puertas X en dos lotes conservaron un tableau de 17 bytes
después de la primera activación. El estado final volvió a cero y `compact`
liberó el único qubit activo. El contador de revisión avanzó por los lotes y la
compactación; no se acumuló un historial de esas 10.000 puertas dentro del núcleo.

## Componente GHZ de 1.000 qubits

El probe preparó H sobre el primer qubit y una cadena de 999 CX, en un lote.
Se obtuvo un componente de 1.000 qubits con **257.000 bytes de tableau** y
2.890 bytes lógicos de direcciones. Las consultas comprobaron:

- `P(q0=1) = P(q999=1) = 0,5`.
- `⟨Z0 Z999⟩ = 1`.
- **`⟨X⊗1000⟩ = 1`**, como comprobación de coherencia además de las correlaciones Z.
- Las consultas dejaron intactas las estadísticas y la revisión.

La construcción tardó aproximadamente **87,27 ms** y el bloque de consultas
**7,68 ms** en esa ejecución. El probe completo terminó dentro de un timeout
externo de 30 segundos. Los tiempos son muestras individuales de pared dentro
del proceso, con otras validaciones en el mismo entorno. No se infiere una
aceleración general, una tendencia entre direcciones ni rendimiento de teléfono.

## Cómo interpretar los límites

`tableau_bytes` cuenta únicamente los datos lógicos del tableau. `address_bytes`
cuenta el texto canónico una vez por dirección. **Ninguno mide RAM total.** Los
mapas, asignadores, copias de cadenas, observadores, aleatoriedad y candidatos
transaccionales necesitan memoria adicional. La consulta del fondo requiere
CPU y memoria temporal aunque no deje qubits materializados.

Los valores predeterminados son 4.096 activos, 1.024 por componente, 64 MiB de
tableaus, 4.096 dígitos por dirección y 4.096 términos por observable general.
Se pueden configurar. El núcleo rechaza operaciones que exceden sus presupuestos
sin reducir silenciosamente el problema. La memoria de un componente sigue
siendo cuadrática empaquetada; ampliar el dominio de direcciones no elimina ese
costo cuando crece un componente activo.

El alcance actual es Clifford con fondos `zero` y `bell`, puertas y consultas
finitas. No se implementaron T, reglas arbitrarias, operaciones de soporte
infinito, snapshots de estado vivo ni interfaz Android para este módulo.

## Reproducción

```sh
cmake -S . -B build-infinite -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-infinite --config Release --parallel 2 --target \
  mundo_x_infinite test_infinite_address test_infinite_core test_infinite_tensor \
  infinite_scale test_compact_measurement mundo_x_native_tests test_core_1000 test_engine_session
ctest --test-dir build-infinite -C Release --output-on-failure \
  -R '^(infinite_|compact_measurement$|native_core_and_xasm$|core_1000$|engine_session$)'
build-infinite/infinite_scale
```

La [automatización de InfiniteCore](../.github/workflows/infinite-core.yml)
repite estos contratos en Linux y Windows, comprueba la procedencia exacta y
conserva el ejecutable y la evidencia por plataforma. Los resultados de esa
automatización pertenecen a su ejecución identificada en GitHub; las cifras
de este documento proceden de la ejecución local registrada en el JSON.
