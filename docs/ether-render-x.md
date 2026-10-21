# Ether Render X v0.1

Ether Render X is the first renderer implemented directly as a domain of Mundo X and driven from XASM. It does not replace Solar or the validated GLES renderer yet.

## Principle

> Resolve what can affect a screen region before asking individual pixels.

The brute-force oracle asks every body for every pixel. Ether Render X first projects each body's conservative bounding sphere into screen regions. Each region stores only a 64-bit candidate mask. Pixels trace only those candidates.

No copy of the world is created per region. Bodies exist once; regions contain only references by id.

## Pipeline

```text
XASM world
  -> camera
  -> conservative region planner
  -> candidate masks
  -> per-pixel rays
  -> nearest exact sphere hit
  -> simple surface shading
  -> framebuffer hash / pixel observations
```

The implementation is in:

- `native/include/mundo_x/ether_renderer.hpp`
- `native/tests/ether_renderer.cpp`
- `native/tests/xasm_renderer.cpp`
- `examples/ether_render.x`

## XASM commands

```text
render <width> <height> <tile>
render_camera <eyeX> <eyeY> <eyeZ> <targetX> <targetY> <targetZ> <fovDeg>
render_body <id> <x> <y> <z> <radius> <r> <g> <b>
render_clear
render_plan
render_frame
render_pixel <x> <y>
render_assert_exact
render_info
```

The render domain can exist without allocating qubits or a `VisualScene`.

Example:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --target mundo_x test_ether_renderer test_xasm_renderer -j 2
./build/mundo_x run examples/ether_render.x
ctest --test-dir build -R '^(ether_renderer|xasm_renderer)$' --output-on-failure
```

## Exactness contract

`render_assert_exact` renders the frame through both paths:

1. region candidate path;
2. brute-force all-bodies path.

It fails if any pixel differs in body id, exact selected depth or RGBA result. This is deliberately stricter than a visual tolerance.

The native randomized oracle also checks 100 generated 20-body scenes.

## Current scope

v0.1 implements:

- one perspective camera;
- up to 64 spherical bodies;
- conservative screen-region projection;
- region candidate masks;
- exact nearest sphere intersection;
- deterministic direct lighting;
- framebuffer and pixel observations;
- brute-force exactness oracle;
- work counters: candidate references/tests versus brute-force references/tests.

It does **not** yet implement the Solar materials, textures, rings, atmospheres, eclipses, mirrors or Android GLES execution. Those are later stages. This keeps the first structural claim measurable before replacing a production renderer.

## Promotion rule

Ether Render X is allowed to enter Solar only after a Solar-specific adapter proves zero candidate omissions and zero frame differences against Solar's full reference while reducing total work on the target path.
