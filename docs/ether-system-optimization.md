# Ether: optimización de recorridos reales del sistema

## Referencia, alcance y entrega

Rama local: `codex/ether-system-optimization-20261020`.
Referencia recuperable: `c13e78f91887b45d735c5fe3d8a07a000bd03d34`.
Se comprobaron las 35 ramas remotas al empezar y al terminar: no cambiaron.
`main` permanece en `7b028db2344912b2ab86a0affa2a1fe56180922b`; el PR #25,
en borrador, en `aab0930378796ebfd42f9629e2575108ca94fcff`.
Los cambios de esta revisión están en commits locales, sin publicar ni fusionar.

La física `PhysicsWorld` procedía del PR #22, commit
`c7e78948ea95080d0603608696da221f02bf4584`, y no estaba en esta rama.
Se recuperaron sólo su cabecera y su prueba, incorporándolas a CMake. No se
sustituyó Ether por la versión antigua del PR ni se incorporaron sus cambios Solar.
La alternativa `RulePhysics` 3D sigue en el PR #21 y no se presenta como integrada.
Solar continúa en pausa; ningún archivo Solar se modificó en esta revisión.

## Resultado medido

Mismo anfitrión, compilador y entradas antes/después. Los grupos nativos alternan
el orden de ejecución; el render usa Mesa llvmpipe, por software. Las consultas
no tienen caché de respuestas. La carga combinada cambia estado, cámara,
tiempo, excepciones y modo de espejos; fuerza el dibujo de cada cuadro.

| Carga concreta | Antes, ms | Después, ms | Cociente |
| --- | ---: | ---: | ---: |
| Física dispersa, 256 cuerpos | 0,031 | 0,031 | 0,98× |
| Física dispersa, 4.096 cuerpos | 10,030 | 0,517 | 19,40× |
| Física dispersa, 16.384 cuerpos | 153,824 | 3,417 | 45,02× |
| Física dispersa, 32.768 cuerpos | 646,871 | 7,740 | 83,57× |
| 2.048 consultas, 1.000 qubits activos, producto | 4,506 | 0,110 | 40,97× |
| 2.048 consultas, 1.000 qubits activos, GHZ | 22,272 | 1,409 | 15,80× |
| 2.048 consultas, 1.000 qubits activos, Clifford mezclado | 26,769 | 23,310 | 1,15× |
| MicroLab, 16.384 partículas, seis pasos con campo | 10,088 | 9,121 | 1,11× |
| Carga combinada, mediana de tres campañas | 87,420 | 80,023 | 1,09× |
| Selección reflejada, 960×540, un millón de objetos | 356,720 | 0,551 | 647,3× |

La mediana de la carga combinada disminuye **8,5 %**. Las tres
comparaciones individuales mejoraron, con variación del anfitrión. Esta carga
coejecuta 4.096 cuerpos 2D, MicroLab con 16.384 partículas, una sesión Ether con
256 direcciones activas y 512 observables, y un millón de cajas a 320×180.
Incluye comprobaciones de resultados en el coste. Es un banco de pruebas de
carga conjunta: **PhysicsWorld todavía no controla la geometría 3D de COSMIC**.
No es una medida de FPS de una APK ni una prueba en Samsung.

La aceleración de selección corresponde a tocar un objeto. No multiplica los
FPS del dibujo continuo. En ella se eliminaron un pase completo de IDs y la
lectura de todos sus píxeles: se conserva el mismo rayo y se leen cuatro bytes.
Las búsquedas de colisión dispersas tampoco representan una pila de cuerpos
mutuamente solapados. El caso pequeño conserva un coste próximo al original;
la validación y el commit transaccional añaden trabajo aunque no se use el índice.

Datos íntegros, muestras y hashes:
- `results/ether-system-optimization-2026-10-20.json`.
- `results/ether-system-graphics-2026-10-20.json`.
- `results/ether-system-combined-2026-10-20.json`.
- `results/ether-system-validation-2026-10-20/`.

## Qué cambia y por qué conserva el resultado

### Observación cuántica

`PauliObserver` construye un índice de columna a fila pivote. Para reducir una
consulta toma su primer bit presente; si esa columna no tiene pivote, ya puede
probar que el operador no pertenece al espacio estabilizador. Si lo tiene,
elimina esa fila con la misma multiplicación Pauli con signo. La forma escalonada
impide reintroducir bits anteriores: el cursor sólo avanza.

Esto evita examinar filas irrelevantes, también en consultas nuevas. No elimina
componentes del estado, no cambia fases ni resultados, ni crea un historial de
respuestas. Se valida el tamaño y los bits de relleno de entradas empaquetadas.
Para 1.000 qubits, el índice añade 16.000 bytes: el payload del observador pasa
de 268.000 a 284.000 bytes en este anfitrión de 64 bits. Se declara en la métrica
existente; no se oculta como memoria gratuita. Las firmas C/JNI no cambian.

### Física 2D

Se mantiene el integrador, la gravedad uniforme, el suelo y la respuesta por
impulsos del PR #22. Para más de 512 cuerpos, una jerarquía espacial descarta
pares cuyos intervalos no pueden tocarse. Después de cada contacto se actualizan
los límites y se consulta de nuevo: una corrección puede crear otra colisión.
Los pares se resuelven en el mismo orden `(i,j)` del original. Un listado fijo
calculado antes de resolver contactos perdería esa propiedad.

La topología del índice se reutiliza y sus límites se reajustan cada paso. Se
reconstruye al cambiar el número de cuerpos o perder localidad espacial. Una
nube densa pasa al recorrido original, evitando reconstrucciones repetidas de
listas de candidatos. La prueba geométrica también descarta ejes separados
antes de calcular cuadrados y raíces.

El paso se prepara en espacio de trabajo reutilizable y se confirma con una
copia que no lanza excepciones. Entradas inválidas, desbordamientos o falta de
memoria no publican medio paso. Se conservan las direcciones de los cuerpos
públicos durante `step()`, una propiedad de la interfaz anterior. Las reglas
rechazan gravedad no finita y restitución fuera de [0,1]. El modelo de distancias
al cuadrado declara su rango numérico representable, en lugar de aceptar NaN.

### MicroLab

La reflexión de partículas mantiene el orden de operaciones float y el generador
aleatorio. Cuando `x+1` ya pertenece a [0,4), se omite `fmod`; fuera se usa el
recorrido anterior. Los agentes y sus nutrientes siguen avanzando, aunque el
modo visible muestre partículas. No se ahorra tiempo congelando estados ocultos.
MicroLab conserva FactorizedCore de cuatro qubits y sus puertas T: no se sustituyó
por un backend Clifford que perdería esa función.

### Selección gráfica

Escritorio y `QuantumRenderer` aplican un rectángulo GLES de un píxel al pase de
IDs. Se conservan la proyección, el viewport completo y las coordenadas del rayo,
incluidos los espejos. La lectura pasa de W×H×4 bytes a cuatro. El rectángulo se
retira incluso al fallar y el siguiente cuadro completo se comprueba contra la
referencia. En escritorio la caché de imagen se invalida antes del pase, para
no reutilizar una imagen alterada si la lectura falla.

Los shaders finales son idénticos a los de la referencia. Se probaron y retiraron
una convolución explícita para el hash por bytes y un filtro por rango de
excepciones: no mejoraban consistentemente el dibujo de escenas móviles.
También se replanteó el primer índice físico: reconstruirlo en cada paso y
usarlo en escenas pequeñas desperdiciaba parte de la ganancia.

## Evidencia y cobertura de esta revisión

| Ruta o archivos revisados | Ejecución y evidencia |
| --- | --- |
| `pauli_observer.hpp`, `infinite_core.hpp`, `infinite_session.hpp`, `core_1000.cpp` | 18,432 comparaciones de observación, entradas empaquetadas inválidas, dos lectores y un escritor; 74/74 CTest, incluidos C ABI y PC |
| `physics_world.hpp`, `physics_world.cpp`, CMake | 576,001 comprobaciones diferenciales; 128 escenas × 12 pasos; 2,098,395 contactos; 48 pasos con fallback denso; suelo, masas, cuerpos fijos, coincidencias, cadenas de contacto y rollback |
| `micro_lab.hpp`, consumidor `micro-jni.cpp`, fuentes compartidas COSMIC | 6,014,277 valores float comparados byte a byte, además del estado JSON, puertas, inyecciones y mismas semillas |
| `renderer.py`, `engine.py`, `egl_runtime.py` | 413 consultas de selección, incluidas 35 vistas con objetos reflejados; cero diferencias de ID y cero imágenes alteradas por un scissor residual |
| Shaders de escritorio/Android y herramientas de espejos | Nueve grupos aprobados; 7.814 rayos de oráculo float64; 6.912.000 píxeles del modo original iguales; programa primario preprocesado idéntico |
| `QuantumRenderer.java` y 12 fuentes Java de producción/instrumentación | Compilación ECJ Java 8 contra Android API 35; no ejecución ART/GPU |
| Carga combinada con el puente nativo real | 54 pares de cuadros; mismos hashes de imagen, física, MicroLab y estado final de Ether; restauración invalida el epoch |
| Procedencia del código nativo y JNI compartido | 79 archivos verificados contra commits explícitos; ningún verificador deshabilitado |
| PhysicsWorld y lote Ether | ASan + UBSan: 32 y 15.238 comprobaciones, respectivamente; LeakSanitizer no ejecutado |

Es una cobertura de los cambios de esta ronda, que se añade a las auditorías
anteriores. No equivale a volver a auditar cada archivo histórico del repositorio.
Las rutas Android se compilaron; no se construyó una APK ni se midió un dispositivo.

## RAM, frontera alcanzada y trabajo que queda

Todos los procesos medidos se limitaron a **2 GiB de espacio virtual**, 55 s de
CPU y 60 s de pared por caso. El máximo RSS de estas campañas fue **138.036 KiB
(134,80 MiB)**, en la selección gráfica. Es memoria real del proceso anfitrión;
no representa el consumo total de Android en un teléfono de 2 GB. Los bytes de
payload y las capacidades temporales se informan aparte; no se confundieron
con RSS ni con un recuento completo de llamadas al asignador.

El objetivo mayor de física se fijó en 100.000 cuerpos: primer paso en
**49.735 ms**, con 21,599,928 bytes de capacidades de trabajo.
Se llegó al objetivo; no se agotó RAM. El recorrido cuadrático anterior no se
cronometró con 100.000 cuerpos: la última comparación pareada fue 32.768.
Los buffers del índice se conservan para reutilización hasta destruir el mundo;
su memoria depende de la mayor población procesada, no del número de pasos.

Fronteras que siguen siendo reales:

1. Una nube de contactos densos puede requerir O(n²) pares. La siguiente mejora
   sería agrupar contactos e islas con una política explícita de resolución.
   Paralelizar los impulsos actuales sin controlar su orden cambiaría el resultado.
2. `PhysicsWorld` es 2D discreto: puede atravesar cuerpos con pasos demasiado
   grandes y no implementa dinámica rígida 3D ni gravedad N-body. Antes de unirlo
   a la escena 3D hace falta definir geometría, unidades y pertenencia del estado.
3. En la carga combinada domina el dibujo. La próxima optimización estructural
   debe reducir candidatos geométricos reconstruidos por cuadro mediante límites
   conservadores por regiones, actualizados por tiempo, excepciones y observables.
   Repetir imágenes antiguas no resuelve las cámaras o escenas nuevas.
4. Las consultas densas de Clifford siguen requiriendo muchas operaciones;
   el índice ayuda más a soportes pequeños y estados estructurados. No cambia
   la complejidad del tableau ni elimina los presupuestos de componente activo.
5. Ether representa un dominio lógico extensible mediante reglas. El soporte
   activo, los observadores, las colisiones y los píxeles siguen siendo finitos.
   No se han creado infinitos qubits activos ni se ha probado una aceleración
   cuántica física. Los límites de direcciones/transportes existentes no se subieron.

## Commits revisables

- `6c171ad`: recuperar PhysicsWorld del PR #22 sin revertir Ether.
- `00e2f01`: reducir consultas Pauli por los pivotes que necesitan.
- `201388f`: evitar restos innecesarios en MicroLab.
- `8947e3e`: índice físico, refit y contactos ordenados.
- `63ba1fd`: selección de un píxel en escritorio y Android.
- `59916f0`: conservar referencias públicas de cuerpos al confirmar el paso.
- `7bd4a52`: campañas comparativas y referencias de procedencia.
- `f50be37`: comparación byte a byte de vértices MicroLab.

## Repetición

Se necesita historial Git, GCC con C++17, CMake, Python, NumPy y EGL/GLES 3.
La campaña usa Linux para afinidad, límites de proceso y RSS. Obtener también
el commit de física del PR #22 si el clon no lo contiene:

```sh
git fetch origin refs/heads/codex/physics-foundation-20261019
cmake -S . -B build/system -DCMAKE_BUILD_TYPE=Release
cmake --build build/system -j 3
ctest --test-dir build/system --output-on-failure
python tools/system_optimization_check.py
OPENBLAS_NUM_THREADS=1 LP_NUM_THREADS=1 python tools/system_graphics_check.py
OPENBLAS_NUM_THREADS=1 LP_NUM_THREADS=1 python tools/system_combined_check.py
OPENBLAS_NUM_THREADS=1 LP_NUM_THREADS=1 python tools/ether_mirror_check.py \
  --output results/mirror-regression-repeat.json --images build/mirror-images
```

La campaña combinada compila las dos bibliotecas fuera de los procesos medidos.
Su prueba de física compara el PR #22 exacto; el resto compara `c13e78f`.
Ejecutar las campañas de tiempos secuencialmente, sin una compilación concurrente.
Los resultados JSON incluyen hashes de las fuentes; los tiempos son observaciones,
no umbrales de rendimiento garantizado para otra máquina.
