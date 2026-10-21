# Solar: revisión de la percepción de lentitud en 4K

El usuario informó que Solar le parecía más lento en 4K tras la consolidación.
La integración y las pruebas de exactitud previas no demostraban una mejora de
fluidez en su dispositivo. Este registro no da la incidencia por resuelta.

## Hallazgos del código inspeccionado

Fuente: `34432b69ad6c64b87bb8076951cd34e3a22093f7`.

- El shader actual devuelve el catálogo completo en `visualCandidates()`. La
  retirada del descarte por teselas corrige diferencias visuales y elimina su
  ahorro de trabajo GPU. Su contribución a la lentitud del teléfono es una
  hipótesis pendiente de medición.
- `SolarRenderer.syncVisualEther()` sigue reconstruyendo y, cuando cambian,
  enviando máscaras que el shader ya no consume. A 3840 × 2160 son 510 máscaras.
  Es una oportunidad concreta de retirar trabajo muerto; esta auditoría no
  modifica producción ni atribuye una ganancia medida a retirarlo.
- La comparación Android anterior de `SolarPerformanceChecks` fijaba una
  dimensión máxima de 360 píxeles. Las pruebas funcionales 4K y 8K acreditan sus
  condiciones examinadas, pero no una mejora de FPS en un teléfono físico.
- No se encontró una llamada nueva a `glFinish` en el renderizador. La lectura
  `glReadPixels` se limita a capturas solicitadas. El render continuo en pausa
  ya existía antes de esta consolidación.

## Ensayo nuevo, acotado a 4K

Se ejecutó una campaña a 3840 × 2160, con dos calentamientos y cinco muestras
por versión y escena. Usa Mesa llvmpipe por software, una CPU y ninguna GPU
física. El temporizador abarca dibujo y `glFinish`, sin lectura de píxeles.

| Escena | Referencia definida, mediana ms | Shader actual, mediana ms | Cambio de tiempo |
| --- | ---: | ---: | ---: |
| 4: Tierra exterior | 1692,760 | 1620,360 | −4,28 % |
| 24: entrada en Tierra | 3196,805 | 2936,231 | −8,15 % |

No hubo diferencias de color, identidad ni profundidad: 49.766.400
comparaciones de píxel entre las tres salidas. Las repeticiones y los
calentamientos fueron estables. El proceso terminó correctamente en 78,31 s;
el RSS máximo fue 499.004 KiB.

La referencia independiente tiene filtrado definido y evaluación completa de
materiales. **No es la APK anterior instalada por el usuario.** Ambas escenas
seleccionadas son pares y el runner dibujó primero el candidato en ambas.
Esta campaña pequeña no demuestra rendimiento sostenido ni aleatorizado.
Tampoco permite concluir cómo se comportan interacción, temperatura o GPU del
aparato del usuario, ni refuta su observación.

## Evidencia y siguiente dato necesario

Los JSON y el log adjuntos conservan muestras individuales, entorno, comando,
hashes y límites. El shader actual es `e36370ee7997d2f903529247b75a716c59fda679bfd4543b2c0c5c3ba2d6d02b`;
la referencia es `1c978eab12807d2f791bebe4167564027e7ef705947a58f16313461224cc2753`.

Faltan el modelo del teléfono y la identificación de las APK comparadas para
reproducir la regresión reportada. Una comparación pertinente debe mantener
iguales resolución real, escena, cámara, fecha y opciones, y medir el tiempo
completo del fotograma en el mismo dispositivo.
