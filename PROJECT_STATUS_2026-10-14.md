# Mundo X — estado del proyecto — 2026-10-14

> **SNAPSHOT HISTÓRICO.** Este archivo conserva el cierre de una etapa anterior del mismo día. El proyecto fue reanudado posteriormente. Para el estado funcional actualizado, usar `TESTED_FUNCTIONAL_STATUS_2026-10-14.md`.

Este archivo congela el estado real del proyecto al cierre de aquella etapa.

## 1. Núcleos existentes

### Quantum Core
Motor denso de referencia para estados cuánticos pequeños.

Soporta, entre otros:

- Hadamard;
- Pauli X/Z;
- fase S;
- CNOT;
- CZ;
- medición;
- interferencia;
- Bell.

### Compact Core
Representación bit-packed mediante tableau de estabilizadores.

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

No materializa el statevector completo de `2^n` amplitudes para los estados
compatibles con esta representación.

## 2. Resultados de escala

### GHZ Android

```
qubits=1000
state_bytes=257000
state_mib=0.245
GHZ=PASS
time_ms=8.909
```

### Cluster 2D Android

```
rows=32
cols=32
qubits=1024
state_bytes=263168
state_mib=0.251
signature=PASS
time_ms=53.212
```

## 3. Teletransportación cuántica de estado

Compact Core implementa el protocolo estándar de teletransportación de estado
para los seis estados estabilizadores de un qubit.

Se probaron los estados:

```
|0>
|1>
|+>
|->
|+i>
|-i>
```

y las cuatro ramas de medición:

```
00 01 10 11
```

Resultado CI:

```
passed=24/24
compact_state_bytes=51
result=PASS
```

Esto es teletransportación del **estado cuántico simulado**, no transporte de
materia.

## 4. Visualización gobernada por Compact Core

Se conectó un estado Compact Core de 32 qubits a parámetros visuales derivados
del tableau.

Ruta:

```
Compact Core
-> métricas de estado
-> parámetros visuales
-> escena
```

Estado lógico instrumentado:

```
Compact Core: 544 B
estado global adicional: ~64 B
total: ~608 B
```

## 5. Render Canvas — primer cuello de botella

Resultados observados en Samsung A16 5G:

### GENERATIVE, 6,000

```
17.0 FPS
60.42 ms/frame
64 B de estado lógico
```

### GENERATIVE STRESS, 24,000

```
4.1 FPS
~252 ms/frame
64 B de estado lógico
```

### COMPACT->VISUAL, 6,000

```
24.8 FPS
40.28 ms/frame
608 B de estado lógico
```

### COMPACT->VISUAL, 24,000

Capturas posteriores mostraron aproximadamente:

```
5.0–5.9 FPS
~171–210 ms/frame
```

El cuello de botella estaba en la reconstrucción/dibujo individual por
Java/Canvas.

## 6. Render GPU

Se creó una ruta OpenGL ES 3.0 donde la CPU entrega únicamente el estado compacto,
tiempo e interacción.

La GPU reconstruye los elementos mediante shaders y `gl_VertexID`.

Ruta:

```
Compact Core
-> 8 métricas
-> uniforms GPU
-> shader
-> 1 draw call
```

Resultado real en Samsung A16 5G:

### 96,000 elementos — baja estimulación

```
fps=64.6
frame=16.99 ms
draw_calls/frame=1
logical_state=608 B
```

### 96,000 elementos — feedback máximo

```
fps=60.5
frame=16.66 ms
draw_calls/frame=1
logical_state=608 B
feedback=4.000
```

Este es el resultado visual más fuerte de esta etapa.

La ganancia se explica por mover el render masivo a GPU y reducir el trabajo por
frame a una sola llamada de dibujo, no por una aceleración física cuántica.

## 7. Transformer X

Existe un Transformer experimental que opera sobre modos dispersos complejos:

```
mode_id + z(Re,Im)
-> Transformer
-> delta z
-> nuevo estado
```

Es una red neuronal real con atención y pesos entrenables.

Transformer X y Compact Core siguen siendo módulos distintos; todavía no existe
una fusión completa entre ambos.

## 8. Plataformas

### Android

Existe APK arm64 funcional con:

- laboratorio cuántico;
- Compact Core;
- cluster/GHZ;
- teletransportación;
- visual Canvas;
- visual GPU;
- Performance Launcher;
- launcher HOME.

### Windows x64

Existe ejecutable nativo de Windows construido con MSVC y validado en GitHub
Actions.

## 9. Launchers

### Performance Launcher

Hace preflight del dispositivo, abre una app y termina su propio proceso para
liberar RAM.

No:

- hace overclock;
- modifica apps;
- fuerza governors;
- requiere root.

### Android HOME launcher

Puede registrarse como HOME de Android e incluye:

- reloj/fecha;
- RAM/temperatura/estado térmico;
- búsqueda;
- cajón de aplicaciones;
- acceso a Settings y Mundo X.

## 10. Fronteras actuales

Mundo X todavía no implementa de forma compacta y general:

- T gate;
- magic states arbitrarios;
- circuitos universales no-Clifford;
- hardware cuántico físico;
- fusión completa Transformer X <-> Compact Core.

La siguiente frontera científica sería salir de Clifford/estabilizadores sin
volver a un statevector denso ni esconder el costo en otra estructura.

## 11. Estado de este snapshot

```
STATUS DEL SNAPSHOT: PAUSED / FROZEN
DATE: 2026-10-14
SUPERSEDED BY: TESTED_FUNCTIONAL_STATUS_2026-10-14.md
```

La etapa queda preservada con código, commits, builds, APKs, benchmarks y
resultados reales de hardware móvil.

Una futura aplicación posible mencionada por el creador es un **reloj cuántico**.
No está implementado en esta rama y queda solamente como dirección futura.
