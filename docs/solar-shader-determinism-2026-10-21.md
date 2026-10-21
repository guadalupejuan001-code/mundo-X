# Solar: filtrado definido y comprobación exacta

## Cierre de integración

PR25 quedó fusionada en `main` mediante
[`faa653c9bc81a3a7a48a5fce643878c32c98bd61`](https://github.com/guadalupejuan001-code/mundo-X/commit/faa653c9bc81a3a7a48a5fce643878c32c98bd61).
La revisión `6115ccad` aprobó las 18 campañas, incluido un nuevo pareado exacto,
Solar normal **13/13**, Solar 4K **19/19** y Solar 8K AOSP **15/15**.
El [estado final](../results/repository-unification-2026-10-21/ci/final-6115/workflow-summary.json)
enlaza cada ejecución. El producto y sus pruebas conservan exactamente los
bytes de `b9ec7452`; el cambio posterior se limita al fixture de ejecución y
registro de recursos, como acredita el
[cotejo de fuentes](../results/repository-unification-2026-10-21/ci/final-6115/source-identity.json).

El [ensayo 8K](../results/repository-unification-2026-10-21/ci/final-6115/solar-8k-summary.json)
conserva 16 capturas, incluidas dos PNG de 4320 × 7680. Solicitó 2.048 MiB al
emulador, que anunció 2.560 MiB; `MemTotal` observado del invitado fue 2.532.896
KiB y el RSS previo del proceso emulador fue 3.450.340 KiB. Estas mediciones
no se presentan como un límite de 2 GiB ni como un pico de la aplicación.
Las imágenes y el estado aprobado acreditan las condiciones examinadas en
software GLES; no sustituyen una medición de FPS en un teléfono físico.

## Validación Android del candidato publicado

El commit `b9ec7452307c610448adbb1217e1566aa88fa79b` supera el
[pareado Android final](https://github.com/guadalupejuan001-code/mundo-X/actions/runs/39529818861):
**8 de 8 escenas, 584.640 píxeles RGBA, cero diferencias**, y todas las
repeticiones exactas. Los 29 hashes de entrada por captura coinciden entre
versiones. El runner observó la lectura de la configuración de SwiftShader
con un trabajador y solicitó 2.048 MiB de RAM al emulador Android 15.
Ese argumento no acredita por sí solo la RAM efectiva del invitado: este
pareado no registró `MemTotal`. En los intentos 8K posteriores, el emulador
registró un aumento automático a 2.560 MiB pese a la misma solicitud.
El límite comprobado de 2 GiB por proceso corresponde a los ensayos host;
no se extiende a toda la VM Android ni al teléfono.
No hubo problemas de evidencia, instrumentación ni referencia inconclusa.

El [informe completo](../results/repository-unification-2026-10-21/ci/final-b9ec/solar-paired-report.json)
y el [resumen por escena](../results/repository-unification-2026-10-21/ci/final-b9ec/solar-paired-summary.json)
conservan las identidades APK, los hashes de imagen, las entradas y el entorno.
La variante normal también aprobó sus
[13/13 pruebas Android](https://github.com/guadalupejuan001-code/mundo-X/actions/runs/39529818833).
Estos resultados acreditan la comparación definida y la integración en ese
emulador; los perfiles UHD y el rendimiento de un dispositivo físico son
comprobaciones diferentes.

## Decisión final: retirar el culling GPU que no acreditó exactitud

El pareado Android de `3954b0008db6b61acb1700b8201d75f1f18623e2` comprobó la
referencia completa y detectó otro fallo real: **8 píxeles de 584.640**, cuatro en
Tierra/preset1 y cuatro en Saturno/preset2. Ambos APK repitieron sus ocho escenas
exactamente y sus entradas pareadas coincidieron. El resultado se reprodujo en
CI Android 15/KVM y en Android 10/TCG: las ocho capturas de la referencia son
idénticas entre ambos entornos. Las diferencias máximas de canal son 133 y 17;
no se tratan como ruido ni se acepta una tolerancia mayor.

La comprobación geométrica de esos ocho rayos no encontró un cuerpo contribuyente
ausente de las máscaras. La caja proyectada del planificador ya tiene un margen
de un mosaico completo. No se inflaron radios ni límites sin evidencia. El fallo
queda acotado a la ruta que filtra cuerpos y a su resultado numérico en GLES; no
se atribuye una línea concreta al controlador ni se declara incorrecta la
geometría conservadora de la CPU.

Se retiró **sólo el filtrado de cuerpos en GPU**: producción evalúa todos los
cuerpos como la referencia independiente, y mantiene sus omisiones de cálculo de
material/nubes no consumidos y sus cachés de CPU. Los planes y máscaras siguen
siendo infraestructura comprobada; el estado visible cambia a «escena completa»
y deja de mostrar un porcentaje que sugería ahorro GPU no aplicado. No se cambian
órbitas, radios, volúmenes atmosféricos ni integración física.

La ablación exacta de los casos 1/2, ejecutada en procesos separados, confirmó
**0 diferencias en 146.160 píxeles RGBA**, con todas las repeticiones exactas. El
candidato final `e36370ee…` se comparó contra la referencia completa `1c978eab…`,
que permanece intacta. Sus picos RSS fueron 722.488 y 664.796 KiB respectivamente;
los cambios de calentamiento fueron iguales y se conservan separados del gate.
El caso 7 también tiene la ablación all-body exacta preservada. La tolerancia de
imagen sigue siendo cero y el filtrado analítico conserva su oracle independiente.

La evidencia nueva está en
[`full-evaluation-fallback`](../results/repository-unification-2026-10-21/solar/full-evaluation-fallback/).
Los fixtures exactos de casos 1/2 se añaden al gate de CI junto al caso 7 y las 32
escenas generales. El pareado Android del nuevo APK aprobó después de congelar
el arreglo, como documenta el apartado inicial. El fallo anterior se conserva;
los replays host y la ejecución Android mantienen sus resultados propios.

## Primer aislamiento: 53 píxeles y referencia completa

La prueba Android con un trabajador terminó con **7/8 comparaciones exactas**;
ambos APK repitieron sus ocho escenas sin un solo cambio y sus 29 hashes de
entrada registrados por captura coincidieron. El fallo restante fue una fila
de 53 píxeles de Urano
(136 canales, diferencia máxima 13). Ese fallo se conserva íntegro en
[`android-runtime/runner.json`](../results/repository-unification-2026-10-21/solar/android-runtime/runner.json);
no se convirtió en un aprobado ni se cambió la tolerancia.

Se reprodujeron en host **todos los canales de la diferencia Android**, usando
la misma cámara, matrices, cuerpos, máscaras, estado y tiempo de la escena, con
el mismo SwiftShader. Esto aisló este fallo del proceso Java/ART y de las
superposiciones Android. El exportador usa `SolarRenderer` intacto; 28 de sus 29
hashes reconstruidos coinciden con Android. La única excepción es el caché de
órbitas en doble precisión; los vértices float realmente dibujados sí coinciden.
El fixture exacto y su exportador están conservados, con instrucciones de
reproducción, en
[`android-input-harness`](../results/repository-unification-2026-10-21/solar/android-runtime/android-input-harness/).

La referencia inicial conservaba una selección de mosaicos incorrecta para la
partición de 128 píxeles que produce la CPU: calculaba
`floor((gl_FragCoord / resolution) * grid)`. A 360 × 203, la fila normalizada cambia
en y=101, mientras el mosaico de producción cambia en y=128. En x=153..205,
`masks[1] ^ masks[4] = 1 << 20`, correspondiente a Titania. La esfera de esa luna
no intersecta esos rayos: su discriminante queda entre -89,77 y -71,33. Por tanto,
no se atribuye la diferencia a una oclusión geométrica legítima de Titania ni a
una línea concreta del controlador.

Se hicieron tres ablaciones dirigidas, sin modificar la producción:

| Comparación con las entradas exactas de Android | Píxeles distintos / 73.080 |
| --- | --- |
| Referencia inicial con lookup normalizado frente a candidato con mosaicos de 128 píxeles | 53 |
| Referencia inicial frente a candidato cambiando sólo el lookup al histórico | 0 |
| Candidato con todos los cuerpos habilitados frente al candidato de producción | 0 |
| Material y nubes completos, todos los cuerpos habilitados, frente al candidato de producción | 0 |

La última fila establece un oracle independiente: la nueva referencia ejecuta
**todos los cuerpos**, sin el lookup histórico ni el culling optimizado, y conserva
el cálculo completo de material y nubes. Se mantuvo intacto el shader de producción
`361676e4…`, incluido su índice correcto de 128 píxeles. La referencia nueva es
`1c978eab…`; la anterior `7aae77f5…` queda preservada junto a sus fallos y su parche.
En ese primer aislamiento, la evaluación completa permitió comprobar el culling
del caso 7. El pareado posterior detectó los ocho píxeles descritos en la decisión
final y llevó a retirar esa optimización de producción. La referencia completa
se conserva; no se genera automáticamente para copiar la salida del candidato.

Las evidencias de las ablaciones, hashes y el delta firmado idéntico entre host y
Android están en
[`odd-height-diagnostic`](../results/repository-unification-2026-10-21/solar/android-runtime/odd-height-diagnostic/).
El nuevo fixture de regresión
[`solar-android-case-7.json`](../solar-android/tests/fixtures/solar-android-case-7.json)
se ejecuta en CI junto a las 32 escenas anteriores, con color, identidad y
profundidad. El checker admite el tiempo y orden de texturas reales registrados;
los fixtures previos conservan sus valores por defecto. El pareado posterior con esa referencia detectó los ocho píxeles descritos
en la decisión final. Estos primeros replays host se conservan como evidencia
histórica y no se presentan como aprobación de Android.

## Validación inicial de filtrado: evidencia conservada

Se corrigieron operaciones de filtrado con resultados indefinidos en GLSL ES y se
estableció una referencia explícita que incorpora la misma corrección. Frente a
la primera referencia corregida (`7aae77f5…`), las optimizaciones conservadas
produjeron **32/32 escenas idénticas**. Estos resultados históricos se conservan
y no se atribuyen al hash nuevo sin volver a ejecutar el gate:

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
La campaña Android inicial y su fallo de 53 píxeles están descritos arriba.
**No se considera Android aprobado por las pruebas host.**

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
Parte del shader de `af0b1797573b55a739dcaef7b784eb98825ff07c`. Incorpora la
corrección de filtrado, sus guardas numéricas y la evaluación de todos los cuerpos.
Mantiene el cálculo completo del campo de material y las nubes originales. El
candidato final conserva sus omisiones de cálculos no consumidos y evalúa todos
los cuerpos. La versión previa con mosaicos de 128 píxeles queda documentada como
una optimización retirada tras el fallo reproducible. La justificación independiente del cambio de referencia
está en las ablaciones anteriores.

| Objeto | SHA-256 |
| --- | --- |
| Shader original de `af0b179` | `e99a3064ca1e0111d4e909382bf8c81cc44288398b0253d804bf433aab10156d` |
| Primera referencia, lookup histórico (supersedida) | `7aae77f535f053be36065bb1ed22a8e9956bf2107d39d9760b38e760c5feacf6` |
| Referencia completa final, todos los cuerpos | `1c978eab12807d2f791bebe4167564027e7ef705947a58f16313461224cc2753` |
| Candidato anterior con culling GPU, retirado | `361676e4882e9d5560c05d098f9f2092425d579323002082d779ff4c2b72bc50` |
| Shader final de producción, evaluación completa | `e36370ee7997d2f903529247b75a716c59fda679bfd4543b2c0c5c3ba2d6d02b` |
| Fixtures de 32 escenas, 320 × 180 | `e34e642655d489f09e08a86dbbfb8af4043ee956a5e79ebf306d876606b1c408` |
| Fixture exacto Android, 360 × 203 | `2071930394ffc1cce2b46693fd350e92531569936f5c6efe43882ce72746afe1` |

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
| Primera referencia corregida (`7aae77f5…`) y candidato con culling (`361676e4…`), un trabajador, 32 escenas cada uno | 0 | 0 / 0 |

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
repeticiones. En esos replays históricos de 32 escenas (`7aae77f5…` / `361676e4…`),
las sumas de cambios de calentamiento son 47/41, idénticas para referencia y
candidato. Esas cifras no se atribuyen al candidato final `e36370ee…`. No se
descartan divergencias persistentes: éstas hacen fallar el ejecutable.

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
afirmar que se aplicó la configuración**. Se guardan la configuración y la traza. En la campaña inicial hubo 24 aperturas
exitosas del archivo exacto; su hash, directorio de trabajo y bibliotecas cargadas
se comprobaron además por lectura del proceso. El campo automático
`emulator_processes` quedó vacío; la evidencia manual está en
`actual-driver-process.json` y no se atribuye a ese campo. El filtro de strace
incluye las grafías absoluta y relativa del mismo archivo, tras comprobar que un
único filtro absoluto omitía aperturas relativas en este entorno. La opción no cambia el shader
ni las tolerancias y se aplica igual a ambos APK.

`solar-performance.yml` usa la referencia corregida, el oracle independiente y el
gate de Mesa, incluidas las entradas exactas del fallo, antes del pareado Android.
La ejecución local con la referencia completa terminó con el fallo de ocho
píxeles descrito arriba. La verificación Android del nuevo candidato sin culling
aprobó las ocho escenas en CI/KVM, según el apartado inicial. La campaña inicial
solicitó ART `speed`, pero sus logs conservan un fallback por ClassLoaderContext;
no se afirma que toda la instrumentación se ejecutara en AOT. No se ejecutaron pruebas en un móvil físico ni se afirma
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
