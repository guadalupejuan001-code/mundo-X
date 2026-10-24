# Ether Render X — observación espacial 8K (24 octubre 2026)

Módulo C++17 opcional, aislado de la ABI de EtherRenderer (64 cuerpos), el puente MIL Core y Solar/Android. No representa aceleración cuántica física ni modifica main.

## Diseño
- 4.096 esferas con ID, geometría, material y orden exacto de profundidad.
- Proyección conservadora de esferas por regiones; tablas de candidatos por tile y orden por cota de distancia.
- Oráculo exhaustivo independiente del filtrado espacial para ID, profundidad y RGBA.
- Full frame en flujo: observa píxeles sin reservar un framebuffer gigante, conserva hash de ID, RGBA y bits de profundidad.
- Observación dispersa: emite solo superficies donde las huellas proyectadas pueden contener cuerpos. El fondo queda definido por una regla, sin generar previamente toda la imagen. Esta observación **no es la composición completa ni una tasa real de pantalla**.

## Reproducir

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
    cmake --build build --target test_ether_render_spatial
    ctest --test-dir build -R '^ether_render_spatial$' --output-on-failure
    c++ -std=c++17 -O3 -I native/include tools/benchmark_ether_render_spatial.cpp -o /tmp/ether-spatial
    /tmp/ether-spatial 7680 4320 4096 8
    /tmp/ether-spatial 7680 4320 4096 16
    /tmp/ether-spatial 7680 4320 4096 32
    /tmp/ether-spatial 7680 4320 4096 64

## Resultados locales antes de PR

G++ 14.2.0, Linux x86_64 con O3. Se verificaron 97 escenas aleatorias completas de 320×180, casos de cámara dentro de esfera, empates de profundidad, coincidencia de huellas y 2.048 muestras a 8K con 4.096 esferas; 10.071.016 comprobaciones, SPATIAL_EXACT_PASS.

Escena 8K: 7680×4320 = 33.177.600 píxeles; máximo exhaustivo 135.895.449.600 intersecciones.

| Tile | Pruebas efectivas | Hash final |
| --- | ---: | --- |
| 8 | 7.091.523 | 8289019075456906279 |
| 16 | 9.349.565 | 8289019075456906279 |
| 32 | 14.921.083 | 8289019075456906279 |
| 64 | 30.443.411 | 8289019075456906279 |

Tiempos host: observación dispersa aproximadamente 150–180 ms; todos los píxeles con hash, entre 1,06 y 1,16 segundos. Son valores de ejecución CPU, **no 120 FPS 8K** ni resultado Samsung. La exactitud de los 33,2 millones de píxeles fue verificada entre tamaños de tile mediante hash; el oráculo exhaustivo a 8K cubrió posiciones muestreadas, no cada píxel.

## Promoción pendiente
- Comparar con el prototipo local previo (~40 ms) sobre la misma máquina: ese código no estaba publicado en main.
- Integrar en el renderizador de Android con composición real de fondo, profundidad, copia de GPU y presentación; medir FPS y memoria en Samsung.
- Mantener MIL, Solar y el renderer original intactos hasta comprobar no regresión.
