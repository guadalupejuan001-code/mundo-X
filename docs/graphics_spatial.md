# Optimización gráfica por reglas y regiones

Esta rama añade una etapa de planificación al laboratorio de objetos de Mundo X.
Conserva el modelo `ObjectSceneModel`: semilla, regla, IDs estables y cambios
dispersos. La escena mantiene hasta 1.048.576 objetos y 4.096 cambios persistentes.
No añade un motor gráfico externo ni cambia el formato de las escenas guardadas.

## Qué hace el código

- `ObjectDrawPlan` calcula intervalos de IDs que pueden contribuir a la imagen.
  Examina regiones de una fila y descarta una región únicamente cuando su caja
  completa queda fuera de un plano de la cámara. La caja cubre las alturas
  permitidas, los cambios y la animación. Los objetos descartados conservan sus
  IDs y sus cambios; vuelven a enviarse cuando cambia la vista.
  Sus arrays reutilizables ocupan 41.152 bytes de datos, sin cabeceras Java; no
  se crea un array de transformaciones por objeto ni se asigna uno en cada frame.
- Los intervalos consecutivos se agrupan en llamadas de hasta 16.384 instancias.
  El shader reconstruye el ID global con `uBaseId + gl_InstanceID`. La ruta
  explícita usa el mismo intervalo desplazando su atributo de transformaciones.
- Cada cubo comparte 24 vértices entre sus 12 triángulos mediante 36 índices.
  Se mantienen los triángulos, sus normales y su orden. La malla compartida ocupa
  648 bytes de datos: 576 de vértices y 72 de índices, frente a los 864 anteriores.
- «Campo quieto» sigue dibujando solo cuando cambia algo. «Fuera de cámara» permite
  activar y desactivar el descarte para comprobar el resultado.

La representación compacta y el coste de renderizado son medidas distintas.
Un formato base de 32 bytes evita un array de transformaciones de 16 MiB para
un millón de objetos, pero la GPU debe generar y rasterizar los objetos enviados.
Hay memoria del proceso, del plan de dibujo y del controlador. El trabajo no
cuesta cero.

## Qué comprobar

La interfaz distingue población de la escena, instancias enviadas, instancias
omitidas y llamadas de dibujo. «Enviadas» no significa que todas aportaron un
píxel: también pueden quedar ocultas detrás de otras. La consulta de visibilidad
solo confirma que hubo muestras visibles, no cuántos objetos se vieron.

«Imagen A/B» compara la representación por reglas y la explícita, y comprueba
además la imagen contra una ruta que envía toda la población. Exige cobertura
real de píxeles; dos imágenes vacías no bastan. Las capturas de transformaciones
comprueban también IDs en los límites de lote y al final de la escena.

El ensayo independiente en `tools/graphics` compara con el dibujo anterior de
36 vértices, además de los caminos nuevos. Usa el shader y la malla extraídos
del código Java y el plan calculado por `ObjectDrawPlan`, para evitar validar
solo una reimplementación del algoritmo.

## Límites de la mejora

En una vista que incluye toda la escena puede no haber regiones descartables.
Los lotes añaden llamadas y la planificación consume CPU. El uso de índices
permite reutilizar vértices, pero el número de ejecuciones reales del shader
depende del controlador. Los contadores de candidatos no son una medida de
invocaciones GPU ni una garantía de mejora de FPS.

La captura aportada muestra una Mali-G68 sin objetos visibles. Dividir el dibujo
en lotes permite probar una ruta distinta, pero no demuestra por sí solo la causa
ni la reparación de ese fallo. Las pruebas de GPU por software y el emulador
Android tampoco sustituyen una ejecución en ese teléfono.

Los cambios se desarrollan en `codex/graphics-spatial-20261018`, partiendo de
`ebd53fc`, para poder integrarlos con el trabajo paralelo sin sobrescribirlo.
