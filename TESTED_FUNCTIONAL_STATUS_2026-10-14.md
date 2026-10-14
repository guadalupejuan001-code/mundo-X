# Mundo X — estado funcional probado — 2026-10-14

Este documento separa deliberadamente tres categorías:

- **PROBADO EN HARDWARE REAL**: observado en un teléfono Android físico;
- **VERIFICADO EN CI/BUILD**: compila y/o pasa pruebas automatizadas reproducibles;
- **IMPLEMENTADO / EXPERIMENTAL**: existe en código, pero todavía no tiene una validación suficiente para atribuirle rendimiento o corrección general.

La intención es conservar resultados reales y evitar mezclar prototipos con conclusiones demostradas.

---

# 1. Qué es Mundo X hoy

Mundo X es actualmente una plataforma experimental híbrida con varias líneas de trabajo:

1. **simulación cuántica por software**;
2. **representación compacta de estados Clifford/estabilizadores**;
3. **renderizado procedural y reconstrucción local en GPU**;
4. **sincronización de estado/eventos por red**;
5. **experimentos Android de launcher/rendimiento**;
6. **Transformer X como módulo neuronal experimental separado**.

No es un computador cuántico físico y no contiene qubits físicos.

El núcleo cuántico actual corre sobre CPU ARM64/x86_64 convencional.

La descripción técnicamente precisa del núcleo es:

> **simulador cuántico compacto de estados Clifford/estabilizadores sobre hardware convencional**

y, para el proyecto completo:

> **plataforma experimental de software híbrido que combina simulación cuántica compacta con sistemas clásicos de GPU, red y generación procedural**

---

# 2. Núcleo cuántico — VERIFICADO

## Quantum Core denso

Existe como referencia matemática para circuitos pequeños.

Soporta, entre otros:

- estados complejos;
- Hadamard;
- X/Z;
- fase;
- CNOT;
- Bell;
- medición;
- interferencia.

Hello X:

```
|0> -> H -> phase(pi) -> H -> |1>
```

Resultado histórico:

```
TEST: PASS
```

## Compact Core

Implementación C++17 bit-packed basada en tableau de estabilizadores.

Dominio actual:

```
Clifford / stabilizer
```

Operaciones confirmadas:

```
H
S
X
Z
CNOT
CZ
medición Z proyectiva
```

No materializa un statevector completo de `2^n` amplitudes para esta familia.

### GHZ — hardware Android real

```
qubits=1000
state_bytes=257000
state_mib=0.245
GHZ=PASS
time_ms=8.909
```

### Cluster 2D — hardware Android real

```
32 x 32 = 1024 qubits
state_bytes=263168
state_mib=0.251
signature=PASS
time_ms=53.212
```

### Cluster 2D — GitHub Actions

```
32 x 32 = 1024 qubits
state_bytes=263168
signature=PASS
~10.206 ms
```

Estos resultados pertenecen únicamente al dominio Clifford/estabilizador.

No prueban simulación universal arbitraria de 1,000+ qubits.

---

# 3. Teletransportación cuántica — VERIFICADO

Se implementó el protocolo estándar de teletransportación de **estado** dentro de Compact Core.

Estados probados:

```
|0>
|1>
|+>
|->
|+i>
|-i>
```

Ramas de medición:

```
00
01
10
11
```

Resultado automatizado:

```
passed=24/24
compact_state_bytes=51
result=PASS
```

Esto verifica teletransportación del estado cuántico simulado dentro del dominio estabilizador.

No transporta materia.

Registro:

```
results/compact_quantum_teleportation_2026-10-14.md
```

---

# 4. Render visual gobernado por Compact Core — PROBADO EN HARDWARE REAL

Se conectó un estado Compact Core de 32 qubits con una escena visual.

Ruta:

```
Compact Core 32q
-> métricas derivadas del tableau
-> uniforms / parámetros visuales
-> renderer
```

Estado Compact Core:

```
544 bytes
```

Estado lógico total instrumentado aproximado:

```
608 bytes
```

La interacción táctil modifica un qubit del estado Compact y el estado resultante vuelve a alimentar la siguiente evolución visual.

Esto demuestra una conexión funcional entre el simulador cuántico y una salida visual.

No demuestra aceleración cuántica de gráficos.

---

# 5. Canvas vs GPU — PROBADO EN SAMSUNG A16 5G

## Canvas / CPU

Resultados observados:

### GENERATIVE — 6,000

```
17.0 FPS
60.42 ms/frame
```

### GENERATIVE — 24,000

```
~4.1 FPS
~252 ms/frame
```

### COMPACT -> VISUAL — 6,000

```
24.8 FPS
40.28 ms/frame
```

### COMPACT -> VISUAL — 24,000

```
~5.0–5.9 FPS
~171–210 ms/frame
```

Conclusión confirmada:

> El cuello de botella principal de esa etapa era la ruta Java/Canvas, no el tamaño del estado lógico.

## OpenGL ES 3.0

Se movió la reconstrucción masiva a GPU.

Ruta:

```
estado compacto
-> pocos uniforms
-> gl_VertexID
-> shaders
-> 1 draw call/frame
```

Resultado observado con **96,000 elementos**:

```
~60.5–64.6 FPS
~16.66–16.99 ms/frame
draw_calls/frame=1
```

Conclusión confirmada:

> El gran salto de rendimiento provino de GPU + batching + reconstrucción local, no de hardware cuántico.

---

# 6. Prueba gráfica procedural extremadamente pequeña — PROBADO EN HARDWARE REAL

Se construyó una escena gráfica OpenGL ES generada completamente mediante shader, sin texturas de imagen ni modelos externos.

APK observada:

```
~1.03 MiB
```

Primera captura física:

```
display=2340x1080
FPS=37.7
frame=26.50 ms
image textures=0
mesh assets=0
draw calls/frame=1
```

Después se añadió resolución interna adaptativa + reescalado + sharpen.

Captura posterior:

```
display=2340x1080
internal=1685x778
scale=72%
FPS=49.5
frame=20.21 ms
```

Lectura correcta:

> Se demostró que una escena visual compleja puede reconstruirse desde muy poco contenido almacenado.

No se demostró que una ciudad AAA completa pueda representarse en ~1 MB.

---

# 7. Ciudad procedural navegable — PROBADO CUALITATIVAMENTE EN HARDWARE REAL

Se evolucionó la demo gráfica hacia navegación libre:

```
lado izquierdo -> movimiento
lado derecho -> cámara
```

Se probaron varias arquitecturas.

## Etapa A — ciudad por shader / ray marching

Funcionó como espacio 3D navegable, pero mostró límites claros:

- lectura urbana pobre;
- edificios demasiado próximos;
- sensación de corredor;
- geometría poco reconocible;
- costo alto por píxel;
- reducción de resolución interna necesaria.

Captura física observada:

```
FPS ~= 43.8
frame ~= 22.81 ms
internal=1170x540
scale=50%
```

## Etapa B — calidad procedural más alta

Se añadieron:

- más detalle de fachadas;
- más distancia;
- calles/cruces;
- cielo/nubes;
- reflejos aproximados;
- pájaros sintéticos;
- más pasos de ray marching.

Captura física observada:

```
FPS ~= 20.5
frame ~= 48.84 ms
internal=1170x540
scale=50%
```

Conclusión:

> Aumentar el detalle manteniendo toda la ciudad como ray marching elevó demasiado el costo por píxel.

Además, aunque subió la calidad local, el resultado todavía no tenía suficiente **coherencia urbana**.

## Etapa C — cambio a geometría rasterizada real

Se reemplazó el enfoque principal de ciudad ray-marched por:

- cubo 3D real reutilizable;
- instancing GPU;
- depth buffer;
- edificios compuestos por varias piezas;
- calles/banquetas;
- colisiones AABB;
- cielo procedural;
- pavimento húmedo aproximado;
- luces;
- aves como sprites/points;
- regeneración de bloques alrededor del jugador.

GitHub Actions:

```
run #60: success
commit: e6551ab1ea6bd4bc8cea3c400be89ddf6f06e049
```

Corrección del shader de aves:

```
run #61: success
commit: cbef11d4c8168fdcc4e0f68cb88b592b5d70cf23
```

Observación cualitativa de la prueba física posterior:

- la calidad geométrica mejoró;
- la escena seguía sin sentirse como una ciudad coherente;
- las aves no resultaban visualmente perceptibles en condiciones normales.

Conclusión actual:

> El renderer ya puede dibujar geometría urbana más eficientemente, pero el generador todavía necesita una capa de urbanismo jerárquico.

La siguiente arquitectura urbana debería seguir:

```
mundo
-> distritos
-> red vial
-> manzanas
-> parcelas
-> edificios
-> fachadas
-> detalle urbano
```

No basta con:

```
grid
-> bloque
-> edificios aleatorios
```

---

# 8. Sincronización por red — IMPLEMENTADA / PARCIALMENTE PROBADA

## Internet connectivity

En Android físico se observó:

```
internet=PASS
http=204
```

## Cliente relay

Existe cliente TCP con:

- host/IP;
- puerto;
- room;
- device name;
- estado Compact;
- eventos;
- contadores TX/RX.

Se observó también un caso de:

```
ECONNREFUSED
```

al intentar conectar contra una IP/puerto donde no había un relay escuchando.

Esto confirmó que la conectividad general funcionaba y que el error era del endpoint TCP, no de acceso a Internet.

## Relay embebido

Se añadió la capacidad de iniciar un relay TCP desde el propio teléfono:

```
START RELAY HERE
```

para pruebas locales / LAN.

## Sincronización entre dos dispositivos

Existe una pantalla donde:

```
HOST
JOIN
touch event
object count
RESET
```

se transmiten como eventos mínimos.

El protocolo evita enviar la escena completa.

Por ejemplo:

```
T <x> <y> <qubit>
C <count>
R
```

La interfaz fue corregida para respetar insets de Android y no quedar debajo de cámara/status/navigation bars.

Estado actual:

- código compilado y expuesto;
- build CI verificado;
- todavía falta una prueba física completa entre **dos teléfonos reales** para declarar esta ruta completamente validada extremo a extremo.

---

# 9. Launchers Android — IMPLEMENTADOS / BUILDS VERIFICADOS

## Performance Launcher

Hace preflight de:

- RAM;
- Battery Saver;
- estado térmico;
- temperatura;
- refresco de pantalla.

Puede abrir una app y cerrar su propio proceso para liberar sus recursos.

No:

- requiere root;
- modifica apps;
- toca anti-cheat;
- fuerza governors;
- overclockea CPU/GPU.

## HOME Launcher

Puede registrarse como launcher HOME de Android.

Incluye:

- reloj/fecha;
- RAM;
- temperatura;
- estado térmico;
- cajón de apps;
- búsqueda;
- apertura de aplicaciones;
- accesos a ajustes.

---

# 10. Windows x64 — VERIFICADO

Existe runtime nativo de Windows x64 construido con MSVC.

Smoke tests históricos:

```
mundo_x.exe hello
mundo_x.exe run examples\bell.x
mundo_x.exe cluster 4 4 --dense
mundo_x.exe cluster 32 32
```

Resultado:

```
PASS
```

---

# 11. Transformer X — IMPLEMENTADO / EXPERIMENTAL

Existe un Transformer PyTorch real con pesos entrenables.

Representación:

```
mode_id
+
z = [Re, Im]
-> TransformerEncoder
-> delta z
-> nuevo estado
```

Compact Core y Transformer X todavía son módulos separados.

No existe todavía evidencia de que Transformer X mejore el Compact Core ni de que aporte ventaja cuántica.

---

# 12. Primera representación exacta no-Clifford con T — VERIFICADA EN CI

Se añadió una descomposición exacta:

```
T = alpha I + beta Z
```

con:

```
alpha = (1 + exp(i*pi/4)) / 2
beta  = (1 - exp(i*pi/4)) / 2
```

Cada rama sigue siendo un estado Clifford/estabilizador representado por Compact
Core, pero la suma ponderada completa puede ser un estado no-Clifford.

No se usa poda, truncación ni fusión aproximada.

Resultados verificados en Linux/GitHub Actions:

```
3 qubits / 1 T
branches=2
fidelity=1.000000000
branch_tableaus=PASS
result=PASS
```

```
4 qubits / 4 T
branches=16
fidelity=1.000000000
branch_tableaus=PASS
result=PASS
```

```
4 qubits / 8 T
branches=256
fidelity=1.000000000
branch_tableaus=PASS
result=PASS
```

Windows x64 también pasó los casos de 4 y 8 puertas T.

Registro completo:

```
results/exact_t_decomposition_2026-10-14.md
```

La APK Android más reciente ya expone un botón para ejecutar este experimento con
qubits y cantidad de puertas T configurables dentro de los límites de validación.

Importante:

> esto demuestra corrección exacta para los circuitos probados, no eficiencia
> escalable general. Sin reducción de ramas, el costo actual crece como `2^T`.

---

# 13. Qué está científicamente demostrado hoy

Sí está demostrado dentro del proyecto:

1. Compact Core representa y evoluciona estados Clifford/estabilizadores de forma compacta.
2. GHZ y cluster 2D pasan las firmas implementadas.
3. Las mismas ideas funcionan en Android físico y en runtime nativo.
4. La teletransportación de estado estabilizador pasa 24/24 pruebas.
5. Una suma ponderada exacta de ramas Compact Core reproduce circuitos pequeños con puerta T y fidelidad 1.0 en los casos probados.
6. Las ramas estabilizadoras de esos experimentos también validan individualmente contra Dense Core.
7. Un estado Compact puede controlar una escena visual.
8. La reconstrucción en GPU puede generar cantidades muy grandes de elementos con estado lógico pequeño.
9. GPU + batching eliminó el cuello de botella observado en Canvas.
10. Una escena procedural puede ocupar muy poco almacenamiento.
11. La navegación 3D y la generación urbana procedural son funcionales como prototipos.
12. El cambio de ray marching a geometría rasterizada/instanciada es técnicamente funcional y compila correctamente.

No está demostrado:

1. computación cuántica universal compacta;
2. reducción eficiente general del crecimiento `2^T` para circuitos no-Clifford;
3. representación compacta general de magic states arbitrarios;
4. ventaja cuántica física;
5. que Mundo X supere a hardware cuántico real;
6. que el renderer supere Unreal Engine;
7. que una ciudad AAA pueda almacenarse en ~1 MB;
8. que Compact Core sea la causa de mejoras de FPS;
9. que las aves/reflejos actuales alcancen calidad visual de juego AAA;
10. coherencia urbana comparable con una ciudad diseñada profesionalmente;
11. sincronización completa de dos teléfonos en una prueba física extremo a extremo.

---

# 14. ¿Es tecnología cuántica?

## Sí, en una parte concreta

**Quantum Core y Compact Core son software de computación/simulación cuántica.**

Representan estados y operaciones cuánticas matemáticamente válidas.

Compact Core explota la estructura especial de estados Clifford/estabilizadores
para evitar almacenar un statevector denso completo.

Ahora existe además una capa experimental que representa exactamente estados
no-Clifford pequeños como suma de ramas estabilizadoras tras aplicar puertas T.

En ese sentido, es correcto describir esa parte del proyecto como:

> **tecnología de software cuántico / simulación cuántica compacta y experimental no-Clifford**

## No, en el sentido de hardware cuántico

Actualmente Mundo X:

- no contiene qubits físicos;
- no usa una QPU;
- no muestra ventaja cuántica de hardware;
- corre sobre CPU/GPU convencional.

## El renderer no se vuelve cuántico por usar Compact Core

Cuando Compact Core alimenta parámetros visuales, existe un vínculo funcional:

```
estado cuántico simulado
-> parámetros
-> renderer clásico GPU
```

pero la GPU sigue haciendo gráficos clásicos.

Por tanto, el proyecto completo es mejor descrito hoy como:

> **arquitectura híbrida de software: simulación cuántica compacta + experimentos no-Clifford + computación clásica/GPU/red**

---

# 15. Frontera científica principal

La frontera cambió.

Antes era:

```
¿podemos representar T exactamente?
```

Ahora esa prueba exacta existe para circuitos pequeños.

La nueva frontera es:

```
Clifford/stabilizer
        |
        v
T exacta por suma de ramas
        |
        v
2^T ramas
        |
        v
¿podemos reducir/factorizar/representar esas ramas
sin perder exactitud ni esconder el mismo costo?
```

Ese es ahora el problema principal.

Hasta demostrar una mejora reproducible sobre el crecimiento ingenuo `2^T`, no
debe afirmarse computación cuántica universal compacta.

---

# 16. Estado actual

```
DATE: 2026-10-14
STATUS: ACTIVE EXPERIMENTAL
LATEST VERIFIED ANDROID BUILD: GitHub Actions run #68
LATEST VERIFIED ANDROID COMMIT: 370686361d8ca6cf5b773a2c7a3d762f5e1685b5
LATEST VERIFIED T TEST WORKFLOW: GitHub Actions run #65
LATEST VERIFIED WINDOWS T TEST: GitHub Actions run #9
```

El archivo `PROJECT_STATUS_2026-10-14.md` queda preservado como snapshot histórico de una etapa anterior.

Este documento es el estado funcional más actualizado del día.
