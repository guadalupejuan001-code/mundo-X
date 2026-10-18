# XLab · Objetos 0.3 — validación del 18 de octubre de 2026

## APK comprobado

- Commit de compilación: `f942ac516c4116cbe860d40c22f97eea92ea59ce`.
- [Build y pruebas completas](https://github.com/guadalupejuan001-code/mundo-X/actions/runs/39147724314): ambos jobs exitosos.
- [Descargar ZIP con APK y SHA256](https://github.com/guadalupejuan001-code/mundo-X/actions/runs/39147724314/artifacts/12143149171).
- APK: `XLab-0.3.0-debug.apk`, **2.400.660 bytes**.
- SHA256: `7196681a2e25d4ffa63a654e7cf91bf413807814539a48d2b59b9603db144bb9`.
- Paquete `com.mundox.lab.xlab.objects`, etiqueta **XLab · Objetos**.
- Android 8+, OpenGL ES 3, ARM64 y x86-64; compilación debug.

Se instala junto a XLab 0.2, cuyo paquete es `com.mundox.lab.xlab`. Se comprobó
que sus certificados debug difieren; utilizar otro paquete evita borrar los
datos del laboratorio anterior para poder instalar esta edición. Esto no migra
automáticamente las preferencias entre aplicaciones. La clave debug de CI no
es una clave de publicación permanente.

El ZIP pasó su comprobación de integridad y el SHA256 del APK coincide con el
manifiesto de CI. Se verificaron las clases de objetos, las dos bibliotecas
nativas y el identificador del paquete del APK descargado.

## Pruebas aprobadas

- **12/12 CTest nativas** en Linux.
- **448 comprobaciones** del modelo Java, también ejecutadas en Android.
- **15/15 recorridos Android**, `tests_failed=0`, código de instrumentación `-1`.

Los ocho recorridos anteriores de circuitos/protocolos/memoria/bitácora siguen
pasando. Los siete nuevos comprueban el modelo, apertura y persistencia,
equivalencia de imagen y efectos de las ediciones, cancelar/repetir, medición de
ambas rutas, cancelación de escala y parada automática con restauración.

La persistencia se comprueba cerrando y creando otra Activity: se conserva la
semilla negativa y la edición por ID. Dos lotes de +256 añaden 512 excepciones
nuevas además de la edición previa. El límite de 4.096 y los errores atómicos
se comprueban en el modelo.

## Equivalencia de CPU y GPU

La comparación lee un framebuffer RGBA8 de 128 × 128 con profundidad y exige
cobertura real. Además, transform feedback captura el mismo programa de
vértices para hasta 1.024 IDs y compara sus cuatro valores con la CPU.

Las pruebas con semillas `-2147483648`, `2147483647`, `0` y `-1` pasaron.
Los informes guardados de las últimas tres, con 256–257 excepciones y un objeto
oculto, tienen **0 píxeles diferentes**, diferencia máxima de canal **0** y
**0 transformaciones diferentes** en las dos rutas. No fue necesario consumir
la tolerancia de dos niveles de color de la comprobación.

Durante el desarrollo se detectó una discrepancia en el hash escalar de 32 bits
en el traductor SwiftShader del emulador: las posiciones/IDs y los valores del
VBO coincidían con la CPU, pero las alturas y tonos procedurales diferían.
Reformular solamente los productos tampoco lo resolvió. La versión final
mantiene todo el hash en cuatro bytes, incluida la semilla y los desplazamientos;
cada acumulación es como máximo 65.535. La función matemática de la CPU y sus
valores de referencia se conservan. Esta corrección es del ensayo gráfico y no
modifica el núcleo cuántico.

La medición rechaza escenas que fallen esa comprobación previa. Las imágenes y
transformaciones comprobadas no demuestran equivalencia para todas las cámaras,
semillas o cantidades posibles.

## Rendimiento observado en el emulador

Equipo de prueba: Android 15 x86-64 con **Google SwiftShader**, GPU por software.
Viewport de dibujo informado: **996 × 794**. No son tiempos de un teléfono ni
de una GPU física. Los intervalos incluyen presentación y planificación; el
envío de CPU no es tiempo GPU aislado.

Ensayo de 1.024 objetos, semilla `-1`, 257 excepciones, imagen comprobada:

| Ruta | Muestras | P50 de fotograma | P95 de fotograma | CPU de envío media |
| --- | ---: | ---: | ---: | ---: |
| Reglas + cambios | 28 | 89,156 ms | 126,527 ms | 0,256 ms |
| Transformaciones explícitas | 50 | 47,894 ms | 71,555 ms | 1,717 ms |

**En esta ejecución, reconstruir fue más lento en cadencia.** La descripción
lógica fue menor: 2.088 bytes frente a 16.384 bytes de transformaciones. No
equivale al ahorro de RAM total: ambas representaciones podían quedar asignadas,
y el controlador, las estructuras Java y los buffers temporales tienen costes
adicionales. El orden fijo y las muestras breves limitan cualquier comparación
estadística o extrapolación a otro equipo.

La escala automática probó 1.024 y 4.096 objetos y paró correctamente a los
**15.195 ms**, con `frame_limit`. En 4.096, P95 fue **230,661 ms** para reglas y
**136,880 ms** para explícita. La imagen previa de ambas etapas pasó. PSS al
terminar cada etapa: 84.214 y 85.579 KiB, respectivamente; no es el pico medido.
Se restauró la escena con sus excepciones.

El techo configurable de **1.048.576** no fue alcanzado por esta escala. Su
presencia no acredita fluidez a esa cantidad. El límite real en el teléfono
queda pendiente de ejecutar allí la prueba y exportar su informe.

## Evidencia y uso

- [Guía y significado de las mediciones](../docs/xlab_0_3_objetos.md).
- [Benchmark completo](xlab_objetos_2026-10-18/xlab-objetos-benchmark.json).
- [Escala y motivo de parada](xlab_objetos_2026-10-18/xlab-objetos-escala.json).
- [Imagen semilla -1](xlab_objetos_2026-10-18/xlab-objetos-imagen--1.json),
  [semilla 0](xlab_objetos_2026-10-18/xlab-objetos-imagen-0.json) y
  [semilla MAX](xlab_objetos_2026-10-18/xlab-objetos-imagen-2147483647.json).
- [Capturas, log de instrumentación y emulador](https://github.com/guadalupejuan001-code/mundo-X/actions/runs/39147724314/artifacts/12143694653).

En la aplicación: **Objetos ↗ → Escala extrema**. Puede detenerse en cualquier
etapa y copiarse el informe. La parada es cooperativa; no interrumpe una llamada
bloqueada del controlador. El ensayo no añade colisiones físicas individuales
ni demuestra una ventaja cuántica.
