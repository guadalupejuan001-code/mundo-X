# COSMIC X Solar 1.4 — Cine + fluidez UHD sin perder calidad

## Qué cambia

Esta versión extiende el observador Solar GLES3 **original** de Mundo X. No reemplaza Ether Core, no introduce Unity/Unreal ni mezcla fenómenos ficticios de MIL 100q con las órbitas. Las 23 posiciones, gravedad, mapas planetarios y materiales de entrada se mantienen en sus fuentes existentes.

### Modo Cine (activado de fábrica, reversible)

El control **Calidad → Cine** activa un tratamiento visual adicional:

- Atmósferas con un refuerzo suave del halo hacia la luz y la zona crepuscular, cuando el entorno del planeta tiene atmósfera modelada.
- Brillo solar y difracción **fotográficos** de coste acotado, subordinados a las comprobaciones de oclusión existentes. Esto no pretende representar rayos solares medidos o un instrumento óptico calibrado.
- Relieve aparente anti-filtrado en vistas muy cercanas a cuerpos sólidos. **No es topografía observacional ni un mapa altimétrico** y no afecta la esfera física utilizada para la colisión ni las coordenadas orbitales.
- Reflejos de partículas en anillos, sólo como variación del material con el ángulo de iluminación. **No es una simulación de las partículas**.
- Tonos de bruma estelar sutiles y tramado determinista sub-8-bit para atenuar las bandas de color sin parpadeo temporal.
- El modo original (`uCinematic=0`) conserva el shader previo sin añadir estos efectos. Los fixtures de referencia siguen validándose sin necesidad de modificar sus mapas NASA.

Este acabado **no** añade mapas topográficos de alta resolución, nubes meteorológicas observadas, magnetohidrodinámica, ni predicción de órbitas más precisa. Los parámetros ópticos continúan siendo aproximaciones gráficas.

### Vista rápida mientras mueves la cámara 4K/8K

Antes, Solar evaluaba los 23 cuerpos y los efectos del shader en **todos** los píxeles 4K/8K durante cada movimiento. Ahora, al arrastrar o pellizcar sobre la vista:

1. Se mantiene asignado el **framebuffer 3840×2160 / 2160×3840, o 7680×4320 / 4320×7680**, según el modo.
2. Durante el gesto, se usa un framebuffer **independiente de 960×540 o 540×960**, con la misma cámara, proporción y datos orbitales.
3. Se escala esa imagen sólo para presentarla en la pantalla.
4. Al soltar, un nuevo fotograma completo vuelve a usar la resolución nativa elegida.
5. Si se solicita **Guardar PNG**, la captura obliga a dibujar a la resolución completa aun durante un gesto. Los píxeles exportados nunca proceden del framebuffer reducido.
6. Si la GPU no puede reservar el framebuffer adicional, el modo opcional se desactiva internamente y se conserva el renderizado completo validado. La app no anuncia una resolución no asignada.

| Vista | Píxeles por escena original | Píxeles en movimiento | Relación de carga de fragmentos |
| --- | ---: | ---: | ---: |
| UHD 4K | 8.294.400 | 518.400 | 16× menos píxeles |
| UHD 8K | 33.177.600 | 518.400 | 64× menos píxeles |

**Estos valores son sólo conteos matemáticos de fragmentos**, no una mejora medida de FPS ni rendimiento garantizado. Persisten costes de draw calls, shaders, GPU, CPU, textura, transferencia, memoria, y composiciones. El consumo de RAM total tampoco se deduce únicamente de los 8 bytes/píxel de attachments.

El modo nuevo no cambia `SolarData`, `QuantumSolarBridge` ni los motores nativos. La vista interactiva se controla desde `SolarSurface` con temporización corta de 140 ms tras soltar y se destruye/libera con el contexto GL.

## Controles

En el menú **Calidad** y también en **Ajustes** hay interruptores independientes:

- **Cine · halo solar, relieve y color**: activa o desactiva la interpretación cinematográfica del shader.
- **Vista rápida mientras mueves 4K/8K**: mantiene la interfaz responsive cuando es posible y vuelve a fotograma UHD al detener el gesto.

Los valores se guardan en preferencias Android y se restauran al reabrir la app o girar el dispositivo.

## Relación con MIL100 × Ether Render X

El PR #45 del repositorio introdujo un visor separado de 100 qubits lógicos GHZ, acoplados como observables de materiales a 100 marcadores 3D de Ether. **Solar sigue utilizando su puente Ether/InfiniteCore de preparación de ocho sitios para las trazas ilustrativas**. No se asignan qubits GHZ arbitrarios a planetas: eso no añade fidelidad astronómica, FPS ni potencia de render. Ambas líneas conservan código propio y permanecen reproducibles.

## Pruebas

- `SolarRenderPolicyChecks.java`: relaciones 4K/8K, orientación, 16×/64×, presupuesto y exclusión de vista reducida para PNG.
- `SolarInstrumentation.java`: imagen fija Cine distinta de Original; repetición bit a bit; misma revisión de Ether y tiempo físico; FBO preview 960p real en UHD y conservación de la representación después de volver a modo normal.
- Suite Solar existente: texturas NASA, navegación, cámaras, datos planetarios, campos magnéticos, atmósferas, PNG real, selección de cuerpos, 4K y 8K.
- `tools/solar_shader_check.py`: referencias de shader con modo original por defecto (uniform `uCinematic=0` en fixtures sin UI), para separar cambios intencionados de errores de física/filtrado.

**Requisito:** no atribuir una ganancia real de FPS sin resultados medidos en GPU, idealmente Samsung A16 5G. La validación de CI usa emulador o software GLES y no equivale a benchmarks en hardware real.

## Fuentes visuales

Tierra: NASA/GSFC SVS Blue Marble; Luna: NASA SVS/LRO-LROC Moon Kit; Júpiter: NASA/JPL/Space Science Institute Cassini PIA07782. Las texturas originales y sus créditos permanecen intactos. El nuevo relieve visual es procedural, no medición.
