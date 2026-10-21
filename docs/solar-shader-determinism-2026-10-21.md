# Solar: filtrado definido y comprobación exacta

## Resultado de esta revisión

Se corrigieron operaciones de filtrado con resultados indefinidos en GLSL ES y se
estableció una referencia explícita que incorpora la misma corrección. Frente a
esa referencia, las optimizaciones conservadas producen **32/32 escenas idénticas**:

| Comprobación | Resultado | Memoria máxima del proceso |
| --- | --- | --- |
| SwiftShader 4.0.0.1, un trabajador, procesos separados | 0 diferencias en 1.843.200 píxeles RGBA; todas las repeticiones posteriores al calentamiento exactas | Referencia: 666.124 KiB RSS; candidato: 723.444 KiB RSS |
| Mesa llvmpipe 25.2.8, programas independientes | Color, identidad y profundidad: 0 diferencias; 5.529.600 píxeles comparados entre las tres salidas | 241.412 KiB RSS |
| Oracle de gradientes, GLSL ejecutado en SwiftShader | 896 componentes, 0 fallos; error absoluto máximo 1,57 × 10⁻⁸ | 132.372 KiB RSS |
| Mismo oracle en Mesa | 896 componentes, 0 fallos; error absoluto máximo 2,06 × 10⁻⁸ | 99.008 KiB RSS |

Esto demuestra **equivalencia de las optimizaciones después de corregir el
filtrado**. No demuestra igualdad de color con la referencia histórica que usaba
operaciones indefinidas, ni representa FPS de un teléfono. Las pruebas host
impusieron 2 GiB de espacio de direcciones por proceso; el oracle impuso 1 GiB.
Los tiempos brutos permanecen en los JSON, pero esta revisión no los presenta como
una mejora de rendimiento: hubo compilaciones independientes en el entorno.

La evidencia está en
[`results/repository-unification-2026-10-21/solar`](../results/repository-unification-2026-10-21/solar/).
Los resultados de Android para estos APK se incorporarán después de ejecutar la
campaña descrita abajo. **No se considera Android aprobado por las pruebas host.**

## Defecto encontrado y corrección de producción

El shader anterior llamaba a `fwidth` dentro de rutas que varían entre fragmentos:
el cielo, el cuerpo intersectado, el detalle de superficie y la opacidad de los
anillos. También consultaba la textura con `texture`, cuya elección de mipmap usa
derivadas implícitas, dentro de la rama del cuerpo y su material. Un vecino puede
pertenecer al cielo, a otro cuerpo o no ejecutar esa función.

GLSL ES 3.00 define esos resultados como indefinidos bajo flujo no uniforme;
`textureGrad` permite suministrar los gradientes explícitos. La referencia
normativa es la [especificación de Khronos, revisión 6](https://registry.khronos.org/OpenGL/specs/es/3.0/GLSL_ES_Specification_3.00.pdf),
secciones 3.10.2, 8.8 y 8.9. Las secciones 8.1 y 8.2 también delimitan los dominios
de `atan` y `pow`.

La corrección transporta diferenciales de un píxel desde el rayo de cámara:

1. Se deriva la normalización del rayo respecto de las coordenadas de pantalla.
2. Para una esfera, se deriva la intersección y después su normal. Con distancia
   `t`, radio `r`, rayo unitario `d`, normal `n` y diferencial `dd`, se utiliza
   `dn = (t/r) * (dd - d * dot(n, dd) / dot(n, d))`.
3. Los diferenciales se rotan a las mismas coordenadas locales que el material y
   se convierten a gradientes de longitud/latitud. `textureGrad` recibe esos
   gradientes; el detalle usa su huella espacial.
4. Los anillos derivan la intersección con su plano. La sombra de anillos incluye
   además la variación de la dirección normalizada hacia el Sol cuando cambia el
   punto de superficie.

En la tangencia se limita el denominador a un valor finito que satura los filtros.
En los polos se elige un meridiano definido y una huella conservadora. Se acotan
las bases de dos `pow` para evitar valores negativos originados por redondeo de
vectores unitarios o funciones trigonométricas. No se cambian efemérides, radios,
estado cuántico, pasos de integración atmosférica ni constantes físicas.

El beneficio comprobado es que el filtrado deja de depender de derivadas de
fragmentos que no ejecutan la misma ruta. **No atribuimos los 99 píxeles del fallo
Android anterior exclusivamente a este defecto.** Se encontró además sensibilidad
al entorno de ejecución de SwiftShader, descrita más abajo.

## Referencia corregida y procedencia

La referencia congelada está en
[`solar-android/tests/fixtures/solar-defined-reference.frag`](../solar-android/tests/fixtures/solar-defined-reference.frag).
Parte del shader de `af0b1797573b55a739dcaef7b784eb98825ff07c` y sólo incorpora la
corrección de filtrado y sus guardas numéricas. Mantiene el cálculo completo del
campo de material, las nubes originales y el cálculo original del mosaico de
candidatos. El candidato conserva sus omisiones de cálculos no consumidos y el
índice actual del mosaico.

| Objeto | SHA-256 |
| --- | --- |
| Shader original de `af0b179` | `e99a3064ca1e0111d4e909382bf8c81cc44288398b0253d804bf433aab10156d` |
| Referencia corregida | `7aae77f535f053be36065bb1ed22a8e9956bf2107d39d9760b38e760c5feacf6` |
| Shader candidato de producción | `361676e4882e9d5560c05d098f9f2092425d579323002082d779ff4c2b72bc50` |
| Fixtures de 32 escenas, 320 × 180 | `e34e642655d489f09e08a86dbbfb8af4043ee956a5e79ebf306d876606b1c408` |

[`defined-reference.patch`](../results/repository-unification-2026-10-21/solar/defined-reference.patch)
conserva el parche exacto contra el shader histórico;
[`defined-reference.json`](../results/repository-unification-2026-10-21/solar/defined-reference.json)
conserva el origen, los hashes y el alcance de la comparación. No se genera la
referencia automáticamente a partir de cada nuevo candidato.

## Validación independiente

`tools/solar_gradients_check.py` extrae las funciones de producción y las ejecuta
en GLSL. El oracle perturba rayos o puntos y vuelve a intersectar esferas y planos
en doble precisión, mediante diferencias centrales. No reutiliza las fórmulas
analíticas como resultado esperado. Comprueba 128 casos por familia: normal
esférica, coordenadas angulares, radio de anillo y radio de su sombra; también
comprueba resultados finitos en tangencias y polos.

La tolerancia numérica de ese oracle (`3e-6 + 3e-4 * abs(esperado)`) verifica una
derivada aproximada por diferencias finitas. **No es una tolerancia de imagen**.
Las comparaciones de capturas exigen igualdad de todos los bytes RGBA, incluidos
los canales alfa. El comparador se comprobó introduciendo un solo cambio de un
byte alfa: lo rechaza, al igual que una repetición inestable o una configuración
de controlador diferente.

Resultados principales:

- [`gradients-swiftshader-final.json`](../results/repository-unification-2026-10-21/solar/gradients-swiftshader-final.json)
  y [`gradients-mesa.json`](../results/repository-unification-2026-10-21/solar/gradients-mesa.json).
- [`swiftshader-defined-comparison-32.json`](../results/repository-unification-2026-10-21/solar/swiftshader-defined-comparison-32.json):
  las 32 comparaciones externas, hashes RGBA y las dos procedencias.
- [`swiftshader-reference-32.json`](../results/repository-unification-2026-10-21/solar/swiftshader-reference-32.json)
  y [`swiftshader-candidate-32.json`](../results/repository-unification-2026-10-21/solar/swiftshader-candidate-32.json):
  todas las repeticiones, calentamiento y memoria de cada proceso.
- [`mesa-defined-pair-32.json`](../results/repository-unification-2026-10-21/solar/mesa-defined-pair-32.json):
  color, identidad y profundidad con una segunda implementación de GLES.
- [`replay-comparator-checks.json`](../results/repository-unification-2026-10-21/solar/replay-comparator-checks.json):
  comprobaciones negativas de la comparación exacta.

## SwiftShader: matriz de diagnóstico y límites

El fallo anterior de Android tenía 99 píxeles diferentes de 584.640, con entradas
registradas iguales. Algunas repeticiones eran inestables; Júpiter y Urano podían
ser estables dentro de cada APK y aun así diferir entre APK. Esta revisión no
borra ese resultado ni lo convierte en una regresión demostrada del candidato.

El mismo SwiftShader 4.0.0.1 del SDK se pudo ejecutar directamente en host,
eliminando Java/ART y las superposiciones Android de ese experimento. Dos
compilaciones del shader original idéntico diferían en 21 píxeles de cuatro
escenas; las repeticiones también fluctuaban. Las salidas diagnósticas de identidad
y de los 32 bits de una componente de la normal eran exactas en seis escenas.
Los píxeles que cambiaban en color estaban sobre cuerpos y no cambiaban de
identidad. Quitar la integración atmosférica no eliminó el problema.

| Variante diagnóstica | A/A final | Repeticiones segunda y siguientes, sumadas por programa |
| --- | --- | --- |
| Original, trabajadores por defecto, cuatro escenas | 21 píxeles distintos | 57 / 46 |
| Gradientes analíticos, antes de las guardas finales, mismos trabajadores | 22 píxeles distintos | 65 / 53 |
| Original, un trabajador, seis escenas | 0 | 0 / 0 |
| Gradientes analíticos, antes de las guardas finales, un trabajador | 0 | 0 / 0 |
| Referencia y candidato finales, un trabajador, 32 escenas cada uno | 0 | 0 / 0 |

La variante intermedia analítica tiene hash `795e111afbd5e8c6dd6f740e2937f5dcb8abe7ed32eb2cabcdeea7cc927e21bb`;
no se confunde con el candidato final de la tabla de procedencia. La opción de
diagnóstico `ForceClearRegisters=1` tampoco resolvió la fluctuación multihilo.
No se encontró evidencia suficiente para identificar una línea concreta del
controlador como causa. La corrección del shader no se presenta como reparación
del controlador multihilo.

SwiftShader admite un archivo `SwiftShader.ini` con `Processor.ThreadCount`;
véanse su [documentación de configuración](https://swiftshader.googlesource.com/SwiftShader.git/+/refs/heads/master/docs/RuntimeConfiguration.md)
y la [lectura de configuración de la implementación GLES histórica](https://swiftshader.googlesource.com/SwiftShader.git/+/6ef6d2afff67ff7fb7af8c253838efcffa5d618d/src/Main/SwiftConfig.cpp).
Se usa un directorio aislado con `ThreadCount=1` y el servidor de configuración
desactivado. En host se observaron dos hilos de proceso con esa configuración,
frente a diez con el comportamiento por defecto.

Con un trabajador aún existen cambios del primer dibujo de algunas escenas. Se
realizan **dos dibujos completos de calentamiento para ambos programas**, se
registran sus diferencias por separado y después se exige exactitud a todas las
repeticiones. En los replays finales las sumas de cambios de calentamiento son
47/41, idénticas para referencia y candidato. No se descartan divergencias
persistentes: éstas hacen fallar el ejecutable.

Se conservaron además intentos fallidos: SwiftShader produjo `SIGSEGV` con algunas
variantes complejas o con dos programas completos en el mismo proceso. Separar
los programas permitió terminar bajo el mismo límite de memoria. No se atribuye
el fallo a falta de RAM sin evidencia: los replays separados tienen picos de
816.812 y 885.368 KiB de espacio virtual. Mesa ejecutó los programas independientes
y las salidas auxiliares conjuntamente.

## Android y CI

Los APK candidato y referencia corregida se compilaron en árboles separados con
JDK 17, Gradle 8.9, un trabajador y JVM de 1 GiB. Ambos usan el mismo APK de
instrumentación y certificado de esta compilación. La referencia mantiene
versión 1.1.0/code2 y el candidato 1.3.0/code4. Sus manifests, hashes, firmas,
assets y ABI se conservan en
[`android-builds/solar-apk-builds.json`](../results/repository-unification-2026-10-21/solar/android-builds/solar-apk-builds.json).

La campaña de Android exige ocho escenas exactas y repeticiones exactas, con las
mismas entradas registradas. Usa seis dibujos previos a la primera captura por
escena, conserva ambas capturas y verifica que dibujar no cambia el núcleo. El
runner conserva su política de fallo ante instrumentación incompleta, diferencias
o entradas inconcluyentes.

`--swiftshader-threads 1` crea una configuración exclusiva para el emulador.
`strace` se filtra al archivo exacto y debe observar una apertura de lectura
exitosa antes de continuar. Un archivo escrito pero no leído **no permite
afirmar que se aplicó la configuración**. Se guardan la configuración, la traza y
los mapas/hilos del árbol de procesos del emulador. La opción no cambia el shader
ni las tolerancias y se aplica igual a ambos APK.

`solar-performance.yml` usa la referencia corregida, el oracle independiente y el
gate de Mesa antes del pareado Android. La ejecución local Android de estas
fuentes está pendiente al congelar el cambio; su resultado se añadirá como
evidencia posterior. No se ejecutaron pruebas en un móvil físico ni se afirma
rendimiento o exactitud para su controlador.

## Reproducción mínima del gate host

```bash
export OPENBLAS_NUM_THREADS=1 LP_NUM_THREADS=1
python3 tools/solar_gradients_check.py --output gradients.json
python3 tools/solar_shader_check.py \
  --fixtures results/solar-integration-2026-10-20/shipping/fixtures-native-320.json \
  --baseline-shader solar-android/tests/fixtures/solar-defined-reference.frag \
  --warmup-frames 2 --repeats 3 --output shader-defined.json
```

Para el SwiftShader legado, se suministra su `libEGL.so` mediante `--egl-library`,
se incluyen sus dos directorios de bibliotecas en `LD_LIBRARY_PATH` y se añaden
`--swiftshader-threads 1 --single-program --color-only --capture-dir ...`.
Se ejecuta primero la referencia usando ese mismo archivo en `--baseline-shader`
y `--candidate-shader`; se repite en otro proceso con el shader candidato.
`tools/solar_compare_replays.py` exige configuraciones y fixtures iguales,
repeticiones exactas y compara los PNG completos de ambos procesos.
