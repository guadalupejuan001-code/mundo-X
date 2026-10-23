# Solar: Ether demand-driven v0.1

Base: main después de PR #54 (Ether Memory con 256 páginas COW).

Esta etapa cambia la política de **observación y renderizado**, no las leyes físicas ni la semántica cuántica. Solar conserva `QuantumSolarBridge`, su estado Ether por sesión, la efeméride `SolarData` y el shader GLES propio.

## Implementación

`SolarRenderer` añade un modo de vista previa adaptativa: mide el coste de cada cuadro, baja el máximo de resolución tras 12 cuadros de más de 29 ms y lo recupera gradualmente tras 90 cuadros por debajo de 14 ms. Usa histéresis para evitar cambios continuos. La vista previa va de 480 a 1280 píxeles de dimensión máxima. El estado de simulación, la fecha, los cuerpos y la observación de Ether no se modifican por cambiar el framebuffer.

La captura explícita 4K/8K no se rebaja automáticamente. La opción `setQuality` desactiva la adaptación para respetar la resolución manual; `setAdaptivePreview(true)` permite reactivarla. El modo de entrada atmosférica se excluye de esta primera etapa para evitar afectar su experiencia.

## Límites y aceptación

- Esta primera modificación es **una política de resolución**, no una reimplementación del renderizador ni una prueba de que Ether acelere GLES.
- Requiere compilar Solar Android, ejecutar la suite de integración y medir FPS, PSS/RSS, GPU y calidad en un dispositivo real.
- El PR #54 optimiza las transacciones de Inverse Ether Memory, pero Solar todavía consulta el puente `NativeCore` existente; no se le atribuyen ganancias de RAM sin un perfil que demuestre su participación.
- Futuro: reducir cálculos y transferencias de observables sin cambios, priorizar objetos visibles y eliminar buffers temporales grandes, siempre con comparación de imagen y conservación de estado.

No fusionar hasta validar los workflows de Solar.
