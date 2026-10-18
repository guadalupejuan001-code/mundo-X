# Gráficos: equivalencia y trabajo enviado

Base: `ebd53fc`. Código en `codex/graphics-spatial-20261018`.

La optimización conserva el modelo de reglas y cambios. Añade una malla de
24 vértices con 36 índices, lotes de hasta 16.384 instancias y descarte conservador
de regiones fuera de cámara. La guía está en [graphics_spatial](../docs/graphics_spatial.md).

## Ensayo local reproducible

- Compilación completa de Java de la aplicación y su instrumentación: correcta,
  con ECJ, API Android 35 y destino Java 8.
- Modelo: 448 comprobaciones aprobadas.
- Planificador: 1.542 comprobaciones aprobadas, incluyendo un oráculo de las ocho
  esquinas por objeto para detectar omisiones incorrectas hasta el millón.
- GLES real mediante Mesa llvmpipe (LLVM 19.1.7): ocho casos aprobados, comparando
  el renderizador anterior con las rutas nuevas. Framebuffer RGBA8 de 128 × 128,
  profundidad de 24 bits. Se comprueban transformaciones y IDs globales contra
  el modelo Java mediante transform feedback.

| Escena | Población | Enviados con descarte | Diferencias de píxeles con original |
|---|---:|---:|---:|
| Vista general | 1.048.576 | 1.048.576 | 0 |
| Vista cercana y girada | 1.048.576 | 170.752 | 0 |
| Vista cercana, población no cuadrada | 100.003 | 56.576 | 0 |

En la vista cercana del millón se omitieron 877.824 instancias fuera de cámara:
**83,72 % menos instancias enviadas**, manteniendo los 7.962 píxeles de objetos
de la imagen original. Se usaron 473 llamadas, frente a una del dibujo anterior.
En la vista general se conservaron todas las instancias, repartidas en 64 llamadas.

La malla indexada se expande a exactamente los mismos 216 valores `float` que
la anterior. Sus datos compartidos pasan de 864 a 648 bytes. El plan usa 41.152
bytes de arrays reutilizables; esta memoria se informa por separado de la regla.

La captura completa del millón verificó todos los IDs y transformaciones en las
dos rutas: cero diferencias de bytes y de IDs, incluido el último, 1.048.575.
También se probaron las poblaciones 65.535, 65.536 y 65.537, semillas extremas,
objetos elevados y ocultos, y una cámara sin objetos: esta última produce cero
envíos y fondo idéntico, sin contar como demostración de rendimiento visible.

## Alcance de las medidas

Los tiempos del JSON son dibujos individuales de validación con `glFinish`.
Incluyen las llamadas Python al controlador y excluyen la generación del plan
Java, las subidas y la lectura de píxeles. No sustentan una cifra de aceleración
de la aplicación ni de FPS del teléfono. El descarte y los lotes añaden trabajo
de CPU; su ventaja depende de la cámara y del controlador.

La captura del usuario mostró una pantalla vacía en Mali-G68. Este ensayo no
demuestra que ese fallo esté reparado en el dispositivo. Se mantiene una
comprobación de cobertura real, referencia completa sin descarte y recuperación
de errores. La validación en hardware Mali sigue pendiente.

Datos completos: [GLES](graphics_spatial_2026-10-18/host-gles.json),
[comprobaciones Java](graphics_spatial_2026-10-18/java-checks.txt).
Reproducción: [herramientas de validación](../tools/graphics/README.md).
