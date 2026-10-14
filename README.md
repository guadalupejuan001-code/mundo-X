# Mundo X

**Mundo X** es un proyecto experimental de computación orientado a una idea central:

> **No buscamos más potencia por fuerza bruta. Buscamos representar y transformar solo lo necesario.**

El objetivo no es abandonar la computadora clásica. El objetivo es explorar si podemos usar una computadora clásica como soporte de un modelo cuántico/ondulatorio mucho más eficiente, evitando materializar información que el sistema puede describir mediante estructura, relaciones, fase, interferencia o reglas de evolución.

---

## Objetivo principal

La simulación cuántica clásica habitual representa un estado de `n` qubits mediante:

```
2^n amplitudes complejas
```

Eso funciona, pero el costo de memoria crece exponencialmente.

**Mundo X quiere atacar esa representación, no comprar más RAM.**

La pregunta del proyecto es:

> ¿Qué familias útiles de estados y operaciones cuánticas pueden describirse y evolucionar sin almacenar explícitamente todo el vector de `2^n` amplitudes?

No pretendemos afirmar que todo estado cuántico arbitrario puede comprimirse. Queremos descubrir qué estructura puede explotarse sin perder el comportamiento que nos importa.

---

# Quantum Core

**Quantum Core** es el núcleo actual de Mundo X.

Su función inicial es servir como referencia correcta de matemáticas cuánticas antes de intentar comprimir la representación.

Actualmente implementa:

- estados complejos normalizados;
- superposición;
- fase relativa;
- interferencia;
- Pauli-X;
- Hadamard;
- CNOT;
- entrelazamiento de Bell;
- medición y colapso.

Estado general de referencia:

```
|psi> = sum(alpha_k |k>)
```

con:

```
sum(|alpha_k|^2) = 1
```

## Hello Mundo X

El experimento mínimo es:

```
|0>
 ↓ H
(|0> + |1>) / sqrt(2)
 ↓ phase(pi)
(|0> - |1>) / sqrt(2)
 ↓ H
|1>
```

Ejecuta:

```bash
python hello_mundo_x.py
```

Resultado observado con la implementación actual:

```
inicio             -> probabilidades [1, 0]
Hadamard           -> probabilidades [0.5, 0.5]
cambio de fase pi  -> probabilidades [0.5, 0.5]
interferencia      -> probabilidades [~0, 1]

Mundo X says: 1
TEST: PASS
```

El residuo numérico observado para el estado 0 fue aproximadamente `3.7e-33`, esencialmente cero dentro de precisión de coma flotante.

Esto verifica en el simulador la cadena:

**superposición → fase → interferencia → resultado.**

---

## Lo que ya funciona

### Quantum Core

Funciona como simulador de referencia para las primitivas cuánticas anteriores.

### Entrelazamiento

Puede construir el estado de Bell:

```
(|00> + |11>) / sqrt(2)
```

mediante Hadamard + CNOT.

### Transformer X

Existe como componente experimental separado para explorar representación y aprendizaje sobre estados compactos.

No define el núcleo de Mundo X y todavía no se usa como prueba de ventaja computacional.

---

## Estado actual de eficiencia

El README principal ya no usa el statevector denso como referencia visual principal.
Ese método queda únicamente como **baseline histórico** dentro de los resultados de
benchmark.

El estado actual de Mundo X es **Compact Core**.

Resultados medidos:

| Prueba | Compact Core |
|---|---:|
| GHZ 24 qubits | **408 bytes de estado** |
| GHZ 40 qubits | **~680 bytes de estado** |
| GHZ 10,000 qubits | **~23.97 MiB de estado** |

## Capacidad teórica con 1 GB

Para la representación bit-packed actual:

```
16 * n * ceil(n / 64) + n bytes
```

Con **1 GB decimal = 1,000,000,000 bytes**:

```
63,232 qubits -> 999,634,688 bytes
63,233 qubits -> 1,000,662,225 bytes
```

Por tanto:

```
Compact Core actual con 1 GB: 63,232 qubits
```

Con **1 GiB = 1,073,741,824 bytes**:

```
65,532 qubits
```

> Estos valores corresponden a la familia Clifford/stabilizer soportada por el
> Compact Core actual. No representan qubits universales arbitrarios.

Los resultados comparativos contra el motor denso se conservan en:

```
results/native_benchmark_2026-10-13.md
```

para reproducibilidad, pero ya no definen el estado actual del proyecto.

---

# XASM v0.1 — primer lenguaje de Mundo X

Mundo X ya no se usa únicamente llamando funciones C++ directamente.

Ahora incluye **XASM v0.1**, un lenguaje mínimo que ejecuta programas sobre
Compact Core.

Ejemplo Bell:

```text
qubits 2
h 0
cx 0 1
show
bytes
assert ghz
```

Compilar el runtime:

```bash
cmake -S . -B build
cmake --build build -j
```

Ejecutar un programa:

```bash
./build/mundo_x run examples/bell.x
```

Instrucciones actuales:

| XASM | Operación |
|---|---|
| `qubits N` | crea el espacio de N qubits |
| `h q` | Hadamard |
| `x q` | Pauli-X |
| `z q` | Pauli-Z |
| `s q` | fase S |
| `cx c t` | CNOT control→target |
| `show` | muestra los generadores compactos |
| `bytes` | muestra bytes del estado |
| `assert one` | valida el Hello X de 1 qubit |
| `assert ghz` | valida Bell/GHZ |

## Resultados compilados de XASM v0.1

Los tres programas iniciales fueron compilados y ejecutados con éxito:

```
examples/hello.x
qubits=1
instructions=3
state_bytes=17
ASSERT one PASS
```

```
examples/bell.x
qubits=2
instructions=2
state_bytes=34
STATE
+XX
+ZZ
ASSERT ghz PASS
```

```
examples/ghz4.x
qubits=4
instructions=4
state_bytes=68
STATE
+XXXX
+ZZII
+IZZI
+IIZZ
ASSERT ghz PASS
```

Esto convierte Compact Core en un **runtime programable**. Todavía no es una red
de comunicación nueva; es el sustrato sobre el cual ya podemos definir programas,
funciones, macros y más adelante un modelo operativo propio.

---

# Benchmark 2D cluster-state — resultado grande

Se añadió un benchmark de **estado cluster 2D**, una estructura de
entrelazamiento mucho más rica que GHZ y relevante para computación cuántica
basada en mediciones.

## Validación 4×4

El mismo circuito se ejecutó con Compact Core y Dense Core:

```
CLUSTER_COMPACT rows=4 cols=4 qubits=16
state_bytes=272
signature=PASS
ms=0.005
```

```
CLUSTER_DENSE rows=4 cols=4 qubits=16
state_bytes=524288
state_mib=0.500
signature=PASS
ms=22.329
```

Ambos motores validaron la firma del mismo estado cluster.

## Escala 32×32

Después se ejecutó:

```
32 × 32 = 1,024 qubits
```

con Compact Core:

```
CLUSTER_COMPACT rows=32 cols=32 qubits=1024
state_bytes=263168
state_mib=0.251
signature=PASS
ms=10.206
```

Resultado:

- **1,024 qubits**
- **~257 KiB de estado**
- **firma del cluster: PASS**
- **~10.2 ms** en el runner Linux de GitHub Actions

Un statevector denso de 1,024 qubits requeriría `2^1024` amplitudes y no es una
representación materializable en hardware convencional.

> Este resultado sigue perteneciendo a la familia Clifford/stabilizer. No demuestra
> simulación universal arbitraria de 1,024 qubits. Demuestra que una estructura
> cuántica 2D altamente entrelazada puede representarse y validarse compactamente
> sin materializar un statevector completo.


## Verificación real en Android

La APK fue instalada y ejecutada en un teléfono Android arm64 real.

### GHZ — 1,000 qubits

Salida observada en el dispositivo:

```
COMPACT X
qubits=1000
state_bytes=257000
state_mib=0.245
GHZ=PASS
time_ms=8.909

Android process PSS ~= 68563 kB
```

Resultado:

- **1,000 qubits GHZ**
- **257,000 bytes de estado**
- **~0.245 MiB de estado**
- **GHZ: PASS**
- **~8.9 ms**
- proceso Android completo: **~68.6 MB PSS**

### Cluster 2D — 32×32 = 1,024 qubits

Salida observada en el mismo dispositivo:

```
2D CLUSTER STATE
rows=32
cols=32
qubits=1024
state_bytes=263168
state_mib=0.251
signature=PASS
time_ms=53.212

Android process PSS ~= 63318 kB
```

Resultado:

- **1,024 qubits**
- **263,168 bytes de estado**
- **~0.251 MiB de estado**
- **firma del cluster: PASS**
- **~53.2 ms**
- proceso Android completo: **~63.3 MB PSS**

El PSS corresponde a **todo el proceso Android**: runtime, interfaz, librerías,
APK y memoria nativa. No debe confundirse con el tamaño del estado cuántico
compacto, que en estas pruebas fue de ~0.25 MiB.

Estas mediciones confirman que los resultados del runtime compilado también se
reproducen en hardware móvil real.


---

# Próxima meta: Compact X general

Compact Core ya demuestra la idea de representación estructurada dentro de
Clifford/stabilizer.

La siguiente etapa es conservar esa eficiencia al salir de esa familia,
empezando por recursos no-Clifford como la puerta `T` / magic, sin volver a
materializar un statevector completo.

Ideas que se pueden explorar y comparar:

- estados generados por reglas;
- representaciones dispersas;
- simetrías;
- factorizaciones;
- redes tensoriales;
- modos/frecuencias;
- relaciones locales;
- transformaciones implícitas;
- combinaciones híbridas.

La regla es estricta:

> **No vale reducir memoria en un lugar si el mismo costo reaparece oculto en otra tabla, caché o estructura.**

---

## Criterio de éxito

Una nueva representación de Quantum Core será interesante solo si cumple simultáneamente:

1. reproduce resultados del núcleo de referencia dentro de una tolerancia definida;
2. conserva normalización y fase cuando sean necesarias;
3. reproduce interferencia y entrelazamiento en los casos compatibles;
4. usa menos memoria explícita para la familia de estados evaluada;
5. no materializa secretamente el vector completo durante el cálculo;
6. permite medir tiempo, memoria y error contra el baseline denso.

Nuestro objetivo no es decir:

> "Mundo X es más poderoso."

Nuestro objetivo es poder demostrar algo del tipo:

```
misma tarea
mismo resultado dentro de tolerancia
menos representación explícita
menos memoria
```

---

## Orden del proyecto

```
1. Quantum Core          -> referencia cuántica densa            [HECHO]
2. Compact Quantum Core  -> baseline Clifford compilable          [ACTIVO]
3. Compact X general     -> superar límites del baseline          [SIGUIENTE]
4. Interface X           -> entrada/salida de información
5. Transformer X         -> usar aprendizaje solo si aporta
6. Fold / modo operativo -> investigar dinámica posterior
```

Nada se añade porque "suene cuántico". Cada etapa debe justificar su costo con una prueba.


---

# Mundo X Lab APK v0.1

La primera APK de Mundo X ya compila correctamente en GitHub Actions.

Build verificado:

```
workflow: Build Mundo X Lab APK
última verificación relevante: run #16
status: success
artifact: Mundo-X-Lab-APK
```

Archivo extraído y verificado:

```
Mundo-X-Lab-v0.1.apk
size: 983,977 bytes
sha256: d2673d4599b39327e34e0f13602001f23f3c9f0f5ce7fee97a15cee05be9199e
ABI: arm64-v8a
minSdk: 26
targetSdk: 35
```

El APK contiene el núcleo nativo:

```
lib/arm64-v8a/libmundox.so
```

y expone desde Android:

- **HELLO X** — compara Dense Core y Compact Core en el circuito mínimo;
- **RUN COMPACT** — crea un estado GHZ compacto de 2 a 10,000 qubits;
- **COMPARE DENSE** — compara ambos motores con límite de seguridad de 22 qubits para Dense;
- **PRACTICE** — permite ejecutar comandos `h`, `x`, `z`, `s`, `cx`, `show`, `bytes`;
- lectura del **PSS aproximado del proceso Android** después de cada experimento.

La app usa Java únicamente para la interfaz. El cálculo principal se ejecuta en
C++17 mediante JNI y reutiliza los motores nativos de Mundo X.

Este build es un **debug APK** firmado por el toolchain de Android. No es una
release firmada para publicación en Google Play.

La estructura del APK fue inspeccionada tras la compilación y contiene el DEX,
manifest, recursos y la librería nativa esperada.

La instalación física **ya fue verificada en un dispositivo Android arm64**.
En ese dispositivo pasaron tanto el benchmark GHZ de 1,000 qubits como el
cluster-state 2D de 1,024 qubits.


---

# Windows 10 x64 — build nativo verificado

Mundo X ya compila como ejecutable nativo de Windows x64 usando MSVC.

Build de GitHub Actions:

```
workflow: Build Mundo X Windows 10 x64
run: #1
status: success
```

Archivo principal:

```
mundo_x.exe
size: 83,968 bytes
format: PE32+ x86-64
```

El workflow ejecutó antes de publicar el artefacto:

```
mundo_x.exe hello
mundo_x.exe run examples\\bell.x
mundo_x.exe cluster 4 4 --dense
mundo_x.exe cluster 32 32
```

Todos los smoke tests terminaron correctamente.

Uso rápido en Windows 10 x64:

```powershell
.\\mundo_x.exe hello
.\\mundo_x.exe bench 1000
.\\mundo_x.exe cluster 32 32
.\\mundo_x.exe run examples\\bell.x
.\\mundo_x.exe practice 2
```

El paquete Windows incluye el ejecutable y los programas de ejemplo.

---

# Experimento visual: representación explícita vs generativa

La APK incluye un experimento visual Android diseñado para poner a prueba una
idea central de Mundo X:

```
estado mínimo + regla + fase/frecuencia + perspectiva + tiempo
-> estado visible
```

El experimento ahora tiene tres rutas. `CLASSIC` y `GENERATIVE` sirven como
baselines visuales. `COMPACT -> VISUAL` conecta directamente el estado del
Compact Core con los parámetros que gobiernan la escena.

Esto **no** significa que exista hardware cuántico ni que Compact Core ya acelere
gráficos. Compact Core es un **simulador cuántico compacto de estados
Clifford/estabilizadores**: representa y evoluciona esos estados mediante un
tableau bit-packed sin materializar el statevector completo de `2^n` amplitudes.
Lo nuevo es que ese estado cuántico compacto ya participa directamente en la
generación visual.

Modos:

### CLASSIC

Mantiene estado explícito por elemento:

- posición x/y;
- velocidad x/y;
- fase;
- tamaño;
- color.

Para `N` elementos, el estado explícito instrumentado es aproximadamente:

```
N * 28 bytes
```

### GENERATIVE

No mantiene arrays por objeto para describir la escena. Cada elemento visible se
reconstruye por índice desde una semilla global, reglas deterministas, tiempo,
fase, perspectiva y feedback de interacción.

El contador `explicit_scene_state` reporta solo el estado lógico explícito de
la representación, no el overhead completo del runtime Android ni Canvas.

### COMPACT -> VISUAL

Este modo mantiene un estado de **32 qubits** dentro de Compact Core y lo hace
evolucionar con las operaciones Clifford ya soportadas:

```
H
S
X
Z
CNOT
```

Cada frame se extrae del tableau compacto una firma numérica derivada de:

- densidad de X;
- densidad de Z;
- densidad de Y;
- fases activas;
- actividad diagonal X/Z;
- correlación local aproximada;
- hash determinista del tableau.

Esa firma controla directamente:

- color;
- frecuencia;
- fase;
- profundidad;
- deformación;
- distribución/tipo de formas.

La ruta es:

```
Compact Core (32q)
-> firma del tableau
-> parámetros visuales
-> escena
```

El estado de Compact Core para 32 qubits ocupa **544 bytes** con la
representación actual. Sumado al estado global mínimo instrumentado del renderer,
el modo reporta aproximadamente **608 bytes de estado lógico explícito** antes
del overhead del proceso Android.

Tocar una zona de la pantalla se mapea a uno de los 32 qubits y aplica una
perturbación Clifford al estado, de modo que la interacción modifica tanto el
feedback visual como el estado compacto que alimenta los siguientes frames.

### Interacción / pliegue

Tocar la escena inyecta energía a una variable global de feedback. Esa variable
se decae con el tiempo y modifica la siguiente evolución visual, creando un
bucle:

```
estado visible
-> interacción
-> feedback
-> nueva evolución
-> nuevo estado visible
```

### STRESS

El modo normal renderiza 6.000 elementos y STRESS sube a 24.000.

La pantalla reporta en vivo:

- FPS suavizado;
- tiempo por frame;
- objetos;
- estado explícito estimado;
- PSS total del proceso Android;
- feedback e interacción.

Resultados medidos en hardware Android real (captura del 2026-10-14):

```
mode=GENERATIVE
objects=6000
fps=17.0
frame=60.42 ms
explicit_scene_state=64 bytes
process_PSS=105660 kB
feedback=1.475
touch=(0.62, 0.64)
```

Lectura del experimento:

- la escena generativa compleja se produjo con 64 bytes de estado lógico explícito instrumentado;
- el proceso Android completo ocupó ~105.7 MB PSS, por lo que el estado de la representación no debe confundirse con el overhead total de Android/Java/Canvas/framebuffer;
- 60.42 ms por frame está muy por encima del presupuesto de ~16.67 ms requerido para 60 FPS;
- el cuello de botella actual es cómputo por frame, no el tamaño del estado explícito de escena;
- este resultado demuestra compacidad de representación en esta demo, no una mejora global de rendimiento gráfico.

Próximo objetivo experimental: reducir el costo de reconstrucción visual por frame sin aumentar de forma significativa el estado explícito.

## Build con acoplamiento Compact Core -> visual

Build verificado en GitHub Actions:

```
workflow: Build Mundo X Lab APK
run: #26
status: success
commit: 52e64a88f7b1ff31ea75e3e5c548ef88a7d5a2d7
```

APK extraída:

```
Mundo-X-Lab-debug.apk
size: 1,034,605 bytes
sha256: 497135ba9b1ad0dc0ca1214dfb735761174460876e9eda16aa64235feda6824e
```

## Primera medición física de COMPACT -> VISUAL

En un **Samsung A16 5G** se observó:

```
mode=COMPACT->VISUAL
objects=6000
fps=24.8
frame=40.28 ms
logical_state=608 bytes
process_PSS=108494 kB
CompactCore=32q
core_bytes=544
step=1067
```

La captura fue tomada mientras se movía el dedo en círculos sobre la escena.

Comparada con la captura previa de `GENERATIVE` a 6,000 objetos:

| Medición | GENERATIVE | COMPACT -> VISUAL |
|---|---:|---:|
| FPS observados | 17.0 | **24.8** |
| Tiempo por frame | 60.42 ms | **40.28 ms** |
| Estado lógico | 64 B | 608 B |
| PSS del proceso | 105,660 kB | 108,494 kB |

En estas dos observaciones, `COMPACT -> VISUAL` mostró aproximadamente **+45.9% FPS**
y **-33.3% tiempo por frame**, con ~**+2.7% PSS**.

> Esta comparación todavía no es un benchmark A/B controlado y no demuestra que
> Compact Core sea la causa de la mejora. Temperatura, interacción, carga del
> sistema y momento de la medición no fueron controlados.

El registro completo, incluida la prueba STRESS de 24,000 objetos, está en:

```
results/visual_android_samsung_a16_5g_2026-10-14.md
```


## Ruta GPU para eliminar el cuello de botella de Canvas

Se añadió una segunda ruta visual Android usando **OpenGL ES 3.0**.

Objetivo:

```
Compact Core
-> 8 parámetros derivados del tableau
-> uniforms GPU
-> 1 draw call
-> 6,000 / 24,000 / 96,000 elementos
```

La CPU ya no calcula posición, color, profundidad y forma de cada elemento ni
ejecuta miles de llamadas `Canvas.draw*` por frame.

En esta ruta:

- el índice de cada elemento nace en GPU mediante `gl_VertexID`;
- posición, color, fase, frecuencia, profundidad y forma se reconstruyen dentro
  del vertex/fragment shader;
- Compact Core sigue evolucionando en CPU y entrega solamente su firma compacta;
- la interacción táctil continúa perturbando uno de los 32 qubits;
- el estado lógico instrumentado se mantiene en ~**608 bytes**;
- el render masivo se envía mediante **una sola llamada `glDrawArrays` por frame**.

Build verificado:

```
workflow: Build Mundo X Lab APK
run: #36
status: success
commit: d2a2b8839ce5acf75dfe61a562fdc263e0925178
APK size: 1,042,561 bytes
sha256: 56a10c49d33cfb446b685fbdc76b456281dfe4d0389bfb1b2c49742735ca01ed
```

La APK compila correctamente, pero **todavía no se documenta una ganancia de FPS**
para esta ruta GPU hasta medirla en el Samsung A16 5G real.



---

# Teletransportación cuántica en Compact Core

Mundo X ya incluye una prueba reproducible de **teletransportación de estado
cuántico** implementada directamente sobre Compact Core.

Ruta:

```
q0: estado origen
q1-q2: par entrelazado
CNOT(q0,q1)
H(q0)
medir q0 -> m0
medir q1 -> m1
X(q2)^m1
Z(q2)^m0
q2: estado destino
```

Se validaron los seis estados estabilizadores de un qubit:

```
|0>   -> +Z
|1>   -> -Z
|+>   -> +X
|->   -> -X
|+i>  -> +Y
|-i>  -> -Y
```

y las cuatro ramas de medición `00`, `01`, `10`, `11` para cada estado.

Resultado de GitHub Actions run #33:

```
passed=24/24
compact_state_bytes=51
result=PASS
```

Esto demuestra teletransportación correcta del **estado cuántico** dentro del
dominio Clifford/estabilizador actual de Compact Core. No implica transporte de
materia ni hardware cuántico físico.

Registro completo:

```
results/compact_quantum_teleportation_2026-10-14.md
```

---

# Performance launcher Android

La APK incluye una pantalla de preparación de rendimiento para lanzar Wild Rift
o cualquier otra app elegida por el usuario.

Ruta:

```
preflight del teléfono
-> recomendación
-> lanzar app
-> Mundo X termina su propio proceso
```

El preflight mide:

- RAM total y disponible;
- flag `lowMemory` de Android;
- Battery Saver;
- estado térmico;
- temperatura y nivel de batería;
- tasa de refresco reportada por la pantalla.

También ofrece accesos directos a ajustes de batería y pantalla.

Cuando se lanza la app objetivo, Mundo X ejecuta liberación de sus propios
objetos, abre la app mediante Intent oficial y termina su propio proceso después
de ~0.9 s para no permanecer compitiendo por RAM.

Límites deliberados:

- no requiere root;
- no modifica otras apps;
- no fuerza clocks CPU/GPU;
- no cambia governors;
- no mata procesos ajenos;
- no altera anti-cheat ni archivos de juegos.

Build verificado:

```
workflow: Build Mundo X Lab APK
run: #39
status: success
commit: 579723969a6d62ca06a99af3b6b306957784f93f
APK size: 1,042,601 bytes
```

Este launcher prepara y reduce overhead propio; no se documenta una ganancia de
FPS hasta medirla en hardware real.

---

# Wild Rift low-end mode — Android

Mundo X Lab incluye una capa de preparación para **League of Legends: Wild Rift**
sin root y sin modificar archivos del juego.

Paquete oficial detectado:

```
com.riotgames.league.wildrift
```

Funciones:

- comprueba si Wild Rift está instalado;
- lee RAM total y disponible;
- detecta el flag de memoria baja de Android;
- lee Battery Saver;
- lee estado térmico cuando Android lo expone;
- lee temperatura y nivel de batería;
- genera un preset recomendado para hardware low-end;
- abre Wild Rift mediante su Intent oficial;
- cierra Mundo X ~1.2 s después del lanzamiento para liberar su propia RAM.

El modo **no** modifica APK, archivos, memoria ni anti-cheat de Riot. Tampoco
fuerza frecuencias de CPU/GPU ni cambia resolución de otra app, porque Android no
permite esas acciones a una app normal sin privilegios especiales.

Objetivo experimental:

```
preflight del teléfono
-> configuración gráfica recomendada
-> lanzar Wild Rift
-> Mundo X sale de memoria
```

Los benchmarks FPS/temperatura antes vs después se documentarán cuando se midan
en un teléfono real.

---

## Estado científico

Mundo X corre actualmente sobre **hardware convencional** (CPU ARM64/x86_64).

**Quantum Core y Compact Core simulan sistemas cuánticos.** La diferencia es que
Quantum Core usa un statevector denso como referencia, mientras Compact Core usa
una representación estructurada bit-packed para estados Clifford/estabilizadores,
evitando materializar el vector completo de `2^n` amplitudes en esa familia.

Todavía no se ha demostrado:

- ventaja cuántica de hardware;
- hardware cuántico físico propio;
- eliminación general del crecimiento exponencial para estados arbitrarios;
- superioridad general frente a todos los simuladores existentes;
- una nueva teoría física.

Eso no invalida el objetivo. Define exactamente qué falta demostrar.

La hipótesis de trabajo de Mundo X es más limitada y comprobable:

> **Para estados y problemas estructurados compatibles, Compact Core demuestra que
> es posible representar y evolucionar el comportamiento cuántico relevante sin
> materializar el statevector denso completo. El reto abierto es extender esa
> eficiencia fuera de Clifford/estabilizadores sin perder corrección ni trasladar
> el costo a otra estructura oculta.**
