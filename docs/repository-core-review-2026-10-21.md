# Revisión del núcleo y recuperación de capacidades de Mundo-X

**21 de octubre de 2026.** Base revisada: `52150dfa928c088a32e69ce32f53c1e5a3b558c3`, más los cambios nativos descritos aquí, registrados después de las pruebas en `02e69b81f18329e38d5f0e32a5a082be27bffa3c`. Esta revisión corresponde al código local y a una compilación nueva. Los resultados anteriores se distinguen de las pruebas ejecutadas en esta campaña.

## Resultado

El núcleo contiene ingeniería útil y comprobable: representación Clifford con signos, componentes finitos, reglas globales que no enumeran el dominio, observaciones coherentes de una sesión compartida y recuperación exacta del estado y del generador aleatorio. La integración puede conservar esos beneficios si cada consumidor declara qué estado posee, qué consulta y qué recursos materializa. La existencia de varios motores especializados es parte del contrato actual.

Esta revisión corrigió un fallo reproducible de recuperación ante falta de memoria en la física 2D, recuperó la prueba extrema que había quedado fuera de la integración y recuperó la física 3D con una API validada, pasos transaccionales, selección espacial conservadora y presupuestos explícitos. La física 3D tiene un consumidor ejecutable en el CLI existente: `mundo_x physics3d [bodies] [steps]`.

La compilación Release nueva y **las 78 pruebas CTest pasaron**, sin pruebas omitidas. Los procesos se limitaron a **2 GiB de espacio virtual**. El máximo RSS de CTest y sus hijos fue **141.552 KiB**; la carga de 65.536 cuerpos 3D alcanzó **24.948 KiB**, y la prueba extrema de Ether alcanzó **17.048 KiB**. Son resultados de estas cargas en Linux x86_64; no certifican por sí solos el consumo total de una APK en un teléfono de 2 GB.

Evidencia principal: [gate-status.json](../results/repository-unification-2026-10-21/core/gate-status.json), [ctest.xml](../results/repository-unification-2026-10-21/core/ctest.xml) y [manifiesto de las 144 entradas de prueba](../results/repository-unification-2026-10-21/core/test-input-manifest.json). Los hashes de todas esas entradas se comprobaron al cerrar la campaña.

## 1. Qué representa cada motor y quién lo consume

| Componente | Estado y cálculo reales | Consumidor observado | Límite que debe conservarse |
| --- | --- | --- | --- |
| `InfiniteCore` + `EtherCore`, dentro de `InfiniteSession` | Estado físico `Ψ = U_Ether φ`: marco Clifford global y modificaciones finitas de un fondo cero o Bell. | CLI `.xi`, puentes `mxi_*`, PC y viewport compartido, Graphics y señales Solar. | La familia de puertas es Clifford; cada consulta y modificación tiene soporte y trabajo finitos. |
| `CompactCore` | Tableau estabilizador compacto, con signos y medidas; representa el estado puro salvo fase global. | Componentes de Infinite, laboratorios y registro finito anterior. | Su memoria crece con el registro materializado; no representa estados arbitrarios con T. |
| `EngineSession` | Registro finito Compact o DenseReference, más historial de puertas y observador. | Proyectos y puente gráfico finitos anteriores. | El historial crece. Los IDs ReducedT y Factorized devuelven capacidad de sesión cero y se rechazan al construir este ABI. |
| `DenseCore` / `DenseCore64` | Vector explícito de amplitudes, usado también como referencia numérica. | Pruebas, CLI y la ruta DenseReference explícita. | El número de amplitudes crece exponencialmente. No se selecciona silenciosamente como sustituto de Ether. |
| `ReducedExactTRepresentation` | Cosets de Pauli con coeficientes complejos de doble precisión sobre un tableau; permite T y fases generales. | XASM, `mundo_x_phase`, pruebas y experimentos conservados. | La expansión puede crecer exponencialmente. «Exact» no significa aritmética simbólica de precisión ilimitada. |
| `AdaptivePhaseCore` | Pares de amplitudes mientras el estado permanece producto; transferencia al motor reducido cuando corresponde. | `mundo_x_phase`, con modos adaptativo y de reproducción explícitos. | Tolerancia local de separabilidad `1e-14`; la suma de residuos aceptados no es una cota global rigurosa del error. |
| `FactorizedCore` | Producto de factores densos disjuntos en complex128, con operaciones y separaciones definidas. | `MicroLab`, que crea un registro de cuatro qubits; laboratorios y pruebas. | Coste exponencial en el tamaño del factor. La estimación de almacenamiento excluye temporales y sobrecarga del proceso. |
| `VisualScene` | Objetos derivados de reglas, IDs y excepciones finitas de altura/ocultación. | XASM y escena procedimental. | Las excepciones actuales no son un formato general de posición, velocidad y orientación de cuerpos. |
| `PhysicsWorld` | Círculos 2D, gravedad y contactos, con estado clásico propio. | API y pruebas nativas; no se encontró consumidor de producto fuera de las pruebas. | La corrección de esta revisión no constituye una mejora de rendimiento de una APK. |
| `RulePhysics` recuperado | Esferas 3D con posiciones, velocidades, masas y materiales explícitos; reglas comunes de aceleración y contacto. | Nuevo comando `mundo_x physics3d`, además de pruebas CTest. | No almacena esos cuerpos en Ether. La gravedad entre pares sigue siendo cuadrática y optativa. |
| `src/quantum_core.py`, `BridgeX`, `TransformerX` | Prototipo denso complex64, resumen de modos de bytes y Transformer pequeño de PyTorch. | Scripts y pruebas Python del prototipo. No aparecen en las rutas nativas/JNI citadas. | BridgeX conserva frecuencias y una fase circular agregada, no el orden completo de los bytes. No es almacenamiento general reversible. |

Fuentes: [InfiniteCore](../native/include/mundo_x/infinite_core.hpp), [InfiniteSession](../native/include/mundo_x/infinite_session.hpp), [EngineSession](../native/include/mundo_x/engine_session.hpp), [FactorizedCore](../native/include/mundo_x/factorized_core.hpp), [MicroLab](../native/include/mundo_x/micro_lab.hpp), [VisualScene](../native/include/mundo_x/visual_scene.hpp), [fase adaptativa](../native/include/mundo_x/adaptive_phase_core.hpp) y [prototipos Python](../src). La revisión Python fue estática; esta campaña CTest no ejecuta PyTorch ni valida entrenamiento.

Los CMake de Android incluyen `native/include` desde el repositorio. Los headers conservados de Basis VM y Quantum Echo en `android-experiments` agregan capacidades distintas; no son otra copia completa del núcleo que deba sustituirse indiscriminadamente. Las referencias históricas de procedencia sirven para explicar el origen, no para hacer retroceder las fuentes actuales.

## 2. Ether: dominio por reglas, estado finito y coherencia

El fondo cero o de parejas Bell define respuestas sin reservar un objeto por dirección. Las direcciones son cadenas decimales; la implementación no las reduce a un entero de 64 bits. Eso permite consultar sitios muy alejados con un coste relacionado con la longitud del texto y la operación, sin recorrer todos los índices intermedios.

Sobre ese fondo, `InfiniteCore` conserva componentes de `CompactCore` para las modificaciones finitas de `φ`. `EtherCore` representa las capas globales admitidas. Una operación física local se conjuga a través del marco: una consulta evalúa el observable transformado sobre el mismo estado `φ`; una puerta o medida modifica ese mismo estado físico. La normalización de las reglas y las consultas por intervalos evitan algunas expansiones, con límites explícitos del cono que se necesita resolver.

`InfiniteSession` protege el acceso con un mutex. Una observación entrega valores, estadísticas, revisión y época de una sola lectura. `query_batch` reutiliza la eliminación del observador dentro del lote; esa reutilización es preparación matemática temporal, no un caché persistente de respuestas de otros estados. Los snapshots MXI1/MXI2 conservan estado y RNG, y restaurar invalida observaciones por época incluso cuando la revisión histórica coincide. Los candidatos y la preparación previa de respuestas evitan publicar un estado parcial ante rechazos de transporte o de recursos.

La conexión compartida del PC está en `pc-android/native/pc_machine.hpp`, `quantum_device.hpp` y `native/src/infinite_bridge.cpp`: el invitado y su viewport conservan referencias a la misma `InfiniteSession`. Compartir estos headers entre APK distintas no implica compartir una instancia viva entre procesos. Los puentes finitos y la ruta de fases conservan una procedencia explícita.

### Presupuestos predeterminados

| Recurso | Límite actual | Qué cuenta |
| --- | ---: | --- |
| Qubits activos | 4.096 | Soporte materializado, no tamaño conceptual del fondo. |
| Qubits por componente | 1.024 | Tamaño de un componente estabilizador conectado. |
| Payload de tableau | 64 MiB | Payload comprometido; excluye mapas, cadenas duplicadas, capacidades, trabajo del observador y copias transaccionales. |
| Texto de una dirección | 4.096 dígitos | La interfaz sigue teniendo un límite finito de entrada. |
| Términos por consulta | 4.096 | Producto Pauli recibido. |
| Reglas del marco | 4.096 | Marco global conservado. |
| Sitios de una consulta Ether | 4.096 | Trabajo local permitido al transformar el observable. |
| Transporte de puertas | 65.536 | Puertas por lote. |
| Observables por lote | 4.096 | Consultas del transporte. |
| Términos agregados del lote | 65.536 | Suma de términos recibidos. |
| Texto agregado de direcciones | 16 MiB | Datos recibidos por el lote. |
| Snapshot | 16 MiB | Formato de intercambio, verificado antes de aceptar. |

El PC configura un límite de tableau menor, de 16 MiB. Ninguno de estos límites lógicos equivale al RSS o PSS completo del proceso. Las capas de aplicación y el controlador gráfico tienen sus propios costes.

Para no perder una capacidad útil, tampoco se fusionaron por nombre los límites de los otros motores: Factorized usa por defecto factores de hasta 20 qubits y 256 MiB de amplitudes lógicas; la ruta adaptativa admite 1..63 qubits y 65.536 componentes por defecto. En la ruta adaptativa ese presupuesto se comprueba después de cada puerta pública y sus mapas temporales pueden ser mayores, tal como ya declara [phase-engine.md](phase-engine.md). No se ha añadido una garantía de pico de RAM a esas APIs.

## 3. Material recuperado e integrado

### Prueba extrema del PR20

Se recuperó el propósito de `native/tests/infinite_extreme.cpp` de `d4de6d0`, adaptando `QueryRef`/`expectations` a la API actual de `InfiniteSession`. La antigua exigencia de retener exactamente un qubit después de 65.536 X se sustituyó por el contrato físico: resultado correcto, soporte retenido como máximo uno y compactación exacta al fondo. La implementación actual puede devolverlo inmediatamente al fondo.

La nueva prueba comprueba GHZ de 1.024 qubits, 256 correlaciones ZZ con oráculo analítico y 32 comparaciones por la ruta escalar, un solo observador construido para el componente, lectura sin mutación, rechazo transaccional al exceder 1.024 qubits por componente, 4.096 consultas Bell de 1.001 dígitos sin materializar sitios, límites de transporte y 65.536 puertas X. Se retiraron umbrales de velocidad que no formaban parte del contrato de corrección.

El hunk de `infinite_scale.cpp` del PR21, commit `83e5faee209f30b53453d13646417ef733db6356`, medía GHZ 1.000 / 24 ZZ / cinco repeticiones. Su carga ya está representada en `ether_stress batch 1000 24`, con la API actual, signos alternos, oráculo analítico y comprobación de snapshot/RNG. Se ejecutó esa carga y se conserva [su resultado](../results/repository-unification-2026-10-21/core/ether-batch-1000-24.log); no se añadió una segunda implementación obsoleta del mismo propósito.

### Física 3D del PR21

Se recuperó `RulePhysics` de `fd0bd14`. La versión anterior exponía un vector mutable de cuerpos, convertía `ceil(seconds / max_substep)` a `unsigned` antes de comprobar el rango, mutaba cuerpos a lo largo del paso y recorría todos los pares para colisiones. Esas propiedades no eran una base suficiente para integrarla como capacidad actual.

La versión integrada conserva las ecuaciones originales de Euler semiimplícito, impulsos de esferas y gravedad suavizada, y agrega:

- Validación de cuerpos y reglas en `add`, `set_body` y `set_rules`; `bodies()` es de solo lectura.
- Comprobación finita y de rango antes de convertir el número de subpasos.
- Un candidato reutilizable para todos los subpasos; un fallo conserva cuerpos, revisión y estadísticas del último paso publicado.
- Un índice espacial 3D conservador para más de 128 cuerpos. Ordena candidatos por ID, actualiza el índice y vuelve a consultar después de un contacto para conservar el recorrido lexicográfico original.
- Recorrido directo en escenas densas. No se omiten fuerzas ni contactos cuando una escena resulta costosa: exceder un presupuesto rechaza el paso completo.
- Límites predeterminados de 65.536 cuerpos, 4.096 subpasos, ocho millones de pruebas de pares y 64 millones de visitas al árbol por paso público. El límite de pares suma colisiones y gravedad.
- Estadísticas separadas de contactos, candidatos, gravedad, visitas, subpasos y capacidad de trabajo temporal.

La gravedad entre pares sigue recorriendo los pares dinámicos y sigue siendo O(n²). Los cuerpos de masa inversa cero son fijos y conservan el tratamiento del modelo original. No hay orientación de cuerpos rígidos, mallas arbitrarias, rozamiento general, detección continua de colisiones ni una garantía de conservación exacta de energía a cualquier paso temporal.

El consumidor `mundo_x physics3d` avanza realmente todos los cuerpos, calcula un checksum global y devuelve como máximo las primeras ocho posiciones. Su JSON declara `body_state: explicit_finite`; sus contadores de payload y capacidades no se etiquetan como RSS. Integrarlo directamente en `VisualScene` habría requerido cambiar el contrato de posiciones e identidades del renderizador; esa conversión no se ha fingido.

## 4. Defecto actual demostrado y corregido en PhysicsWorld

`PhysicsWorld::step` reutilizaba el índice cuando `order_.size()` coincidía con el tamaño del candidato. Durante una primera construcción, la asignación de `order_` podía completarse y una asignación posterior de nodos podía fallar. En el siguiente paso, esa coincidencia de tamaños permitía consultar un árbol incompleto.

El [probe conservado](../results/repository-unification-2026-10-21/core/physics-allocation-probe.cpp) provocó una sola falta de memoria sobre 600 cuerpos. El primer paso se rechazó y conservó el cuerpo observado; el reintento terminó en **SIGSEGV, código -11**, bajo el límite de 2 GiB. [La evidencia anterior](../results/repository-unification-2026-10-21/core/physics-allocation-before.json) identifica la base y los hashes del header y del probe.

La corrección agrega una marca `index_ready_`: se invalida antes de cualquier construcción que pueda fallar y se activa únicamente cuando el árbol está completo. El mismo criterio protege el índice de la física 3D recuperada. No cambió el orden de contactos ni la aritmética de la API 2D.

La prueba de regresión `physics_allocation` provoca fallos en distintas asignaciones reales, exige conservar el estado publicado y compara el reintento con una referencia que no falló. En la compilación final recuperó cinco puntos de fallo 2D y cinco 3D, con **11.730 comprobaciones**. El JSON de la primera validación 2D, de 9.010 comprobaciones, se conserva como evidencia intermedia; el XML CTest final contiene el resultado ampliado.

## 5. Validación actual y memoria

Compilador: GCC 13.3.0, C++17 Release. CMake recuperado localmente: 3.22.1. Plataforma: Linux x86_64. Compilación y pruebas secuenciales, `OMP_NUM_THREADS=1` y `OPENBLAS_NUM_THREADS=1`. Cada proceso hijo heredó `RLIMIT_AS=2.147.483.648` bytes; los diagnósticos adicionales tuvieron también 30 segundos de CPU y 40 de pared. RSS se obtuvo de `wait4/rusage` de Linux, no de los contadores internos de los motores.

| Comprobación nueva | Resultado | Máximo RSS |
| --- | --- | ---: |
| Compilación completa | Aprobada; 235,18 s de pared. | 331.236 KiB |
| CTest | 78/78, cero fallos y cero omitidas. | 141.552 KiB |
| `physics_allocation` | 11.730 comprobaciones; recuperación en 5 asignaciones 2D y 5 3D. | Incluido en CTest. |
| `rule_physics` | 442.462 comprobaciones; 64 escenas / 256 pasos iguales campo por campo a un recorrido independiente de todos los pares; rutas densas y dispersas. | Incluido en CTest. |
| Selección espacial 3D | 4.096 esferas separadas: cero pares de colisión comprobados y menos de 4 MiB de capacidad temporal. | Incluido en CTest. |
| CLI 3D, 65.536 cuerpos | Un paso de 1/120 s, un contacto; payload de cuerpos 4.718.592 bytes y capacidad temporal 18.874.288 bytes. | 24.948 KiB |
| `infinite_extreme` | 4.399 comprobaciones; GHZ 1.024, consultas remotas y límites descritos arriba. | 17.048 KiB |
| GHZ 1.000 / 24 ZZ / 5 lotes | Valores analíticos correctos; snapshot/RNG conservados. Tableau: 257.000 bytes. | 6.944 KiB |

Los 256 pasos de referencia 3D comparan nueve campos por cuerpo con igualdad numérica `==`, sobre la misma plataforma y compilación; no acreditan identidad bit a bit entre arquitecturas distintas. La campaña comprueba que el índice evita pares imposibles sin cambiar el orden de los contactos que sí pueden ocurrir.

Se conservan dos intentos iniciales de CTest que no pudieron arrancar `infinite_tensor`, porque el ejecutable local apareció con permisos 0644. La corrección de permisos entre invocaciones separadas no persistió. Restaurar el modo del binario y ejecutarlo en la misma invocación produjo 106.805 comprobaciones aprobadas; el wrapper aplicó esa preparación inmediatamente antes de la campaña completa, que terminó en 78/78. Los bytes del ELF y los hashes fuente permanecieron iguales. Los logs iniciales no se han reemplazado por éxitos ficticios.

Estos tiempos son diagnósticos de ejecución, no una comparación de rendimiento contra otro motor. No se repitieron las comparaciones históricas de Solar, reflejos o simuladores externos. La presente prueba bajo 2 GiB verifica estas cargas del núcleo; una afirmación de producto sobre un teléfono necesita incluir PSS/RSS, memoria gráfica, servicios del sistema y la carga elegida del usuario.

## 6. Valor y siguiente estructura útil

Hay valor técnico en conservar un estado por reglas y desviaciones, componer transformaciones admitidas, responder observaciones finitas sin expandir todo el dominio y mantener una sesión coherente entre propietario y vistas. La prueba extrema demuestra una instancia concreta: consultar 4.096 sitios de 1.001 dígitos sin crear tableau para esos sitios. La recuperación de fallos aporta otra propiedad práctica: un presupuesto o una asignación fallida no destruye el estado utilizable.

La estructura objetivo de Ether, ojos y espejos puede avanzar respetando esas propiedades. El [contrato de arquitectura](ether-architecture.md) ya separa objetivo e implementación. El [diseño de espejos](ether-mirrors.md) observa una sola escena y mantiene identidades, con composición de trayectos de una familia geométrica concreta. Ese cálculo óptico y su coste GPU pertenecen al observador; esta revisión nativa no los convierte en qubits ni revalida sus resultados históricos de imagen.

La próxima integración útil debe escoger variables y operaciones concretas de un consumidor, conservar una autoridad de estado y demostrar dependencia causal de la salida. Por ejemplo, integrar cuerpos 3D exige una correspondencia de IDs, posiciones, velocidades, reglas, tiempo y revisión, además de un formato de snapshot y una referencia física. Un adaptador puede compartir propiedad y revisión, pero no puede declarar que la física se calcula en Ether si conserva toda la evolución en otro vector sin una correspondencia verificable.

Tampoco procede borrar los motores de fases y factores por no ser Clifford: amplían la familia de estados representables con sus propios costes. La separación útil consiste en capacidades explícitas, presupuestos físicos y lógicos separados, observaciones identificadas por sesión/revisión y evidencia reproducible. Los prototipos y los informes anteriores deben conservar su condición histórica hasta que un consumidor actual los utilice y se valide esa ruta.

Estas propiedades apoyan una valoración de ingeniería especializada. Esta auditoría no mide valor económico, novedad patentable, consumo energético ni superioridad frente a todos los simuladores. El código y los resultados tampoco demuestran compresión eficiente de estados cuánticos arbitrarios, computación universal ilimitada o RAM física infinita.

## 7. Fuentes cambiadas y repetición

Las fuentes modificadas son `native/include/mundo_x/physics_world.hpp`, `native/src/main.cpp` y `CMakeLists.txt`. Las añadidas son `native/include/mundo_x/rule_physics.hpp`, `native/tests/rule_physics.cpp`, `native/tests/physics_allocation.cpp` y `native/tests/infinite_extreme.cpp`. La campaña guarda 96 hashes del alcance de revisión y 144 de las entradas completas que participan en pruebas.

El manifiesto canónico `tools/cosmic_source_provenance.py` declara los archivos nativos modificados y añadidos de esta integración con `RECOVERED_PHYSICS_REFERENCE = 02e69b81f18329e38d5f0e32a5a082be27bffa3c`. Las referencias anteriores de los archivos sin cambios se conservan. La revisión no incorporó los headers físicos a una procedencia JNI, porque no participan en esa compilación móvil.

En un entorno con CMake, C++17 y Python:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 1
ctest --test-dir build --parallel 1 --output-on-failure
build/mundo_x physics3d 65536 1
build/test_infinite_extreme
build/ether_stress batch 1000 24
```

La reproducción exacta de límites, rutas y recogida de recursos de esta sesión está en [run-core-gates-as-executed.py](../results/repository-unification-2026-10-21/core/run-core-gates-as-executed.py). Sus rutas absolutas documentan el entorno utilizado; deben adaptarse al trasladar el repositorio. El [estado final](../results/repository-unification-2026-10-21/core/gate-status.json) enlaza cada comando, salida, medición, hash fuente y hash de los binarios principales.
