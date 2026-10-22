# Ether Render X v0.2

Ether Render X is the new canonical rendering core of Mundo X, driven directly from XASM. It is still isolated from the production Solar/GLES path until it matches those capabilities and passes the same exactness gates.

## Principle

> Resolve what can affect a screen region before asking individual pixels.

Bodies exist once. Screen regions contain only candidate IDs. Pixels trace those candidates, not the whole world.

## Pipeline

```text
XASM world
  -> camera
  -> conservative region planner
  -> candidate masks
  -> front-to-back conservative ordering
  -> per-pixel rays
  -> nearest exact sphere hit
  -> simple surface shading
  -> framebuffer hash / pixel observations
```

## Implementation

- `native/include/mundo_x/ether_renderer.hpp`
- `native/tests/rule_renderer.cpp`
- `native/tests/xasm_renderer.cpp`
- `examples/ether_render.x`
- `docs/ether-render-research-training-2026-10-21.md`

## XASM commands

```text
render <width> <height> <tile>
render_camera <eyeX> <eyeY> <eyeZ> <targetX> <targetY> <targetZ> <fovDeg>
render_body <id> <x> <y> <z> <radius> <r> <g> <b>
render_clear
render_plan
render_train
render_frame
render_pixel <x> <y>
render_assert_exact
render_info
```

`render_train` tests candidate region sizes on the executing machine, preserves only the fastest measured median, and then verifies the selected path against the brute-force oracle. It does not train neural-network weights.

## Run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --target mundo_x test_rule_renderer test_xasm_renderer -j 2
./build/mundo_x run examples/ether_render.x
ctest --test-dir build -R '^(rule_renderer|xasm_renderer)$' --output-on-failure
```

## Exactness contract

`render_assert_exact` renders through both the optimized candidate path and a brute-force all-bodies path. Any different body id, selected depth or RGBA value fails.

The randomized native oracle also checks 100 generated 20-body scenes. Tile training must preserve the same frame hash and finish with zero brute-force mismatches.

## Current scope

v0.2 implements a perspective camera, up to 64 spherical bodies, conservative region projection, region candidate masks, exact nearest-sphere intersection, deterministic direct lighting, front-to-back lower-bound pruning, framebuffer/pixel observations, brute-force exactness validation, work counters, and local tile-size training.

It does not yet implement Solar textures/materials, rings, atmospheres, eclipses, mirrors, arbitrary meshes or an Android GPU backend. Existing production renderers remain adapters/references until those functions are migrated.

## Promotion rule

Ether Render X enters a production path only when it covers that path's required features, returns zero differences where exactness is required, and reduces or preserves total cost on the same target hardware.

## v0.3 — observación MIL Core y eficiencia

`native/include/mundo_x/ether_mil_bridge.hpp` conecta el backend **ReducedExactTRepresentation**
de MIL Core de 1 a 1.000 qubits con `EtherRenderer`. La conexión no crea
una segunda instancia ni duplica el estado: consulta observables Pauli
normalizados de MIL Core mediante máscaras multipalabra y aplica su valor al
material del cuerpo seleccionado. La observación es de solo lectura: no mide,
no colapsa estados, no consume RNG y no construye un vector de `2^n` amplitudes.

El material se calcula como `gain = 1 + strength × clamp(expectation, -1, 1)`,
con intensidad de 0 a 1 y color final limitado a rango visible. Solo cambia
la apariencia; las coordenadas, radios y máscaras conservadoras permanecen iguales.
Las expectativas se preparan de manera transaccional fuera del bucle de píxeles,
la norma se consulta una sola vez por actualización y los operadores Pauli
duplicados comparten resultado. Las puertas T, T†, fases arbitrarias y
controladas invalidan la caché; una cámara en movimiento no altera el estado
MIL y no requiere repetir consultas cuánticas.

Nuevos comandos XASM, además de los ya existentes:

```text
backend reduced
qubits 1000
h 999
render 96 54 16
render_body 0 0 0 0 1.8 .35 .55 .75
render_bind 0 X 999 0.75
render_quantum 0
render_frame
t 999
render_frame
render_assert_exact
tdg 999
phase 999 .37
cphase 999 0 .23
render_unbind 0
render_bind_pauli 0 <cadena-Pauli-de-longitud-n> 0.75
```

`render_bind` vincula una componente local X/Y/Z, incluso q999.
`render_bind_pauli` admite cadenas de Pauli con múltiples qubits y signos,
por ejemplo correlaciones entre q0 y q999. La longitud debe coincidir con
el registro; índices inválidos y fuentes no reducidas se rechazan. 
`render_quantum` informa la expectativa observada sin medir.

### Optimización de CPU y límites

Se precalculan una vez por planificación las posiciones relativas y los
cuadrados de radios de las esferas. Un solo algoritmo de sombreado sirve al
recorrido optimizado y al oráculo exhaustivo, evitando discrepancias entre
sus reglas de iluminación. La luz direccional está normalizada una vez.
Cualquier ahorro de latencia debe medirse en el mismo hardware y escena;
un menor número de operaciones no demuestra por sí solo más FPS.

```sh
./build/mundo_x run examples/ether_mil_render.x
ctest --test-dir build -R '^(rule_renderer|xasm_renderer|ether_mil_renderer|reduced_1000)$' --output-on-failure
```

Las pruebas verifican estado de 1.000 qubits, T/T† reversible, fase controlada
sobre índices extremos, seguimiento de un observable sin medición, cambios de
imagen, cero diferencias con el oráculo, restauración, entradas inválidas y
estabilidad de candidatos. Es una simulación por CPU; **no hay aceleración
cuántica física ni backend GLES propio de Ether Render X** todavía.
Se mantienen los renderizadores de producción hasta disponer de paridad funcional
y una comparación de rendimiento favorable.
