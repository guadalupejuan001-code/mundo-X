# Inventario de renderizado de Mundo X

## Decisión

`Ether Render X` es la arquitectura canónica de renderizado nueva. No se borra todavía ningún renderer de producción que conserve una capacidad que Ether Render X v0.1 aún no implementa.

El criterio de retiro no es el nombre ni la antigüedad: un renderer se elimina del producto cuando Ether Render X reproduce su función, pasa su oracle de exactitud y demuestra coste igual o menor en el mismo dispositivo/carga.

## Componentes actuales

| Componente | Rol real | ¿Competidor directo? | Estado |
|---|---|---:|---|
| `native/include/mundo_x/ether_renderer.hpp` | Nuevo motor estructural por regiones, candidatos, rayos e intersección exacta | Sí: núcleo futuro | **Canónico nuevo** |
| `graphics-android/QuantumRenderer` | Renderer GLES de escenas procedurales, millones de objetos, picking, proyectos `.cosmic`, espejos y Android | Sí, parcialmente | **Conservar hasta migración** |
| `graphics-desktop/renderer.py` | Implementación GLES de escritorio y oracle/harness del mismo mundo gráfico | No: referencia y validación | **Conservar** |
| `solar-android/SolarRenderer` | Renderer especializado del sistema solar: materiales, mapas, anillos, atmósferas, eclipses y vuelo | Sí, pero con muchas capacidades no migradas | **Conservar hasta adaptación Solar** |
| `app/.../ObjectSceneRenderer` | Laboratorio Android de reglas, IDs, edición y escenas de hasta un millón de objetos | No: banco de pruebas | **Conservar como oracle/adaptador** |
| `app/.../MicroLabRenderer` | Visualización especializada del micromundo/cuatro qubits | No | **Conservar** |
| `pc-android/PcVideoRenderer` | Presentador de framebuffer/VRAM de la PC emulada, no motor 3D | No | **Conservar** |
| `GpuVisualActivity.SceneRenderer` | Demo de partículas GPU y acoplamiento visual | No: experimento | **Conservar como prueba hasta portar partículas** |
| `SyncedGpuActivity.SceneRenderer` | Demo visual con sincronización entre dispositivos | No: experimento de red | **Conservar hasta separar red del render** |

## Regla de eliminación

Un componente gráfico activo se retira únicamente cuando se cumplen las cuatro condiciones:

1. Ether Render X cubre todas sus salidas/funciones usadas por el producto.
2. Las pruebas de compatibilidad dan cero diferencias donde el contrato exige exactitud.
3. El coste total medido no empeora en el mismo objetivo.
4. Ninguna Activity, APK, test, formato portable o herramienta depende de la implementación retirada.

## Orden de absorción

1. `ObjectSceneRenderer`: reglas/IDs/picking y escenas grandes.
2. `QuantumRenderer`: GLES Android, proyectos `.cosmic`, espejos y edición.
3. `SolarRenderer`: superficies, mapas, anillos, atmósferas, eclipses y vuelo.
4. Demos de partículas/sincronización: migrar solo el render; conservar la lógica específica.

`PcVideoRenderer` no entra en esta lista de reemplazo porque presenta un framebuffer huésped; no resuelve una escena 3D.

## Resultado de la limpieza

No se eliminaron renderers productivos en esta revisión porque Ether Render X v0.1 todavía perdería capacidades si los sustituyera hoy. En cambio, el repositorio queda con una autoridad clara: **Ether Render X es el núcleo objetivo; los demás son adaptadores, referencias o consumidores temporales.**
