# Ether: eliminar las búsquedas repetidas del renderizador

## Referencia y resultado

Esta revisión continúa la línea ya optimizada, incluidos compactación de reglas,
evaluación por intervalos, espejos, física, consultas Pauli y selección de un píxel.
La referencia recuperable es `d0c148dae0ef09159ad0b1e18f6cafaf6285a7aa`.
Rama de entrega: `codex/ether-render-optimization-20261020`.

Se comprobaron las 35 ramas remotas: no cambiaron. `main` sigue en
`7b028db2344912b2ab86a0affa2a1fe56180922b`; el PR #25 permanece abierto y en
borrador, con `aab0930378796ebfd42f9629e2575108ca94fcff`. La rama local reúne
las mejoras verificadas posteriores. No se publicó ni fusionó y no se generó
una APK en esta ronda. Solar sigue pausado.

Misma resolución, precisión, semilla, observables, ediciones, cámara y tiempo
antes/después. Render por software con Mesa llvmpipe, un procesador asignado.
Las cifras siguientes son medianas de las cargas definidas en el banco de pruebas:

| Carga | Antes, ms | Después, ms | Cociente |
| --- | ---: | ---: | ---: |
| 320×180, 1,024 objetos | 22,614 | 9,373 | 2,413× |
| 320×180, 1,048,576 objetos | 66,596 | 21,394 | 3,113× |
| 960×540, 1,048,576 objetos | 485,340 | 167,245 | 2,902× |
| 1920×1080, 1,048,576 objetos | 972,201 | 487,696 | 1,993× |
| Carga conjunta, 320×180 | 82,717 | 39,174 | 2,112× |

La carga conjunta reduce su tiempo **52.64 %**. Son tres
campañas pareadas; las tres mejoran. Ejecuta 4.096 cuerpos de PhysicsWorld,
16.384 partículas MicroLab, una sesión Ether con 256 direcciones activas y 512
observables, y un millón de cajas con cámara, tiempo, ediciones y espejos cambiantes.
Se fuerzan todos los cuadros: no se devuelve una imagen anterior. Se comparan
54 pares de cuadros y los estados nativos. El coste incluye verificaciones de
resultados en ambas versiones. PhysicsWorld todavía no dirige la geometría 3D.

Los grupos de render usan 12 vistas por resolución, excepto 1080p, que usa seis
para conservar el presupuesto por proceso. Por eso las filas no son una curva
controlada de escalado entre resoluciones. Cada fila sí compara exactamente la
misma carga antes y después. Se guardan tres muestras por vista y la primera
observación de cada vista; compilación/JIT se separan de las medianas sostenidas.
En 960×540, la mediana de la primera observación de vistas nuevas, excluidas las
dos primeras especializaciones, fue 554,490 → 183,025 ms. La mejora también
aparece cuando cambian las entradas. No son FPS de Samsung ni de una GPU física.

## Qué lo hacía lento

`common.glsl::editState` ejecutaba una búsqueda binaria en una textura de pares
`(id, estado)`. `objectData` la repetía para cada candidato geométrico de cada
rayo. Un mismo objeto puede ser candidato en muchos píxeles y reflejos. El
recorrido era proporcional a log₂(E) por consulta, siendo E la cantidad de
ediciones; su bucle admitía hasta 21 lecturas de textura.

El perfil instrumentado de una vista de 160×90, un millón de objetos lógicos y
32.768 ediciones contó **252.478 celdas recorridas**, **89.029 reconstrucciones
de objeto** y **1.333.151 lecturas de excepciones**. Con el nuevo índice son las
mismas celdas y reconstrucciones, pero **124.772 lecturas**. Se instrumentan las
llamadas lógicas a `texelFetch`; no son contadores físicos de instrucciones GPU.
Los contadores se ejecutan aparte de las medidas de velocidad.

También se confirmó otro coste: una sola excepción hace que el recorrido use
el límite vertical global reservado para objetos editados. Eso amplía la zona
que algunos rayos atraviesan. Sigue siendo una frontera del recorrido; esta
mejora no pretende haber eliminado todas las reconstrucciones ni los rebotes.

## Representación exacta que se incorporó

El mapa de ediciones del proyecto sigue siendo la fuente de verdad. De él se
deriva una textura privada `R32UI` que contiene:

1. Un directorio de bloques de 256 IDs. Un cero indica que el bloque completo
   conserva el estado por defecto.
2. Sólo para bloques con cambios no nulos, una página de 32 palabras. Cada
   palabra guarda ocho estados de cuatro bits; los valores válidos siguen
   siendo 0–7, incluido 7 para ocultar un objeto.
3. Filas de 256 palabras, para calcular la posición con desplazamientos y
   máscaras, sin divisiones en cada consulta.

Una consulta hace una lectura del directorio y, cuando corresponde, una de la
página: O(1) en el dominio actual de IDs. No se construyen mallas, alturas ni
copias del mundo por espejo. No se aproxima ningún estado y el hash, la
animación, la geometría, los materiales, los rayos y sus normales no cambiaron.
El archivo `eye.frag` sigue idéntico al de la referencia; cambia el decodificador
compartido de excepciones en `common.glsl`.

El índice depende sólo de las ediciones y el tamaño de la escena. Se reconstruye
al cambiar éstos y puede reutilizarse con cámara, tiempo y observables nuevos:
no contiene alturas ni resultados visuales que puedan quedar obsoletos. Los
archivos `.cosmic`, snapshots nativos y sesiones serializadas conservan su
formato. Se mantienen incluso las entradas explícitas de estado cero y el
contador lógico de ediciones, para preservar también el recorrido anterior.

La primera versión del empaquetador Python hacía demasiadas operaciones
individuales sobre NumPy en el caso denso. Se sustituyó por lotes de **4.096**
entradas. `bitwise_or.at` conserva todas las escrituras cuando varios IDs
comparten una palabra; una asignación indexada ordinaria perdería algunas.
Los casos pequeños conservan el recorrido directo.

## RAM y coste de preparación

| Caso de excepciones | Textura anterior | Textura nueva |
| --- | ---: | ---: |
| Ninguna | 8 B | 4 B |
| 64 juntas, dominio de un millón de objetos | 512 B | 17 KiB |
| 4.096 dispersas, dominio de 1.000.003 objetos | 32 KiB | 388 KiB |
| Todos los 1.048.576 IDs editados | 8 MiB | 528 KiB |

El índice intercambia una cantidad pequeña y acotada de memoria por consultas
rápidas en escenas dispersas. No se presenta como una reducción de memoria en
todos los casos. El máximo del formato, bajo el contrato actual de un millón de
IDs, es 540.672 bytes: 256×528 palabras de cuatro bytes. Las dimensiones se
validan contra la capacidad de textura del dispositivo.

Para empaquetar un millón de ediciones, la mediana CPU pasó de
**523,185 a 393,061 ms**. Con 64 ediciones pasó de 0,033 a 0,077 ms: existe un
pequeño coste adicional de preparación, muy inferior al ahorro de dibujo de
estas escenas. No se oculta esa contrapartida.

La asignación máxima rastreada durante el empaquetado denso bajó de
**75.386.076 a 1.278.460 bytes**, incluyendo el buffer resultante. Se midió con
`tracemalloc` fuera de los cronómetros. Son asignaciones Python/NumPy rastreadas;
excluyen el mapa preexistente, la memoria del controlador y asignadores no
rastreados. La técnica por lotes evita temporales proporcionales a toda la lista.

Los procesos de comparación/render tienen un límite de **2 GiB de espacio
virtual**, 55 segundos de CPU y 60 de pared por caso. El mayor RSS registrado en
las campañas con esta métrica fue **434.464 KiB (424,28 MiB)**, en la prueba que
incluye el mapa denso, ambas representaciones y el rastreo de asignaciones. No
es una certificación del consumo total de Android en un dispositivo de 2 GB.
Todos los objetivos declarados terminaron dentro de sus presupuestos.

## Conexiones y cobertura

| Componente | Cambio y comprobación |
| --- | --- |
| Escritorio, rutas ojo y espejos | Nuevo índice y GLSL; 53.452.800 píxeles comparados con los shaders originales y su textura original, cero diferencias |
| Raster procedural y explícito | 414.720 píxeles antes/después; incluye el consumidor de `objectData` en el vertex shader |
| Selección | 413 consultas de un píxel, con reflejos, iguales al pase completo; sin scissor residual |
| Reglas geométricas | 914 rayos independientes adicionales, IDs iniciales/finales y ocultar/restaurar |
| Espejos | Nueve grupos aprobados; 7.814 rayos del oráculo float64, exportación por mosaicos, límites ópticos, restauración y ocho ciclos de vida |
| Compatibilidad con el render previo a espejos | Otros 6.912.000 píxeles idénticos; el programa ya no es textual ni instructivamente idéntico, y se declara así |
| Python, Java y decodificador GPU | 3.163.206 IDs en Python y GPU; 3.163.227 comprobaciones Java; bytes Java/Python idénticos, todos los estados, páginas/filas límite, entradas inválidas y textura insuficiente |
| Graphics Android, 4K/8K | Codificador y subida de textura actualizados; los shaders son byte a byte iguales a escritorio; 14 fuentes Java compiladas contra API 35 |
| COSMIC | Sus `sourceSets` consumen las fuentes y assets Graphics actualizados; no hay otro shader duplicado que sincronizar |
| Visor cuántico de PC | Se añadió `EditPages.java` a su lista explícita de fuentes; compilado el subconjunto compartido de nueve clases Java |
| Núcleo y física existentes | 74/74 CTest y 79 archivos de procedencia verificados; no se modificaron algoritmos nativos en esta ronda |
| Persistencia y estado prestado | Contratos de escena, proyectos finitos e Infinite, acoplamiento causal, observaciones de sólo lectura y RNG aprobados |

El shader original se prueba con su textura `RG32I`; el nuevo, con `R32UI`.
Usar el mismo buffer para ambos habría producido una comparación inválida.
La prueba histórica de identidad del programa se sustituyó por comparación
exacta de salida conservando el código original, sin rebajar la precisión.

La validación descubrió un manifiesto de procedencia desactualizado: el
observador optimizado de la ronda anterior aún aparecía con un hash viejo en
Graphics. Se reconciliaron los manifiestos Android/escritorio con las fuentes
ya verificadas. Se conservó el registro anterior y se añadió el pin del
manifiesto revisado al auditor común. No se desactivó ninguna comprobación.

La comprobación Java usa ECJ, nivel de fuente/destino 8 y Android API 35. El
subconjunto compartido de PC es compatible con Java 8; la compilación completa
Gradle/DEX de su aplicación con Java 17 no se ejecutó. Un intento del arnés
con `android.jar` en el classpath Java 17 produjo un conflicto de módulos del
anfitrión; no se cambió la configuración del producto para ocultarlo.

No se construyó APK ni se ejecutó ART, emulador Android o GPU física. Solar
conserva su shader propio y no se modificó. El antiguo XLab
`app/.../ObjectSceneRenderer.java` contiene su propio shader embebido y subida
de excepciones: se identificó esa ruta separada, pero no se migró en esta ronda.
No debe atribuirse a ella automáticamente el resultado de `QuantumRenderer`.

## Fronteras que quedan

- Los rayos siguen recorriendo las mismas celdas. En escenas adversarias, el DDA,
  la reconstrucción de candidatos y la divergencia entre píxeles vuelven a
  dominar. La siguiente modificación es un índice conservador de regiones que
  descarte bloques completos con validación de altura, tiempo, excepciones y
  observables; debe demostrar que no pierde impactos ni cambia normales.
- Un cambio pequeño después de una lista muy grande todavía reconstruye el
  índice completo. Actualizar sólo páginas afectadas requerirá controlar su
  asignación/liberación y el commit del estado, conservando rollback y recreación
  del contexto. Cambiar un millón de excepciones nuevas sigue costando O(E).
- El índice está diseñado para el contrato de IDs actual, no para direcciones
  arbitrarias de Ether. No aumenta el millón de objetos del modelo, los qubits
  activos ni la potencia física del dispositivo.
- No se incorporó la reescritura del hash entero ni el rechazo vertical extra
  por columna: los sondeos dieron ganancias inconsistentes o regresiones.
  Tampoco se reduce resolución, se congelan estados o se esconden rayos agotados.

## Commits y reproducción

- `480cd52`: índice exacto compartido entre GPU, Python y Android.
- `e176b34`: empaquetado por lotes con temporales acotados.
- `2959f48`: pruebas pareadas, perfiles e integración de la prueba de páginas en CI.
- `ba2e37a`: incluir el codificador en el visor PC.
- `4f04322`: reconciliar manifiestos con los commits canónicos comprobados.
- `6564c16`: fijar la procedencia reconciliada y probar consumidores raster.

Los resultados completos incluyen muestras individuales, memoria y hashes:
`results/ether-render-optimization-2026-10-20.json`,
`results/ether-render-combined-2026-10-20.json` y
`results/ether-render-validation-2026-10-20/`.
Se verificó que los hashes de las fuentes medidas coinciden con la entrega.

Con historial Git completo, Python/NumPy, GCC, CMake y EGL/GLES 3:

```sh
export OPENBLAS_NUM_THREADS=1 LP_NUM_THREADS=1 OMP_NUM_THREADS=1
python tools/render_optimization_check.py --output results/render-repeat.json
python tools/system_combined_check.py \
  --baseline d0c148dae0ef09159ad0b1e18f6cafaf6285a7aa \
  --output results/render-combined-repeat.json
python tools/render_route_check.py --output results/raster-repeat.json
python tools/ether_mirror_check.py --output results/mirrors-repeat.json \
  --images build/mirror-images
python tools/system_graphics_check.py --output results/picking-repeat.json
python graphics-desktop/validate.py
python graphics-desktop/validate_contract.py
python graphics-desktop/validate_projects.py --output results/projects-repeat.json
python graphics-desktop/validate_infinite.py --output results/infinite-repeat.json
python tools/cosmic_source_provenance.py
cmake -S . -B build/system -DCMAKE_BUILD_TYPE=Release
cmake --build build/system --parallel 3
ctest --test-dir build/system --output-on-failure
```

Para los bytes y decodificación compartidos:

```sh
mkdir -p build/edit-pages-java build/edit-page-fixtures
javac -d build/edit-pages-java \
  graphics-android/app/src/main/java/com/mundox/graphics/EditPages.java \
  tools/graphics/EditPagesChecks.java
java -Xmx384m -cp build/edit-pages-java com.mundox.graphics.EditPagesChecks \
  build/edit-page-fixtures
python tools/edit_pages_check.py --java-fixtures build/edit-page-fixtures \
  --output results/edit-pages-repeat.json
```

Ejecutar los bancos de velocidad secuencialmente, sin compilaciones simultáneas.
La referencia gráfica se extrae de Git, no de una captura recordada. El formato
entero utilizado está documentado por Khronos en la
[referencia oficial de OpenGL ES 3.0](https://www.khronos.org/files/opengles3-quick-reference-card.pdf).
