# Registro de 1.000 qubits en COSMIC X Engine

La revisión 0.3.1 configura **1.000 qubits** en los nuevos proyectos de Android
y escritorio. El registro tiene índices de `q0` a `q999`; las puertas,
observables, historial y archivos `.cosmic` conservan ese ancho completo.

El núcleo ejecutado por la sesión gráfica es el `CompactCore` original de
Mundo X. La representación ya admitía registros amplios; esta actualización
conecta el perfil de 1.000 a las interfaces que anteriormente iniciaban en 20
y limitaban los proyectos a 256. Incorpora las optimizaciones de operaciones
empaquetadas desarrolladas en la rama 8K del mismo repositorio, sin cambiar
la ABI de sesión ni seleccionar otro motor.

## Uso en Android

Una instalación sin sesión previa abre un mundo de 1.000 qubits. **Nuevo**
permite crear proyectos de 1 a 1.000. Una sesión guardada conserva su dimensión
al abrirse; **Ampliar a 1000** crea el registro mayor a partir de su estado.
La selección de qubit permite operar hasta `q999`.

La ampliación reproduce el historial en un registro inicialmente en cero.
Todas las operaciones anteriores siguen afectando a sus índices originales.
Cada nuevo qubit empieza en `|0>`, y a cada observable original se le añaden
identidades `I`, de modo que conserva su valor y su papel en el motor gráfico.
También se conservan revisión, semilla, objetos, ediciones, cámara y tiempo.
Los ejemplos Bell de 2 qubits y GHZ de 3 mantienen su finalidad pedagógica.

Los proyectos de 1.000 qubits requieren esta versión del lector. Los lectores
anteriores que admitían como máximo 256 los rechazan; los archivos anteriores
siguen siendo válidos en el lector nuevo.

## Uso en escritorio y XASM

```bash
# Crear y guardar un proyecto nuevo de 1.000 qubits.
python graphics-desktop/demo.py --no-render --export-project mundo-1000.cosmic

# Ampliar un archivo existente, operar q999 y guardar otra copia.
python graphics-desktop/demo.py --project mundo-20.cosmic \
  --expand-qubits 1000 --gate X:999 --no-render \
  --export-project mundo-ampliado.cosmic

# Ejecutar el circuito entrelazado de 1.000 qubits en el núcleo compacto.
./build/mundo_x run examples/ghz1000.x

# Circuito separado de 60 qubits con puerta T en el núcleo reducido.
./build/mundo_x run examples/t60.x
```

`ProjectSession.expanded(1000)` devuelve una sesión nueva que posee su núcleo;
la sesión original sigue disponible. Rechaza reducciones, el mismo tamaño,
valores no enteros y cantidades superiores al perfil portable de 1.000.

XASM añade `expect PAULI` y `assert expectation PAULI VALOR`. Estas operaciones
calculan expectativas sobre la representación activa. `assert norm` usa la
norma de componentes en el backend reducido; `assert fidelity` mantiene la
referencia densa y se limita explícitamente a 12 qubits.

## Qué admite cada núcleo

| Ruta | Alcance de esta revisión |
|---|---|
| Sesión gráfica / `CompactCore` | 1.000 qubits; H, S, X, Y, Z, CX y CZ; observables Pauli. |
| `CompactCore` nativo y XASM | Registro de 1.000, entrelazamiento Clifford y medición Z reproducible. |
| `ReducedExactTRepresentation` | Admite 1 a 1.000 qubits mediante máscaras Pauli multi-palabra; XASM conserva 128 T y 65.536 componentes como presupuestos. |
| `FactorizedCore` | Conserva sus límites de bloque y presupuesto; no se convierte un bloque denso de 20 en uno de 1.000. |

La prueba GHZ de 1.000 entrelaza el registro completo. La capacidad exacta del
núcleo compacto se refiere a estados estabilizadores. La ampliación no afirma
que cualquier circuito no Clifford de 1.000 pueda ejecutarse con el mismo
consumo, ni que incrementar qubits produzca por sí solo más FPS.

## Memoria y observación

El tableau almacena dos matrices de bits y las fases de sus generadores:

`bytes = 2 × n × ceil(n/64) × 8 + n`.

| Registro | Buffers del tableau |
|---|---:|
| 8 qubits | 136 bytes |
| 20 qubits | 340 bytes |
| 60 qubits | 1.020 bytes |
| 1.000 qubits | 257.000 bytes |

Esta tabla no representa la RAM total de Android. El observador, el historial,
las cadenas Pauli, Java, JNI y el renderizador usan memoria adicional. El
programa gráfico inicial incluye 3.000 observables: los 1.000 generadores
iniciales y las consultas locales X/Z de cada qubit. Los proyectos ampliados
conservan sus observables previos extendidos; no se sustituye su programa
visual por otro de 3.000 canales.

El benchmark compara 20, 60 y 1.000 con el mismo procedimiento y registra por
separado estado, observador, historial y tiempos. Sus números corresponden a
la máquina que lo ejecuta; el rendimiento del Samsung A16 exige medición en
ese dispositivo.

## Verificación reproducible

La ejecución local del 19 de octubre aprobó **46/46 casos CTest**. El contrato
específico de 1.000 realizó 30.721 comprobaciones y 11.304 comparaciones con
su referencia pequeña, con error máximo de aproximadamente `5,56e-17` frente
a la tolerancia `1e-10`. La regresión de 60 realizó 91.243 comprobaciones,
incluida la ruta con T; las ampliaciones y las suites gráficas de escritorio
también pasaron. [Evidencia local y hashes](../results/core_1000_local_2026-10-19.json).

El [benchmark de 20/60/1.000](../results/core_1000_benchmark_2026-10-19.json)
conserva siete muestras por caso, calentamiento, definición de circuitos y
contabilidad de buffers. Los tiempos transcurridos muestran variación del
entorno; se conservan mínimos, medianas y extremos, y no se traducen a FPS.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/core_1000_benchmark
python graphics-desktop/check_core_profile.py --output core-1000-projects.json
```

Las pruebas específicas comprueban el estabilizador global X y las 999
correlaciones que fijan el GHZ de 1.000, signos con Y, ambas ramas de medición,
repetibilidad y rechazo de índices inválidos sin alterar el estado. Una
referencia densa pequeña verifica operaciones en sitios no contiguos de un
registro de 1.000; esa comparación usa solo los sitios activos del circuito
de referencia. La prueba GHZ completa se verifica por sus relaciones exactas.

También se comprueban la ABI, restauración de historial, 3.000 canales,
guardado/reapertura y ampliación desde proyectos de 2, 8, 20 y 60 qubits.
Las suites Android ejercitan JNI, los botones de ampliación y la conservación
de la imagen y el mundo. El workflow conserva resultados y la APK del commit
que se probó.

## Ruta universal de 1.000 qubits

La ruta reducida ya no usa una sola máscara de 64 bits. X/Z y los pivotes estabilizadores se almacenan en 16 palabras de 64 bits, suficientes para q0..q999. El contrato `reduced_1000` ejecuta T en q999, una fase general en q998, entrelazamiento CNOT entre q999 y q0 y la transferencia de `AdaptivePhaseCore` de 1.000 qubits. La campaña cuántica aislada aprobó este contrato junto con las regresiones existentes.

Esto elimina el antiguo límite estructural de 63 qubits; **no** convierte cualquier circuito universal de 1.000 qubits en una carga barata. La cantidad de componentes no-Clifford sigue limitada por presupuesto y puede crecer exponencialmente con el estado/circuito.
