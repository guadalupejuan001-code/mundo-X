# Exportación 8K: aislamiento de la semilla del fixture

## Hallazgo

El [job Android 118972460953](https://github.com/guadalupejuan001-code/mundo-X/actions/runs/39523264180/job/118972460953), sobre `2be39f7915001c46b63246b9a9502071b599475d`, completó 29 de 30 pruebas. Falló `ultra8k_streamed_png_dimensions_and_rows` en el requisito `different > 100` del preview muestreado.

Se descargó el artefacto `12311638267` y se inspeccionó su PNG real. El archivo tiene 7680 × 4320 píxeles RGBA y **234.465 píxeles distintos del fondo**. Hay geometría real, con tres colores de caras y un rectángulo envolvente `[3563, 2231, 4109, 2764)`. Su SHA256 es `e11841be140cdc277a2cae7ae7a527932d05e211439dbd05e7baef8e668c8f6a`. La copia original se conserva junto a este informe.

## Causa

`frameReuseContracts`, añadido en `ad3ec6b`, cambia la semilla a 2027 para comprobar que una modificación invalida la imagen. Su bloque `finally` restauraba la configuración de reutilización y presentación, pero dejaba esa semilla. Los fixtures posteriores llaman a `scene()`, que reinicia núcleo, población, resolución, acoplamiento, tiempo y cámara; conserva la semilla. La exportación 8K acabó evaluando otro objeto que el esperado por su umbral de visibilidad.

La ley de altura, tras la animación a 0,875 segundos y la expectativa −1 del primer estabilizador después de Z0, produce:

| Semilla | Altura | Muestras de geometría en los 64 offsets posibles del bloque 8×8 |
|---|---:|---:|
| 2026, fixture previsto | 1,476259949 | 147–149 |
| 2027, heredada de la prueba de caché | 0,669122832 | 76–79 |

El muestreo centrado del PNG guardado encuentra 78 muestras. El oráculo independiente de intersección rayo/caja reproduce la máscara muestreada del objeto de 2027 sin diferencias en los 64 offsets. La semilla 2026 supera el requisito original de más de 100 muestras en los 64 offsets.

La captura de la Activity muestra una superficie de 320 × 41 en paisaje. Esto no determina la proyección del PNG: `scene(1)` fija eye/target y `exportPng` llama a `camera(7680,4320)`. Las comparaciones por tiles frente al framebuffer monolítico ya habían terminado con cero diferencias de color, IDs y bordes en el mismo job. La lectura y el diagnóstico no encontraron un defecto de exportación o de reutilización del framebuffer.

## Corrección

Se añadieron siete líneas exclusivamente en `GraphicsInstrumentation.java`:

- La prueba de reutilización guarda su semilla inicial y la restaura en `finally`, incluso si una comprobación falla.
- `ultraStreamed` declara explícitamente la semilla 2026 para la geometría de su umbral.
- La evidencia registra semilla, dimensiones del preview, paso de muestreo, muestras distintas y umbral antes del assert.

El requisito `different > 100` y las comprobaciones de tamaño, filas, memoria, conservación del estado, captura anterior y geometría permanecen. No se modificaron `QuantumRenderer`, el shader, el codificador PNG ni `scene()`.

## Verificación y alcance

`instrumentation-compile.json` acredita compilación Java con JDK17 y API Android35, contra las clases principales actuales, sin construir un APK. El análisis de imagen y rayos usa menos de 2 GiB de espacio de direcciones; su pico RSS fue 446.900 KiB. `analysis.json` conserva todos los offsets y resultados.

No se ejecutó un segundo emulador ni una campaña de rendimiento. La compilación y el oráculo resuelven el defecto de fixture; la confirmación completa Android corresponde al siguiente CI sobre el commit que incluya la corrección.
