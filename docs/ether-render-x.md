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
