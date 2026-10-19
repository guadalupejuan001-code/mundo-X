# Verificación de gráficos en host

`verify_gles.py` ejecuta los shaders GLES3 reales y los compara con el renderizador
anterior (`ebd53fc`). Usa Python estándar, EGL del sistema y Java. No añade una
biblioteca ni un motor al APK.

```sh
python3 tools/graphics/verify_gles.py \
  --android-jar /ruta/al/sdk/platforms/android-35/android.jar \
  --output /tmp/mundox-graphics.json
```

Necesita `java` y `javac`. Si hay Java pero no `javac`, añadir
`--ecj /ruta/ecj.jar`. Necesita EGL/GLES3 con plataforma EGL surfaceless, como Mesa
llvmpipe en Linux. `--quick` ejecuta dos escenas pequeñas; el comando sin esa
opción incluye el millón. `--baseline-ref` permite fijar otra revisión histórica.

El programa compila una copia temporal del renderizador histórico con otro
nombre. `HostGraphicsExport.java` llama por reflexión a `vertexSource`,
`FRAGMENT_SOURCE`, `cubeMesh` y `cubeIndices` de las clases compiladas. Invoca
directamente `ObjectDrawPlan.update` y `ObjectSceneModel.transform`; el plan y las
transformaciones no se reimplementan en Python. El shader histórico comparte la
función hash actual del modelo, que esta optimización conserva sin cambios.

Verifica:

- Los 36 vértices históricos, expandidos en triángulos, coinciden byte por byte
  con los 24 vértices y 36 índices actuales.
- La imagen de la llamada histórica con todos los objetos coincide exactamente
  con llamadas indexadas de hasta 16.384 instancias, con y sin descarte de rangos
  fuera del campo visible. También compara la ruta explícita con desplazamiento
  de su VBO. Todas comparten cámara, tiempo, shader de fragmento, profundidad de
  24 bits y framebuffer RGBA8 de 128 × 128.
- Hay píxeles de objetos en cada escena visible. Una escena intencionalmente
  fuera de cámara debe producir fondo idéntico y cero instancias enviadas.
- Transform feedback comprueba **todos** los ID y sus cuatro componentes contra
  el modelo Java, byte por byte, en ambas rutas y a través de cada reinicio de
  `gl_InstanceID` entre lotes. Incluye el último ID, 1.048.575. En el segundo caso
  del millón se repite la imagen con otra semilla/rotación/cámara, sin repetir la
  captura completa de transformaciones.
- Se cubren 1.024, 65.535, 65.536, 65.537, 100.003 y 1.048.576 objetos; semillas
  positivas, negativas y extremas; objetos elevados, ocultos y seleccionados;
  escenas no cuadradas, cámaras próximas y animación rotada.

El JSON conserva hashes de fuentes, cobertura, diferencias, objetos enviados,
llamadas y tiempo de cada dibujo. El tiempo termina con `glFinish`, incluye la
presentación de comandos desde Python y excluye subidas/lectura de píxeles. Es
una medida individual de validación en el host, con ruido y efectos de caché:
**no es FPS de un teléfono ni una medición aislada de tiempo GPU**. La igualdad
de estas escenas no demuestra que todos los drivers móviles se comporten igual.

El [resultado guardado](../../results/graphics_spatial_2026-10-18/host-gles.json)
corresponde a Mesa llvmpipe en software.
La memoria que informa el plan cuenta sus arrays primitivos; excluye cabeceras
Java, memoria del driver y el resto del proceso.
