# Actualización integral: estado y criterios de aceptación

Esta rama **no** reemplaza el renderizador 8K ni el núcleo de producción.
Incluye una primera base física 2D determinista independiente, con gravedad
uniforme, suelo y colisiones de discos con impulsos. Es deliberadamente limitada:
no es física 3D, N-body, fluidos, MHD ni simulación cuántica de fuerzas.

## Validación pendiente
- Compilar y ejecutar `native/tests/physics_world_smoke.cpp` con C++17.
- Añadir subpasos, barrido continuo para colisiones rápidas y tests de energía.
- Medir la RAM, p50/p95 del paso físico y reproducibilidad con 1, 1.000 y 100.000 cuerpos.
- Integrar la física en la escena solo tras verificar equivalencia de estados y
  que el coste de la actualización sea aceptable.

## Renderizador
- Línea base: mismo núcleo, escena, cámara, shaders y resolución, GPU identificada.
- Medir tiempo GPU real y p50/p95, FPS sostenidos, memoria y temperatura.
- Ensayar caché por revisión de observables y optimizaciones de regiones.
- Comprobar igualdad de imagen y picking antes de activar una ruta optimizada.
- No atribuir mejoras de FPS a InfiniteCore sin medición A/B.

La rama queda aislada hasta pasar pruebas. No modifica `main`.
