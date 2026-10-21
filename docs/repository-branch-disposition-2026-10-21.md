# Disposición de ramas y borradores de mundo-X — 2026-10-21

La [consolidación](repository-unification-2026-10-21.md) registra los rescates y
correcciones posteriores a esta fotografía inicial. Las expresiones «pendiente»
de este inventario describen lo que faltaba en la referencia fija, no sustituyen
el estado posterior de PR25.

**Referencia fija auditada:** `52150dfa928c088a32e69ce32f53c1e5a3b558c3`. **Main remoto:** `7b028db2344912b2ab86a0affa2a1fe56180922b`. **Captura de API:** `2026-10-21T00:01:25.574Z`. La consolidación se trabaja desde `codex/unify-mundo-x-20261021`; los resultados posteriores deben quedar ligados a sus propios commits.

## Resultado y decisión de integración

Se inventariaron las **35 ramas remotas y los 25 pull requests** del repositorio. De las ramas, **24 heads completos ya son ancestros** de la referencia recuperada y **11 conservan 70 commits distintos fuera de su ascendencia**. Estos 70 commits no equivalen a 70 cambios funcionales pendientes: incluyen squashes ya absorbidos, entregas binarias, disparadores de CI y variantes anteriores de código actualmente corregido. Los únicos PR abiertos en la captura son **#19, #20, #21, #22 y #25**.

La base recuperada está 54 commits por delante de main y 42 por delante de PR25. PR25 ya es su ancestro exacto: sirve para publicar una consolidación mediante avance de su rama, sin crear otro borrador ni reintroducir su código. Las ramas #19–22 comparten la antigua base `05595a150d6956a5535e8618ffd94017020d5289` y cada una carece de 118 commits de la referencia actual. Se recomienda incorporar sus aportes útiles de forma selectiva, validar la consolidación y entonces cerrar esos cuatro PR con un enlace a la integración.

Los faltantes funcionales detectados son **la regresión Android ISO ENTER/LEAVE, el probe InfiniteCore extremo, RulePhysics 3D y la reutilización exacta de framebuffer en Android**. También se debe conservar o mapear explícitamente la carga GHZ de 1000 qubits/24 observables y la prueba de propiedad de la sesión a 8K. El culling scissor de la rama antigua no se importa tal cual: su caja de proyección directa no prueba la cobertura de los espejos actuales.

Este documento deja una fotografía verificable de lo que faltaba en `52150df`. Las acciones marcadas como rescate pendiente describen el plan de consolidación; no afirman que ya se haya compilado, probado, publicado o cerrado ningún PR. El inventario no modifica el estado remoto.

## Método y fuentes

- [Ramas, página completa de 100](https://api.github.com/repos/guadalupejuan001-code/mundo-X/branches?per_page=100) y [PR de todos los estados, página completa de 100](https://api.github.com/repos/guadalupejuan001-code/mundo-X/pulls?state=all&per_page=100&sort=created&direction=desc). Las respuestas contienen 35 y 25 entradas, respectivamente. El JSON adjunto conserva SHAs, fechas, estado, cuerpos y URL de los PR.
- Root actualizó `origin --prune` antes del cotejo. Se compararon las refs obtenidas con una referencia fija, para que las ediciones simultáneas de la consolidación no alterasen la conclusión del inventario.
- `git merge-base --is-ancestor`, los rangos de `git rev-list`, y `git cherry` separan ascendencia y equivalencia de parche. Los blobs de los archivos modificados desde la base común se cotejaron por identidad Git y los hunks importantes se leyeron en sus consumidores.
- Un patch-id negativo de un commit entero no demuestra que todos sus cambios falten. PR19 y PR22 contienen importaciones selectivas; PR1 contiene un squash de varios commits. Un head ancestro tampoco prueba por sí solo que los tests actuales pasen.

El detalle legible por máquina está en [`branch-inventory.json`](../results/repository-unification-2026-10-21/branch-inventory.json). Los enlaces de código de este documento fijan commits, no ramas móviles.

## Aportes que requieren rescate o decisión explícita

### PR19: ENTER nativo incluido; falta la regresión Android de la ISO

El commit original [`57a8b339`](https://github.com/guadalupejuan001-code/mundo-X/commit/57a8b339f9580ff7a41d3da25ca4636aef5e2388) ya aportó el arreglo nativo mediante la recuperación `2d22061`. En la referencia actual son idénticos a la rama `pc-android/CMakeLists.txt`, `native/tests/x86_cpu_test.cpp`, `native/tests/x86_enter_oracle_test.cpp` y `tools/test-native.sh` dentro de `pc-android/`. `native/x86_cpu.hpp` incorpora además mejoras posteriores del recorrido de páginas.

El caso `testIsoEnter` falta en `pc-android/app/src/androidTest/java/com/cosmicx/pc/PcInstrumentation.java`. Genera una ISO El Torito propia que carga en `2000:0000`, ejecuta `C8 10 00 02` en `2000:056c`, comprueba el marco de nivel 2, las variables locales, las banderas conservadas y la restauración de BP/SP por LEAVE. El huésped sólo imprime `ENTER/LEAVE: OK` después de comparar sus propios registros. Su evidencia es `pc-enter-state.json`.

Se deben rescatar sólo helpers, fixture, caso, llamada y verificación del resultado, pasando el inventario de 29 a 30 pruebas. La copia completa de la instrumentación de la rama eliminaría `graphicsOwner`, `etherProgramRoundtrip`, las comprobaciones de lectura sin mutaciones y la evidencia `ether_fusion`; esas verificaciones actuales se conservan. La versión 0.4.1 de la rama tampoco debe arrastrar una regresión de las fuentes al actualizar metadatos de PC.

### PR20: probe extremo útil; culling antiguo incompatible con la cobertura de reflejos

`native/tests/infinite_extreme.cpp` está ausente. Procede de [`14b00e2d`](https://github.com/guadalupejuan001-code/mundo-X/commit/14b00e2d0cf922353ce0abf5462786a48934f377) y la corrección [`ea1626ca`](https://github.com/guadalupejuan001-code/mundo-X/commit/ea1626ca3b2ea90c3052b04d62142e608e3b3e8f). Conserva tres cargas útiles: GHZ de 1024 qubits y 256 consultas ZZ comparando scalar/batch; 4096 consultas en direcciones de 1001 dígitos sin materializar el fondo; y 65536 operaciones X con rollback de una operación inválida y compactación.

El probe requiere adaptarse a `ObserverCache` y a las consultas actuales. La aserción antigua de un qubit activo tras una secuencia identidad no es un contrato estable de la representación implícita. Debe preservarse la equivalencia observable, el rollback y la ausencia de materialización innecesaria. Los tiempos son evidencia medida en la máquina concreta, no un requisito de speedup universal.

El scissor de [`964db0e4`](https://github.com/guadalupejuan001-code/mundo-X/commit/964db0e4e1d0fc2e6fe8f58741566971091eb7a8) calcula una caja a partir de ocho esquinas del mundo directo; no incluye las imágenes reflejadas de los espejos añadidos después. Sus pruebas A/B de [`63a01567`](https://github.com/guadalupejuan001-code/mundo-X/commit/63a01567884449356805292b4a357841c21a47d7) deben acompañar cualquier adaptación, con espejos y picking/export cubiertos. El verificador de la rama elimina la obligación actual `native_ether_contracts` y exige `baseline > optimized` en cualquier dispositivo. Ese cambio completo no es aceptable como integración.

La comprobación de InfiniteCore como propietario vivo del framebuffer a 8K ([`37b5f36c`](https://github.com/guadalupejuan001-code/mundo-X/commit/37b5f36caed27e3307e5fb39105751b1712bc36e)) merece una correspondencia explícita con los contratos Ether actuales o el rescate selectivo de sus afirmaciones de propiedad/revisión/lectura. No debe introducir un backend antiguo para recuperar el nombre de una métrica.

### PR21: RulePhysics 3D y reutilización Android no estaban absorbidos

`native/include/mundo_x/rule_physics.hpp` y `native/tests/rule_physics.cpp` faltan. Son el módulo de esferas 3D de [`9031aebe`](https://github.com/guadalupejuan001-code/mundo-X/commit/9031aebe16f7cf68a7972c31be6b17049f28f217) y [`e293be5a`](https://github.com/guadalupejuan001-code/mundo-X/commit/e293be5a2c746d3213dd821d7abda1d8bc341cce), distinto de `PhysicsWorld` 2D ya incorporado. No se encontró un consumidor que conecte el módulo antiguo al renderer.

Antes de recuperarlo se deben corregir el acceso mutable a `bodies()` que elude validación, el cálculo/conversión a entero de subpasos fuera de rango y la posibilidad de mutación parcial cuando falla un paso. La gravedad por pares conserva coste cuadrático; este módulo debe permanecer aislado del motor 2D y describir los límites de su integrador y colisiones. Esta auditoría de fuente no ejecutó una compilación del módulo antiguo.

La reutilización Android de [`87629f7b`](https://github.com/guadalupejuan001-code/mundo-X/commit/87629f7bdcec4ff9c75f1a29a1ba24efe767e23e), invalidación [`fd0bd14d`](https://github.com/guadalupejuan001-code/mundo-X/commit/fd0bd14dc5a9709703ced7fb61fe031a1c266b75) y prueba [`ea5f35ea`](https://github.com/guadalupejuan001-code/mundo-X/commit/ea5f35ea806b388e5075919dee20cd1ab0e39219) también faltan. El renderer desktop ya tiene reutilización exacta, pero `QuantumRenderer.java` en `52150df` sigue dibujando cada frame. La propuesta original compara sólo un hash FNV de 64 bits y no incluye espejos; se adapta con igualdad exacta del estado, escala/resolución, core/edits, cámara, contexto GL y destinos de debug/picking.

El benchmark [`83e5faee`](https://github.com/guadalupejuan001-code/mundo-X/commit/83e5faee209f30b53453d13646417ef733db6356) aporta GHZ 1000, 24 observables ZZ y cinco repeticiones. Su hunk no está en `native/tests/infinite_scale.cpp`. El agente de core lo ha mapeado a `ether_stress batch 1000 24`, que usa cinco lotes de InfiniteSession y comprueba correlaciones, snapshot y RNG. Se conserva así la carga de corrección/recursos; esta correspondencia no atribuye al ejecutable actual el speedup scalar/batch del benchmark antiguo.

### PR22: PhysicsWorld recuperado y corregido

La recuperación selectiva `6c171ad` incorporó `PhysicsWorld` sin retroceder Ether. Después, `8947e3e` optimizó el reajuste de límites espaciales, `59916f0` mantuvo las referencias de los cuerpos al confirmar un paso, y `7bd4a52` amplió las comparaciones. La rama antigua no debe sustituir esas fuentes por sus versiones anteriores. Sus observadores/batching y puente Solar quedan superados por las sesiones Ether y Solar vigentes.

`docs/physics-renderer-upgrade-plan.md` es el único archivo de esa rama completamente ausente. Es un plan histórico que incluso apunta a `native/tests/physics_world_smoke.cpp`, ruta que la propia rama dejó atrás. Se conserva su procedencia en el inventario y no se importa como descripción actual. Las limitaciones todavía relevantes de la física deben documentarse con el estado final del código.

## Áreas antiguas que ya están presentes

| Área | Evidencia en la referencia | Disposición |
|---|---|---|
| Lenguaje `.mx` y XASM reducido, PR4 | Head `d68a6d76` ancestro; `native/include/mundo_x/mxlang.hpp`, `xasm.hpp`, tests `mxlang_compiler` y `mxlang_cli_contract`, ejemplos `.mx`, comandos `run-mx` y distribución Windows en CMake/workflow. | Conservar integrados; no reabrir PR4. |
| Fases generales y transferencia, PR5 | Head `a6c403ac` ancestro; `adaptive_phase_core.hpp`, ejecutable `mundo_x_phase`, `tests/test_phase_core.cpp`, `tests/test_state_transfer.cpp` y ejemplos `.qx`. El paquete Windows incluye el ejecutable y ejemplos. | Conservar el contrato de sus runners finitos junto al runner infinito, sin reemplazarlos. |
| Algoritmos y benchmarks cuánticos | CMake contiene `test_quantum_algorithms`, `core_1000_benchmark`, `infinite_scale`, `ether_stress` y suites de núcleo. `quantum-algorithms.yml` valida BV 512, DJ 256, H–T–H y direcciones Ether de 1001 dígitos. | Mantener fuentes y gates actuales; añadir únicamente las cargas útiles pendientes de PR20/21. |
| Quantum Core Python, PR2 | Squash `4744607a`; patch equivalente por `git cherry`; blobs idénticos en `src/quantum_core.py`, experimento de interferencia y test Python. | Incluido, sin nuevo merge. |
| Transformer/Bridge Python, PR1 | Squash `4cc75ef2`; blobs idénticos en `src/transformer_x.py`, `src/bridge_x.py`, `experiments/train_file_v0_1.py` y `requirements.txt`. | Incluido como módulo existente; no inventar una conexión de runtime con Ether. |
| XLab/MicroLab y experimentos Android | Heads MicroLab, Android unificado, VM y Quantum Echo ancestros; CMake incluye `basis_vm`, `quantum_echo`, `sparse_observable`. Las ramas delivery sólo agregan binarios históricos. | Conservar fuentes más recientes; auditar los tests actuales y no volver a incorporar APK antiguos. |
| PC x86/x64 | PR11, PR17 y PR18 ancestros; implementación ENTER y oráculo recuperados; fuentes y test actuales preservan PC/graphics como una sesión. | Completar la regresión Android ENTER de PR19. |
| Diagnóstico Windows | `native/tests/reduced_optimization.cpp` idéntico a la rama diagnóstica. CMake limita `/Ob0` al test problemático de MSVC; el workflow actual ejecuta CTest, `.mx`, fases, VM y PC. | No reemplazar por la antigua matriz de diagnóstico con salida exitosa incondicional. |

Fuentes de cableado: [CMakeLists.txt](https://github.com/guadalupejuan001-code/mundo-X/blob/52150dfa928c088a32e69ce32f53c1e5a3b558c3/CMakeLists.txt), [workflow cuántico](https://github.com/guadalupejuan001-code/mundo-X/blob/52150dfa928c088a32e69ce32f53c1e5a3b558c3/.github/workflows/quantum-algorithms.yml), [workflow Windows](https://github.com/guadalupejuan001-code/mundo-X/blob/52150dfa928c088a32e69ce32f53c1e5a3b558c3/.github/workflows/windows-x64.yml), [lenguaje .mx](https://github.com/guadalupejuan001-code/mundo-X/blob/52150dfa928c088a32e69ce32f53c1e5a3b558c3/docs/mx_language_v0_1.md), [fases](https://github.com/guadalupejuan001-code/mundo-X/blob/52150dfa928c088a32e69ce32f53c1e5a3b558c3/docs/phase-engine.md) y [transferencia](https://github.com/guadalupejuan001-code/mundo-X/blob/52150dfa928c088a32e69ce32f53c1e5a3b558c3/docs/state-transfer.md).

## Matriz completa de las 35 ramas

`Fuera/detrás` cuenta commits del head que no son ancestros de la referencia / commits de la referencia ausentes en la rama. No mide funcionalidad faltante. Los SHAs completos, los 70 commits individuales y la equivalencia de cada archivo están en el JSON.

| Rama | Head | Fuera/detrás | PR | Clasificación |
|---|---|---:|---|---|
| `build/android-apk-v0.1` | [`7b674f52`](https://github.com/guadalupejuan001-code/mundo-X/commit/7b674f5261bc01f007c004eb1aef5b67b9f23ab4) | 2/339 | [3](https://github.com/guadalupejuan001-code/mundo-X/pull/3) | Disparador antiguo |
| `codex/basis-vm-android` | [`58dd035a`](https://github.com/guadalupejuan001-code/mundo-X/commit/58dd035a6f16e66c52bf313682065c2bb7630025) | 0/198 | — | Incluida por ascendencia |
| `codex/core-1000-qubits-20261019` | [`ef463624`](https://github.com/guadalupejuan001-code/mundo-X/commit/ef4636240af73351a18710e988346791fbdc3b58) | 0/152 | [15](https://github.com/guadalupejuan001-code/mundo-X/pull/15) | Incluida por ascendencia |
| `codex/cosmic-android-unified-20261019` | [`339fb9bd`](https://github.com/guadalupejuan001-code/mundo-X/commit/339fb9bd7bfb4e6e950a23f30719a98261914d98) | 0/155 | [13](https://github.com/guadalupejuan001-code/mundo-X/pull/13) | Incluida por ascendencia |
| `codex/cosmic-engine-4k-20261019` | [`403bdd73`](https://github.com/guadalupejuan001-code/mundo-X/commit/403bdd7311e6d6b37e849fbe2c85375a9a2cd2ac) | 0/153 | — | Incluida por ascendencia |
| `codex/cosmic-engine-8k-20261019` | [`9fd8fc78`](https://github.com/guadalupejuan001-code/mundo-X/commit/9fd8fc7865cfee6f91430ad4726d6ed841f2b3f6) | 0/150 | [14](https://github.com/guadalupejuan001-code/mundo-X/pull/14) | Incluida por ascendencia |
| `codex/cosmic-solar-4k-atmospheres-20261019` | [`b6a324f7`](https://github.com/guadalupejuan001-code/mundo-X/commit/b6a324f7d9a13709771f4589c8c082883607b788) | 0/146 | — | Incluida por ascendencia |
| `codex/cosmic-solar-explorer` | [`1deb00b8`](https://github.com/guadalupejuan001-code/mundo-X/commit/1deb00b8ae618940cbc6b1195059eab37f923b3f) | 0/157 | — | Incluida por ascendencia |
| `codex/cosmic-x-engine-20261019` | [`d701f5c0`](https://github.com/guadalupejuan001-code/mundo-X/commit/d701f5c052363e8880eba4a614b54cc6fdf8fbc0) | 0/162 | [12](https://github.com/guadalupejuan001-code/mundo-X/pull/12) | Incluida por ascendencia |
| `codex/ether-audit-stress-20261020` | [`aab09303`](https://github.com/guadalupejuan001-code/mundo-X/commit/aab0930378796ebfd42f9629e2575108ca94fcff) | 0/42 | [25](https://github.com/guadalupejuan001-code/mundo-X/pull/25) | Incluida por ascendencia |
| `codex/ether-core-v0-1-20261019` | [`e49125dc`](https://github.com/guadalupejuan001-code/mundo-X/commit/e49125dcd1b6c784d005bfd41c8fe2b8e318281a) | 0/60 | [23](https://github.com/guadalupejuan001-code/mundo-X/pull/23) | Incluida por ascendencia |
| `codex/ether-fusion-pc-check-20261020` | [`f2528050`](https://github.com/guadalupejuan001-code/mundo-X/commit/f2528050d86a3c251e763f918062cf7138a3bd2a) | 1/56 | — | Disparador antiguo |
| `codex/ether-fusion-session-20261020` | [`d5937f08`](https://github.com/guadalupejuan001-code/mundo-X/commit/d5937f08cdea67dcdfa3259a09dd2bd221f9f325) | 0/55 | [24](https://github.com/guadalupejuan001-code/mundo-X/pull/24) | Incluida por ascendencia |
| `codex/factorized-exact-structure-20261018` | [`462a686b`](https://github.com/guadalupejuan001-code/mundo-X/commit/462a686be00fb07d7d3fa8347cd086c9ef71d10b) | 0/178 | [6](https://github.com/guadalupejuan001-code/mundo-X/pull/6) | Incluida por ascendencia |
| `codex/graphics-spatial-20261018` | [`775f5a65`](https://github.com/guadalupejuan001-code/mundo-X/commit/775f5a65ac360c5c3a7f3e974d6f67bc39428051) | 0/173 | [8](https://github.com/guadalupejuan001-code/mundo-X/pull/8) | Incluida por ascendencia |
| `codex/infinite-8k-extreme-20261019` | [`d4de6d0b`](https://github.com/guadalupejuan001-code/mundo-X/commit/d4de6d0b86aca08450eb76d61cd9add2b4b7d097) | 15/118 | [20](https://github.com/guadalupejuan001-code/mundo-X/pull/20) | Rescate selectivo pendiente |
| `codex/infinite-core-rules-20261019` | [`2e6d367c`](https://github.com/guadalupejuan001-code/mundo-X/commit/2e6d367ca2cae86aa3d92892c000c29cc6d5113d) | 0/119 | [17](https://github.com/guadalupejuan001-code/mundo-X/pull/17) | Incluida por ascendencia |
| `codex/integrate-cosmic-projects-20261019` | [`849c7001`](https://github.com/guadalupejuan001-code/mundo-X/commit/849c7001056c7b5838e94604a77a51ab9e6a5f62) | 0/129 | [16](https://github.com/guadalupejuan001-code/mundo-X/pull/16) | Incluida por ascendencia |
| `codex/motor-x-visual-20261018` | [`eca35f15`](https://github.com/guadalupejuan001-code/mundo-X/commit/eca35f159320859e5ce873d59adeed982d6e0c30) | 0/172 | [9](https://github.com/guadalupejuan001-code/mundo-X/pull/9) | Incluida por ascendencia |
| `codex/mundo-x-graphics-android-20261019` | [`141937a0`](https://github.com/guadalupejuan001-code/mundo-X/commit/141937a068124177e32c212fc2942a3c10da12b1) | 0/176 | [10](https://github.com/guadalupejuan001-code/mundo-X/pull/10) | Incluida por ascendencia |
| `codex/own-pc-x86-20261019` | [`155f954a`](https://github.com/guadalupejuan001-code/mundo-X/commit/155f954a79027a1cb3f2b9c1a43e5da8661b3303) | 0/171 | [11](https://github.com/guadalupejuan001-code/mundo-X/pull/11) | Incluida por ascendencia |
| `codex/pc-enter-iso-fix-20261019` | [`57a8b339`](https://github.com/guadalupejuan001-code/mundo-X/commit/57a8b339f9580ff7a41d3da25ca4636aef5e2388) | 1/118 | [19](https://github.com/guadalupejuan001-code/mundo-X/pull/19) | Regresión pendiente |
| `codex/pc-graphics-iso-fix-20261019` | [`acc43006`](https://github.com/guadalupejuan001-code/mundo-X/commit/acc430068cd7414c06f050e6485714d566805e55) | 0/127 | [18](https://github.com/guadalupejuan001-code/mundo-X/pull/18) | Incluida por ascendencia |
| `codex/physics-foundation-20261019` | [`c7e78948`](https://github.com/guadalupejuan001-code/mundo-X/commit/c7e78948ea95080d0603608696da221f02bf4584) | 27/118 | [22](https://github.com/guadalupejuan001-code/mundo-X/pull/22) | Rescate ya incluido; resto superado |
| `codex/quantum-echo-android` | [`9d095a81`](https://github.com/guadalupejuan001-code/mundo-X/commit/9d095a81b3820f4c50fc48fecd9c7ff08bd22b40) | 0/196 | — | Incluida por ascendencia |
| `codex/rule-render-physics-20261019` | [`fd0bd14d`](https://github.com/guadalupejuan001-code/mundo-X/commit/fd0bd14dc5a9709703ced7fb61fe031a1c266b75) | 11/118 | [21](https://github.com/guadalupejuan001-code/mundo-X/pull/21) | Rescate selectivo pendiente |
| `codex/visual-microlab-20261018` | [`bf5f3c66`](https://github.com/guadalupejuan001-code/mundo-X/commit/bf5f3c6687bea8e23f87609ec72634422d1e288a) | 0/176 | [7](https://github.com/guadalupejuan001-code/mundo-X/pull/7) | Incluida por ascendencia |
| `codex/windows-reference-diagnostics-20261018` | [`5ff550d5`](https://github.com/guadalupejuan001-code/mundo-X/commit/5ff550d5cb664f4cb218ec96aa401c1b227686cf) | 3/199 | — | Diagnóstico incluido; CI antigua |
| `codex/xasm-reduced-t` | [`d68a6d76`](https://github.com/guadalupejuan001-code/mundo-X/commit/d68a6d76d7045d77973296345810cc2633b47b30) | 0/198 | [4](https://github.com/guadalupejuan001-code/mundo-X/pull/4) | Incluida por ascendencia |
| `delivery/xlab-0.3.1` | [`dd56120f`](https://github.com/guadalupejuan001-code/mundo-X/commit/dd56120f37bf80035a5f4b7a79f2b953aaad2cf5) | 2/183 | — | Entrega binaria histórica |
| `delivery/xlab-0.3.2` | [`6b206566`](https://github.com/guadalupejuan001-code/mundo-X/commit/6b206566022afa8b7b20b9e6dd3153ce8dad9182) | 1/178 | — | Entrega binaria histórica |
| `experiment/quantum-core-v0.1` | [`4f8d212c`](https://github.com/guadalupejuan001-code/mundo-X/commit/4f8d212cba7f1fce4ebd1a1e2a8cca3ce6044bca) | 1/352 | [2](https://github.com/guadalupejuan001-code/mundo-X/pull/2) | Incluida por squash y contenido |
| `experiment/transformer-x-v0.1` | [`71433b2a`](https://github.com/guadalupejuan001-code/mundo-X/commit/71433b2a466b77e73761de86b670207d639c29ff) | 6/353 | [1](https://github.com/guadalupejuan001-code/mundo-X/pull/1) | Incluida por squash y contenido |
| `main` | [`7b028db2`](https://github.com/guadalupejuan001-code/mundo-X/commit/7b028db2344912b2ab86a0affa2a1fe56180922b) | 0/54 | — | Incluida por ascendencia |
| `phase-performance-trial` | [`a6c403ac`](https://github.com/guadalupejuan001-code/mundo-X/commit/a6c403ac4d403dbc5e2e26fb5d9883ac9ab341f1) | 0/198 | [5](https://github.com/guadalupejuan001-code/mundo-X/pull/5) | Incluida por ascendencia |

Las ramas de disparo (`build/android-apk-v0.1`, `codex/ether-fusion-pc-check-20261020`) no contienen mejoras de runtime pendientes. Las ramas `delivery/xlab-0.3.1` y `delivery/xlab-0.3.2` sólo contienen APK/ZIP/checksums/firma; sus SHAs permiten conservar la trazabilidad de entregas anteriores sin añadir binarios antiguos a main. Ninguna rama se elimina como efecto de esta auditoría.

## Matriz completa de los 25 pull requests

El estado es el devuelto por GitHub al capturar. `merged_at=null` no significa siempre código ausente: PR9 fue absorbido en una integración y su head exacto es ancestro; PR1/2 usan squash.

| PR | Head | Estado capturado | Integración en 52150df | Acción |
|---|---|---|---|---|
| [#1](https://github.com/guadalupejuan001-code/mundo-X/pull/1) Transformer X v0.1: Bridge X + real-file training | [`71433b2a`](https://github.com/guadalupejuan001-code/mundo-X/commit/71433b2a466b77e73761de86b670207d639c29ff) | Cerrado | Squash/contenido | Mantener cerrado |
| [#2](https://github.com/guadalupejuan001-code/mundo-X/pull/2) Quantum Core X v0.1: superposición, fase, interferencia y entrelazamiento | [`4f8d212c`](https://github.com/guadalupejuan001-code/mundo-X/commit/4f8d212cba7f1fce4ebd1a1e2a8cca3ce6044bca) | Cerrado | Squash/contenido | Mantener cerrado |
| [#3](https://github.com/guadalupejuan001-code/mundo-X/pull/3) Build Mundo X Lab Android APK v0.1 | [`7b674f52`](https://github.com/guadalupejuan001-code/mundo-X/commit/7b674f5261bc01f007c004eb1aef5b67b9f23ab4) | Cerrado | Trigger obsoleto | Mantener cerrado; sin integración |
| [#4](https://github.com/guadalupejuan001-code/mundo-X/pull/4) Añade lenguaje .mx con circuitos reutilizables y XASM reducido | [`d68a6d76`](https://github.com/guadalupejuan001-code/mundo-X/commit/d68a6d76d7045d77973296345810cc2633b47b30) | Cerrado · borrador | Ascendencia exacta | Mantener cerrado |
| [#5](https://github.com/guadalupejuan001-code/mundo-X/pull/5) Add general phases and transfer product states without replaying gate history | [`a6c403ac`](https://github.com/guadalupejuan001-code/mundo-X/commit/a6c403ac4d403dbc5e2e26fb5d9883ac9ab341f1) | Cerrado · borrador | Ascendencia exacta | Mantener cerrado |
| [#6](https://github.com/guadalupejuan001-code/mundo-X/pull/6) Evitar fusiones innecesarias y estabilizar la escala de FactorizedCore | [`462a686b`](https://github.com/guadalupejuan001-code/mundo-X/commit/462a686be00fb07d7d3fa8347cd086c9ef71d10b) | Cerrado · borrador | Ascendencia exacta | Mantener cerrado |
| [#7](https://github.com/guadalupejuan001-code/mundo-X/pull/7) MicroLab: partículas, colonias y control visual del núcleo cuántico | [`bf5f3c66`](https://github.com/guadalupejuan001-code/mundo-X/commit/bf5f3c6687bea8e23f87609ec72634422d1e288a) | Cerrado · borrador | Ascendencia exacta | Mantener cerrado |
| [#8](https://github.com/guadalupejuan001-code/mundo-X/pull/8) Optimiza los gráficos procedurales con malla indexada y descarte por regiones | [`775f5a65`](https://github.com/guadalupejuan001-code/mundo-X/commit/775f5a65ac360c5c3a7f3e974d6f67bc39428051) | Cerrado · borrador | Ascendencia exacta | Mantener cerrado |
| [#9](https://github.com/guadalupejuan001-code/mundo-X/pull/9) Integra escenas visuales por reglas en el runtime de Mundo X | [`eca35f15`](https://github.com/guadalupejuan001-code/mundo-X/commit/eca35f159320859e5ce873d59adeed982d6e0c30) | Cerrado · borrador | Ascendencia exacta | Mantener cerrado |
| [#10](https://github.com/guadalupejuan001-code/mundo-X/pull/10) Mundo X Graphics 0.1.1: APK Android con núcleo compartido y sesión persistente | [`141937a0`](https://github.com/guadalupejuan001-code/mundo-X/commit/141937a068124177e32c212fc2942a3c10da12b1) | Cerrado | Ascendencia exacta | Mantener cerrado |
| [#11](https://github.com/guadalupejuan001-code/mundo-X/pull/11) COSMIC X PC: motor x86/x64 propio y RAM por demanda en Android | [`155f954a`](https://github.com/guadalupejuan001-code/mundo-X/commit/155f954a79027a1cb3f2b9c1a43e5da8661b3303) | Cerrado · borrador | Ascendencia exacta | Mantener cerrado |
| [#12](https://github.com/guadalupejuan001-code/mundo-X/pull/12) COSMIC X Engine: integrar núcleo propio, motor gráfico y proyectos portables | [`d701f5c0`](https://github.com/guadalupejuan001-code/mundo-X/commit/d701f5c052363e8880eba4a614b54cc6fdf8fbc0) | Cerrado | Ascendencia exacta | Mantener cerrado |
| [#13](https://github.com/guadalupejuan001-code/mundo-X/pull/13) COSMIC X Engine 0.3.0: app Android nueva con los 14 experimentos propios | [`339fb9bd`](https://github.com/guadalupejuan001-code/mundo-X/commit/339fb9bd7bfb4e6e950a23f30719a98261914d98) | Cerrado | Ascendencia exacta | Mantener cerrado |
| [#14](https://github.com/guadalupejuan001-code/mundo-X/pull/14) COSMIC X Engine: salida 8K y optimización exacta del núcleo | [`9fd8fc78`](https://github.com/guadalupejuan001-code/mundo-X/commit/9fd8fc7865cfee6f91430ad4726d6ed841f2b3f6) | Cerrado · borrador | Ascendencia exacta | Mantener cerrado |
| [#15](https://github.com/guadalupejuan001-code/mundo-X/pull/15) COSMIC X: núcleo Compact de 1.000 qubits y ampliación de proyectos | [`ef463624`](https://github.com/guadalupejuan001-code/mundo-X/commit/ef4636240af73351a18710e988346791fbdc3b58) | Cerrado | Ascendencia exacta | Mantener cerrado |
| [#16](https://github.com/guadalupejuan001-code/mundo-X/pull/16) Integra los proyectos COSMIC y actualiza el README para 1.000 qubits | [`849c7001`](https://github.com/guadalupejuan001-code/mundo-X/commit/849c7001056c7b5838e94604a77a51ab9e6a5f62) | Cerrado | Ascendencia exacta | Mantener cerrado |
| [#17](https://github.com/guadalupejuan001-code/mundo-X/pull/17) Integra InfiniteCore con PC x86/x64, estado persistente y motor gráfico | [`2e6d367c`](https://github.com/guadalupejuan001-code/mundo-X/commit/2e6d367ca2cae86aa3d92892c000c29cc6d5113d) | Cerrado | Ascendencia exacta | Mantener cerrado |
| [#18](https://github.com/guadalupejuan001-code/mundo-X/pull/18) PC 0.3.0: corrige SHRD de ISO y conecta vídeo real hasta 8K | [`acc43006`](https://github.com/guadalupejuan001-code/mundo-X/commit/acc430068cd7414c06f050e6485714d566805e55) | Cerrado | Ascendencia exacta | Mantener cerrado |
| [#19](https://github.com/guadalupejuan001-code/mundo-X/pull/19) PC 0.4.1: corrige ENTER C8 que detiene el arranque ISO | [`57a8b339`](https://github.com/guadalupejuan001-code/mundo-X/commit/57a8b339f9580ff7a41d3da25ca4636aef5e2388) | Abierto | Selectiva; ver detalle | Rescatar regresión Android; cerrar después |
| [#20](https://github.com/guadalupejuan001-code/mundo-X/pull/20) Extreme InfiniteCore + exact 8K work culling | [`d4de6d0b`](https://github.com/guadalupejuan001-code/mundo-X/commit/d4de6d0b86aca08450eb76d61cd9add2b4b7d097) | Abierto · borrador | Selectiva; ver detalle | Rescatar probe/contratos; cerrar después |
| [#21](https://github.com/guadalupejuan001-code/mundo-X/pull/21) Actualiza InfiniteCore, reuso 8K y base física por reglas | [`fd0bd14d`](https://github.com/guadalupejuan001-code/mundo-X/commit/fd0bd14dc5a9709703ced7fb61fe031a1c266b75) | Abierto · borrador | Selectiva; ver detalle | Recuperar 3D/reuso; cerrar después |
| [#22](https://github.com/guadalupejuan001-code/mundo-X/pull/22) Solar 8K + InfiniteCore por reglas + base física verificada | [`c7e78948`](https://github.com/guadalupejuan001-code/mundo-X/commit/c7e78948ea95080d0603608696da221f02bf4584) | Abierto · borrador | Selectiva; ver detalle | Confirmar absorción actual; cerrar después |
| [#23](https://github.com/guadalupejuan001-code/mundo-X/pull/23) COSMIC X: EtherCore + Visual Ether + Solar 8K + PC + pruebas cuánticas | [`e49125dc`](https://github.com/guadalupejuan001-code/mundo-X/commit/e49125dcd1b6c784d005bfd41c8fe2b8e318281a) | Cerrado · borrador | Ascendencia exacta | Mantener cerrado |
| [#24](https://github.com/guadalupejuan001-code/mundo-X/pull/24) Ether: fusionar reglas globales y cambios locales en una sola sesión | [`d5937f08`](https://github.com/guadalupejuan001-code/mundo-X/commit/d5937f08cdea67dcdfa3259a09dd2bd221f9f325) | Cerrado | Ascendencia exacta | Mantener cerrado |
| [#25](https://github.com/guadalupejuan001-code/mundo-X/pull/25) Ether Core: auditoría reproducible, reutilización exacta y correcciones gráficas | [`aab09303`](https://github.com/guadalupejuan001-code/mundo-X/commit/aab0930378796ebfd42f9629e2575108ca94fcff) | Abierto · borrador | Ascendencia exacta | Actualizar y validar como consolidación |

## Condiciones de cierre y evidencia de la consolidación

1. Incorporar selectivamente los aportes pendientes y mantener en las descripciones finales los commits de procedencia listados. No resolver el desfase de las ramas antiguas aceptando sus versiones completas de core, renderer, instrumentación o pins.
2. Vincular las verificaciones nuevas al commit consolidado y al entorno real que las ejecuta. Este inventario no convierte resultados históricos, una compilación o una simulación de escritorio en una aprobación Android. Los fallos pendientes de exactitud Solar y XLab deben conservar su estado real en la entrega.
3. Publicar la rama consolidada y utilizar PR25, cuyo head ya es ancestro, si sigue siendo la vía adecuada al momento de publicar. Reconsultar su estado antes de mutarlo.
4. Cerrar #19–22 únicamente después de que la consolidación publicada preserve sus aportes útiles o documente de forma expresa las partes superadas. Las ramas y SHAs históricos quedan registrados para recuperación; la eliminación remota de ramas no forma parte del inventario.

### Estado al crear este documento

El rescate del probe extremo y RulePhysics 3D está asignado al agente de core. La reutilización Android está asignada a root. La regresión Android ENTER/LEAVE ya se recuperó selectivamente en el árbol de trabajo: conserva los contratos Ether, añade la procedencia `57a8b339` en la evidencia y exige que ENTER consuma exactamente una instrucción. `bash -n` y el parseo de los dos heredocs Python pasan. La generación real de recursos API 35 y compilación Java 17 de 14 fuentes PC/Graphics, incluida la instrumentación, produce 47 clases sin errores; [pc-java-compile.json](../results/repository-unification-2026-10-21/pc-java-compile.json) fija los hashes y comandos. La ejecución Android y validación del APK completo siguen pendientes. Los hashes de los dos archivos editados están en el JSON. Son trabajos posteriores a `52150df`; no se ha declarado ningún PR cerrado ni publicación completada desde este análisis.
