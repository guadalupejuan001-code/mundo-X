# Ether Render X — GPU unificado por visibilidad (etapa 1)

## Estado: implementación y validación, no sustitución final

En esta etapa, el **mismo código nativo** `EtherRenderSpatial` que sustenta
`EtherSurfaceBuffer` genera el índice espacial de Solar. La GPU conserva
las operaciones de imagen completas, sin transferir al dispositivo cada uno
de los 33.177.600 píxeles desde un trazador CPU.

- **Núcleo geométrico:** `native/include/mundo_x/ether_render_spatial.hpp`, con
  `set_camera_basis` y `tile_masks32`. La cámara acepta la orientación
  existente de Solar y no reescribe su simulación.
- **JNI mínima:** `solar-android/app/src/main/cpp/ether_visibility_jni.cpp`;
  expone los candidatos derivados de esferas englobantes (incluye radio de
  anillos y atmósferas).
- **Buffer personalizado GPU:** textura R32UI de una máscara por tile
  de 128×128; tamaño para 7680×4320 = 60 × 34 × 4 = **8160 bytes**.
  El Java reutiliza la memoria del buffer y la textura por fotograma.
- **Etapa visual:** se mantiene el shader GLSL de Solar con texturas NASA,
  PBR, anillos, atmósferas, corona, órbitas, eclipses, profundidad y entrada
  atmosférica. **No se omiten esferas ni pruebas de eclipse.** Solo se evita
  trabajo de atmósferas y anillos para tiles fuera de su envolvente.
- **Garantía conservadora:** las regiones se dilatan un tile para proteger
  errores de redondeo y objetos subpíxel. El oráculo C++ prueba cada rayo de
  escenarios aleatorios y verifica que ninguna envolvente intersectada esté
  ausente del índice.
- **Compatibilidad:** si GLES/JNI no puede crear o cargar el índice,
  Solar vuelve al materializador de referencia. El PR no cambia `main`.

## Decisión de rendimiento

No confundir: menor número de candidatos no demuestra mayor FPS. Medir el
tiempo de preparación nativa, subida de la textura, GPU, swap/presentación y
FPS sostenidos en el Samsung A16 antes de retirar el camino de referencia.
Un buffer CPU comprimido con 15,78 MB no sustituye directamente el
framebuffer/color/depth necesario para presentar una imagen GLES.

El atlas puede ser más eficiente cuando la escena contiene pocos efectos
visibles, pero puede perder eficiencia en vista de cámara dentro de
atmósferas densas o cuando todo el cielo está cubierto por efectos.
`SolarRenderer.setEtherVisibility(boolean)` permite comparación A/B real.

## Alcance de "motor completo"

Conectar selección espacial y presentación es la primera integración.
La ruta GPU realiza aún el sombreado avanzado de Solar mediante sus
funciones actuales; por tanto **Ether no ha sustituido todavía toda la
ingeniería de materiales y física visual**. Reescribir esos módulos sin
pruebas de paridad perdería capacidades. Su migración progresiva debe
cumplir comparaciones RGBA y profundidad, benchmarks emparejados y
calidad del cielo/atmósfera/rings/entrada.

## Reproducir

```bash
c++ -O3 -std=c++17 -Wall -Wextra -I native/include tools/test_ether_gpu_visibility.cpp -o /tmp/ether-gpu
/tmp/ether-gpu
python3 tools/cosmic_source_provenance.py --verify .
gradle -p solar-android :app:assembleDebug :app:assembleDebugAndroidTest
```

El test instrumentado `ether_gpu_spatial_visibility_exact_rgba` captura
cuatro escenas reales (sistema, Tierra, Saturno, Luna) con Ether activo e
inactivo. Exige imágenes RGB idénticas. La integración de shaders no se
declara aprobada hasta obtener los resultados de Android.

En 8K, el shader sigue generando 33,18 millones de fragmentos al pedir
una imagen completa. Ningún índice puede eliminar ese trabajo de salida
si el objetivo es presentar un RGBA completo.
