# Ether Surface Buffer — diseño personalizado 8K / 4.096 superficies (2026-10-24)

## Propósito y límites

La decisión de Mundo X es **no construir una imagen densa antes de observarla**. El módulo opcional `EtherSurfaceBuffer` conserva esta separación de responsabilidades, sin reemplazar `EtherRenderer` original (64 cuerpos), MIL Core ni Solar. Es un observador CPU de esferas; no presenta imágenes mediante una GPU ni demuestra 120 FPS a 8K.

**Dos modos**:

- `prepare_lazy()`: construye únicamente la proyección/candidatos, sin ejecutar un ciclo para generar cada píxel. Las lecturas `at(x,y)` calculan la intersección, color y profundidad en el momento de consulta.
- `capture_surfaces()`: calcula los **píxeles visibles** contenidos en intervalos conservadores, almacenando `uint16_t` para ID y `uint32_t` para RGBA. Las filas contienen intervalos compactos; no se almacenan píxeles vacíos ni profundidad por píxel. Un `at(x,y)` recupera el ID/color guardado y reconstruye solo la profundidad de esa esfera; el fondo es una regla analítica.

El mapa de candidatos por fila ahora intersecta analíticamente la silueta cónica de la esfera con la fila de pantalla; solo recorre los intervalos potencialmente visibles. Cuando las condiciones de flotante no son seguras, conserva el rectángulo original. La revisión de escena invalida el buffer al cambiar cámara, geometría, material o tile.

## Exactitud verificada

- 97 escenas de estrés previas de `ether_render_spatial` siguen pasando.
- Nueva prueba `ether_surface_buffer`: 609.167 comprobaciones en 20 escenas aleatorias más escenarios especiales y 8K/4.096 objetos; compara color, profundidad y ID contra la ruta exhaustiva; casos sin cuerpos, cámara dentro de esfera, ID 4095 y cambios que invalidan caché.
- ASan+UBSan sobre la nueva prueba: pasa en el entorno local Linux.
- Los hashes de fotograma completo siguen siendo idénticos entre tiles 8, 16, 32 y 64 en la escena benchmark: `8289019075456906279`.

## Bench local — Linux x86-64, g++ 14.2, -O3

Escena fija: 7680×4320 (33.177.600 píxeles), 4.096 esferas. Comparar tiempos **solo en el mismo host y con la misma carga**.

| Medida | tile 16 |
| --- | ---: |
| Preparar mapa simbólico | 6,56 ms |
| Capturar buffer de superficies exactas | 162,31 ms |
| Fotograma completo CPU (incluye fondo y hash) | 1.100,76 ms |
| Píxeles en intervalos conservadores | 2.990.885 |
| Píxeles realmente visibles | 2.521.229 |
| Intervalos de superficies | 79.121 |
| Buffer compacto residente | **15.777.626 bytes** |
| Buffer RGBA8 convencional 8K | **132.710.400 bytes** |
| Reducción del payload de imagen frente a RGBA8 | **88,1%** |

La estructura completa del renderizador espacial consume memoria adicional (tiles, candidatos, filas, objetos); la cifra de 15,8 MB **es solo el buffer de superficies**, no el RSS total ni memoria de GPU. Capturar superficies sigue recorriendo cada pixel visible y por eso **no es un algoritmo de coste constante o independiente del número de píxeles observados**. Producir un fotograma completo de 8K sigue requiriendo escribir sus 33,18 M de píxeles. La preparación simbólica evita ese trabajo hasta que se solicite.

## Reproducir

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --target test_ether_render_spatial test_ether_surface_buffer -j2
ctest --test-dir build -R '^(ether_render_spatial|ether_surface_buffer)$' --output-on-failure
c++ -O3 -std=c++17 -I native/include tools/benchmark_ether_surface_buffer.cpp -o /tmp/mx-surface
/tmp/mx-surface 7680 4320 4096 16
```

## Pendiente

- Comparar con el prototipo previo de ~40 ms, cuyo código no está publicado, en un protocolo A/B idéntico.
- Adaptar buffers a GLES/Vulkan y composición de fondo, presentaciones, profundidad y oclusión en Android; medir memoria y FPS del Galaxy/Samsung real.
- No fusionar a `main` mientras falte la verificación de Android/producción.
## Verificación completa de la imagen 8K

El benchmark recorre **solo durante la validación** los 33.177.600 píxeles y reconstruye el hash de ID, RGBA y bits de profundidad desde el buffer compacto. En la prueba Linux local: hash compacto `8289019075456906279` = hash del renderizador completo; comprobación de exportación 8K ~1.237 ms (1,237 segundos). Este coste O(W×H) se paga únicamente al pedir el fotograma completo o al validarlo y **no** durante la preparación simbólica ni al consultar píxeles individuales.
