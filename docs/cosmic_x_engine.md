# COSMIC X Engine · integración del código propio

**COSMIC X Engine es el título del conjunto.** Dentro viene la computación
cuántica de Mundo X: el núcleo cuántico y el motor gráfico cuántico integrado,
según la estructura indicada por X el 19 de octubre de 2026.

El título conserva los nombres de las representaciones matemáticas y las
rutas históricas del código. El repositorio continúa siendo `mundo-X`.

## Estructura integrada

| Parte | Implementación y conexión |
| --- | --- |
| Núcleo cuántico | CompactCore, Reduced Exact T, FactorizedCore y AdaptivePhaseCore; se conservan sus API, herramientas y límites. |
| Motor gráfico cuántico | El observador de Mundo X Graphics consulta el estado y reconstruye el mundo por reglas e IDs. |
| Sesión compartida | `engine_session.hpp` y `graphics_core_bridge.cpp` poseen estado, historial, revisión, restauración y caché. Android y escritorio compilan los mismos archivos. |
| Proyecto portable | `.cosmic` v1 reúne el programa del núcleo, los observables y las reglas y ediciones del mundo. |
| Lenguaje y escenas | `.mx` compila a XASM; VisualScene conserva las instrucciones de escena y la exportación hacia XLab. |
| Laboratorios | XLab y MicroLab conservan sus aplicaciones y funciones dentro del repositorio. |

La sesión gráfica v1 conecta CompactCore con observables Pauli y cajas opacas.
La referencia densa comprueba la corrección. Los identificadores de backend
Reduced y Factorized se reconocen como no disponibles en esta ABI gráfica;
no sustituyen silenciosamente su representación por una densa. Las fases
generales se ejecutan mediante su API y `mundo_x_phase`.

## Procedencia

Se integraron las seis ramas pendientes del propio repositorio conservando
sus commits como ancestros:

| PR | Head original | Aporte |
| --- | --- | --- |
| [#4](https://github.com/guadalupejuan001-code/mundo-X/pull/4) | `d68a6d76d7045d77973296345810cc2633b47b30` | Lenguaje `.mx`, circuitos reutilizables, XASM y backend reducido. |
| [#5](https://github.com/guadalupejuan001-code/mundo-X/pull/5) | `a6c403ac4d403dbc5e2e26fb5d9883ac9ab341f1` | Fases generales, representación adaptativa y transferencia del estado producto. |
| [#6](https://github.com/guadalupejuan001-code/mundo-X/pull/6) | `462a686be00fb07d7d3fa8347cd086c9ef71d10b` | Estructura, almacenamiento y estabilidad de Factorized. |
| [#7](https://github.com/guadalupejuan001-code/mundo-X/pull/7) | `bf5f3c6687bea8e23f87609ec72634422d1e288a` | MicroLab y controles de cálculo sobre Factorized. |
| [#8](https://github.com/guadalupejuan001-code/mundo-X/pull/8) | `775f5a65ac360c5c3a7f3e974d6f67bc39428051` | Dibujo indexado, regiones e IDs estables en XLab. |
| [#9](https://github.com/guadalupejuan001-code/mundo-X/pull/9) | `eca35f159320859e5ce873d59adeed982d6e0c30` | VisualScene y programación de cálculo y escenas. |

Graphics de escritorio procede del paquete original de Mundo X Graphics
del 18 de octubre. Los adaptadores nuevos se escriben sobre ese código y los
núcleos del mismo repositorio. No se incorpora la implementación de otro motor.
Se conservan las herramientas y dependencias de compilación y representación
que ya utilizaba el proyecto.

CompactCore permanece idéntico al punto de partida de Graphics. DenseCore
incorpora las funciones de la rama propia de fases y conserva `Real` y la
referencia FP64 del `main` actual. Los archivos `PROVENANCE.json` registran
los hashes actuales y los históricos. La integración conserva medición Z,
semillas de 64 bits, contadores y las optimizaciones de Reduced.

La idea, creación y dirección del proyecto corresponden a X. El desarrollo
incluye asistencia de IA. Este registro identifica la procedencia del código
y las decisiones de integración; las evaluaciones de comportamiento se
sustentan en las pruebas reproducibles del repositorio.

## Un mismo estado entre cálculo e imagen

El host posee una sola sesión nativa. Al aplicar una puerta se actualizan
estado, historial y revisión juntos. El observador obtiene los valores
esperados de sus operadores Pauli sin colapsar el estado. La cámara cambia
el punto de vista y reutiliza la información cuántica ya consultada.

Guardar obtiene una instantánea coherente. Abrir valida el formato entero,
reproduce el historial en una sesión candidata y sólo entonces sustituye
la activa. Un contador interno de restauraciones invalida las cachés aunque
dos proyectos tengan el mismo número de revisión persistente.

El documento conserva los operadores originales y las ediciones que
temporalmente quedan fuera de la población visible, además de los canales
que exceden el número de objetos.

## Dos recorridos de escena con contratos definidos

- **Graphics y `.cosmic`:** observación de Pauli vinculada a la sesión activa,
  importación/exportación entre Android y escritorio, cámara e interacción por ID.
- **XASM, VisualScene y XLab:** exportación de la escena programada. `scene_probz`
  toma una instantánea de probabilidades Z de CompactCore; requiere ese backend
  y conserva su mapeo exacto `0 / 0,5 / 1`. La escena se exporta después de
  terminar correctamente el programa y aprobar sus aserciones.

Los formatos tienen funciones distintas. Esta versión no convierte
automáticamente cualquier programa `.mx` o XASM en un proyecto `.cosmic`.
Transformer X y Bridge X mantienen sus módulos de aprendizaje y no se anuncian
como parte de la sesión gráfica nativa.

## Reproducir las comprobaciones

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
python3 graphics-desktop/validate_contract.py
python3 graphics-desktop/validate_projects.py
python3 graphics-desktop/validate.py
```

Las instrucciones de Android están en
[graphics-android/README.md](../graphics-android/README.md). Los workflows
registran la revisión compilada, pruebas, hashes y controlador gráfico.
La fidelidad portable exige el mismo estado y las mismas reglas; la igualdad
exacta de píxeles se comprueba dentro del mismo controlador. Los resultados
de emulador no se atribuyen a un teléfono físico.
