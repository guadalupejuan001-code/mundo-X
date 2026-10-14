# Android visual benchmark — Samsung A16 5G — 2026-10-14

Este documento registra mediciones observadas en hardware Android real para el
experimento visual de Mundo X.

## Dispositivo

- dispositivo reportado: **Samsung A16 5G**
- plataforma: Android arm64
- aplicación: Mundo X Lab
- renderer de esta iteración: Java/Android Canvas
- build con acoplamiento Compact Core -> visual: GitHub Actions run **#26**
- commit de implementación: `52e64a88f7b1ff31ea75e3e5c548ef88a7d5a2d7`

Estas mediciones provienen de capturas reales del dispositivo. No constituyen
todavía un benchmark A/B controlado de laboratorio.

---

## Baseline generativo — 6,000 objetos

Captura observada:

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

Interpretación:

- estado lógico explícito instrumentado: **64 bytes**
- PSS total del proceso: **105,660 kB**
- rendimiento observado: **17.0 FPS**
- tiempo observado: **60.42 ms/frame**

---

## Baseline generativo STRESS — 24,000 objetos

Captura observada:

```
mode=GENERATIVE
objects=24000
fps=4.1
frame=252.03 ms
explicit_scene_state=64 bytes
process_PSS=175070 kB
feedback=3.940
touch=(0.65, 0.29)
```

Interpretación:

- el estado lógico explícito instrumentado permaneció en **64 bytes**
- el costo de render/reconstrucción creció aproximadamente con el número de elementos
- el cuello de botella observado fue el trabajo por frame, no el tamaño del estado lógico

---

## Compact Core -> Visual — 6,000 objetos

Captura observada mientras el usuario movía el dedo en círculos sobre la escena:

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

X=0.489
Z=0.521
Y=0.267
phase=0.344
diagX=0.625
diagZ=0.625
corr=0.561
hash=0.121

feedback=3.940
touch=(0.51, 0.56)
```

La ruta ejecutada es:

```
Compact Core (32 qubits)
-> evolución Clifford
-> firma derivada del tableau
-> parámetros visuales
-> escena
```

El estado compacto de 32 qubits ocupa **544 bytes**. Sumado al estado global
instrumentado del renderer, la pantalla reportó **608 bytes de estado lógico
explícito**.

La captura muestra además `step=1067`, por lo que el Compact Core ya había
evolucionado más de mil pasos durante la sesión.

---

## Comparación observada a 6,000 objetos

| Medición | GENERATIVE | COMPACT -> VISUAL |
|---|---:|---:|
| Objetos | 6,000 | 6,000 |
| FPS observados | 17.0 | 24.8 |
| Tiempo por frame | 60.42 ms | 40.28 ms |
| Estado lógico instrumentado | 64 B | 608 B |
| PSS del proceso | 105,660 kB | 108,494 kB |

Diferencias entre estas dos capturas:

- FPS: **+45.9%** en la captura de `COMPACT -> VISUAL`
- tiempo por frame: **-33.3%**
- PSS total: **+2,834 kB** (~**+2.7%**)
- estado lógico instrumentado: 64 B -> 608 B, todavía por debajo de 1 KiB

## Límite de la conclusión

Estas cifras **no prueban todavía** que Compact Core sea la causa de la mejora de
FPS. Las capturas se tomaron en momentos distintos, con interacción táctil y sin
controlar temperatura, frecuencia CPU/GPU, carga del sistema ni duración de la
muestra.

Lo que sí queda demostrado por esta iteración es:

1. el estado de Compact Core participa directamente en la generación visual;
2. la interacción táctil perturba el estado compacto y los siguientes frames;
3. el costo de mantener el Compact Core de 32 qubits no impidió una escena
   interactiva de 6,000 elementos en el Samsung A16 5G;
4. en la captura observada, `COMPACT -> VISUAL` mostró mejor FPS y menor tiempo
   por frame que la captura generativa previa;
5. hace falta una prueba A/B controlada antes de atribuir causalidad o declarar
   una ventaja de rendimiento.

## Próxima prueba recomendada

Medir en el mismo arranque de la app, sin tocar la pantalla durante cada muestra:

```
GENERATIVE       6000 objetos, 15 s
COMPACT->VISUAL  6000 objetos, 15 s
GENERATIVE      24000 objetos, 15 s
COMPACT->VISUAL 24000 objetos, 15 s
```

Registrar para cada caso:

- FPS medio/estable;
- ms/frame;
- PSS;
- temperatura;
- batería;
- duración exacta;
- estado lógico reportado.

Eso permitirá separar una observación prometedora de una mejora reproducible.
