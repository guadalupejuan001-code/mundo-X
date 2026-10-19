# Integración visual de Motor X

Base: `775f5a6`, rama `codex/motor-x-visual-20261018`.

Se añadió `VisualScene` al runtime `mundo_x` y se extendió XASM con declaración,
edición, borrado de excepciones, consulta de escena y copia explícita de una
probabilidad Z a un objeto. `--scene-stdout` exporta la escena final en el formato
de XLab, después de ejecutar y comprobar el programa. No se modificaron los
algoritmos de los núcleos de cálculo ni el renderizador Android.

## Validación local

- Compilación C++17 optimizada con GCC 14.2 y CMake: correcta.
- CTest: 14 pruebas aprobadas, incluyendo los programas anteriores y los nuevos
  contratos visuales. Las comprobaciones cubren errores de entrada y fallos de
  lectura del programa, además de las rutas correctas.
- Modelo visual: 7.559 comprobaciones, con fixtures de bits float32 del modelo
  Java, límites, cambios dispersos y revisiones agotadas. Pasó también con
  AddressSanitizer y UndefinedBehaviorSanitizer.
- CLI: 69 comprobaciones de informes, formato canónico y rechazos sin exportar
  escenas parciales. La salida raw usa LF también en la ruta de Windows.
- XASM visual: 671 comprobaciones de integración, incluidas consultas sin
  colapso ni consumo de RNG y rechazo de programas interrumpidos por un error
  de lectura.
- Comparación real C++/Java: **11 casos, 1.346.595 IDs y 5.386.380 valores float32,
  con cero diferencias de bytes**. Un caso revisa todos los 1.048.576 IDs de la
  escena máxima. Se comprueban además importación/exportación canónica, cambios
  y rechazos esperados. [Datos y hashes del código](motor_x_visual_2026-10-18/scene-bridge.json).

## Ejemplo observable

`mundo_x run examples/visual_interference.x` termina con la aserción del cálculo
aprobada, 1.024 objetos, tres excepciones y versión 3. La exportación contiene:

```text
MUNDOX_OBJECTS_V1
count=1024
seed=2026
version=3
edits=3
497:3
498:6
499:7
```

Los objetos 496, 497 y 498 registran probabilidades de tres momentos del mismo
cálculo. La consulta visual no mide ni modifica el estado; las pruebas comparan
también las secuencias de medición con y sin esas consultas.

## Alcance

Se demuestra compatibilidad de representación y una integración programable.
La comparación entre lenguajes no mide FPS ni la RAM total. Las transferencias
a XLab se realizan mediante el portapapeles y **Restaurar escena**; no hay un
renderizador nativo de escritorio ni sincronización continua. El nuevo workflow
`visual-runtime.yml` ejecuta las regresiones nativas en Linux y Windows y la
comparación de modelos en Linux; sus resultados se consultan en GitHub Actions.

[Uso y semántica de las instrucciones](../docs/motor_x_visual.md).
