# Solar: integración, APK y medidas reproducibles

## Actualización del 21 de octubre de 2026

Este documento conserva la recuperación y las medidas del 20 de octubre.
Los apartados que describen el remoto sin publicar, el permiso de aquella
sesión y el gate Android fallido corresponden a ese corte histórico. La
[consolidación posterior](repository-unification-2026-10-21.md) y
[PR25](https://github.com/guadalupejuan001-code/mundo-X/pull/25) registran la
publicación autorizada y el estado de integración posterior.

El candidato `b9ec7452307c610448adbb1217e1566aa88fa79b` define los gradientes
del filtrado y evalúa todos los cuerpos en GPU. Se retiró el descarte por
teselas tras diferencias reproducibles frente a la referencia completa; el
planificador y sus cachés permanecen como infraestructura, sin atribuirles
ahorro GPU en esta versión. Las otras mejoras de materiales, efemérides,
observaciones y exportación se conservan. La [validación actual](solar-shader-determinism-2026-10-21.md)
identifica cada referencia, shader y ejecución, incluidos los fallos intermedios.
Las cifras de rendimiento del resto de este informe pertenecen a sus fuentes
históricas y no se trasladan al shader nuevo.

## Versión y alcance

Esta ronda reanuda Solar por petición explícita del usuario. La referencia es
`af0b1797573b55a739dcaef7b784eb98825ff07c`, que ya conserva la compactación de
reglas, las consultas optimizadas, los espejos y las mejoras de física y de
renderizado de las rondas anteriores. Rama local de integración:
`codex/solar-integration-20261020`. No se sustituyó el motor por otro proyecto.

El remoto sigue teniendo `main` en
`7b028db2344912b2ab86a0affa2a1fe56180922b`. La integración local contiene ese
commit y los trabajos posteriores. El PR #25 continuaba abierto y en borrador
en `aab0930378796ebfd42f9629e2575108ca94fcff` al consultar su estado. Se
revisaron las 35 ramas remotas al comenzar y al cerrar la revisión.

El registro de la sesión anterior documenta que la revisión automática de
permisos rechazó el `git push`: exigía autorización explícita para publicar y
señaló una denegación anterior. Ese bloqueo de publicación se conserva en este
cierre. **El remoto, sus PR y `main` no se han modificado.** Los commits y la
recuperación quedan preparados para revisión y publicación posterior.

Las APK originales recuperadas se identifican por hash en la entrega. Sus
shaders no coinciden con la referencia af0b179, por lo que no se les atribuyen
los tiempos de «antes». La comparación compila de nuevo la misma referencia
recuperable con el mismo JDK, SDK, NDK y arnés que la versión nueva.

## Recuperación y cierre del 20 de octubre

Se recuperó la rama completa de la sesión interrumpida y se continuó sobre
una copia aislada. La ascendencia Git confirma que conserva `main@7b028db`,
el PR #25 en `aab0930`, las rondas de compactación, evaluación, espejos,
optimización del sistema y renderizado, y la integración Solar. No faltaba
aplicar otra vez esos parches. El código de producción recuperado y las APK
entregadas corresponden a `c2c0c5f`; los commits de cierre añaden el informe,
las evidencias y las correcciones del ejecutor de pruebas.

La carpeta antigua de entrega contenía binarios anteriores a ese commit. Se
seleccionaron las compilaciones shipping posteriores, se verificaron sus
firmas, manifiestos, ABI y assets, y se contrastaron 61 archivos de fuentes
con el commit registrado. Son compilaciones recuperadas y comprobadas; no se
presentan como una compilación independiente repetida durante el cierre.
`APK-PROVENANCE.json` contiene los hashes completos y la relación con las
fuentes y los logs de compilación.

La APK principal de entrega es **COSMIC X Solar Pruebas 1.3.0**,
`com.cosmicx.solar.review`, con código de versión 4 y soporte ARM64/x86_64.
Permite instalar esta revisión junto a Solar anterior. Se verificó que el
certificado de las APK originales recuperadas difiere del de estas
compilaciones; por ello, la variante normal incluida en la recuperación no
es una actualización de firma compatible con aquellas instalaciones.

Se volvieron a comprobar los hashes de las seis fuentes y herramientas de
la campaña `shipping/`, así como sus totales y diferencias de píxel. Todos
coinciden con el código recuperado. Las cifras del anfitrión de este informe
son los resultados conservados de esa campaña; no se presentan como una
nueva medición del teléfono o del emulador durante la recuperación.

El ejecutor Android ahora guarda un informe inicial y la etapa activa antes
de operaciones largas, escribe los resultados de forma atómica, tolera un
timeout transitorio de ADB dentro del mismo límite de arranque y conserva la
causa principal aunque falle la limpieza. Clasifica por separado una
referencia inestable, entradas distintas y diferencias de píxel. Mantiene
la exigencia de ocho escenas y ocho capturas exactas, sin convertir un fallo
en aprobación. Dos pruebas con cuatro subcasos de timeout pasaron; quedaron
conectadas al workflow, cuyo shell también propaga correctamente los fallos
del pipeline de pruebas del anfitrión.

### Resultado de la comprobación Android que estaba pendiente

La ejecución se completó con Android 10 x86_64, SwiftShader, TCG y
2.048 MiB de RAM de invitado. Se instalaron la APK normal de referencia y la
normal mejorada, con los instrumentos más recientes de la sesión recuperada.
Los hashes de la APK mejorada y su instrumento coinciden con los de la
entrega. La variante `.review` comparte las fuentes y los assets comprobados;
su verificación en este cierre fue estática. El emulador y ADB terminaron
con el ejecutor.

| Comprobación Android | Referencia | Mejorada |
| --- | ---: | ---: |
| Escenas completadas | 8 de 8 | 8 de 8 |
| Cuadros cronometrados | 40 | 40 |
| Cuadros totales contados por el renderer | 66 | 66 |
| Ciclos de creación y destrucción adicionales del núcleo | 128 | 128 |
| Escenas cuya captura repetida fue exacta | 3 de 8 | 3 de 8 |
| Escenas con variación entre capturas repetidas | 5 de 8 | 5 de 8 |
| Resultado de instrumentación | Fallo de repetibilidad | Fallo de repetibilidad |

No se registraron caída de app, error de creación GLES ni fallo de los
invariantes de sólo lectura del núcleo durante este recorrido. **La puerta
de exactitud visual quedó fallida**, con salida 1 del ejecutor; no se cambió
el criterio para presentarla como aprobada.

Cada captura Android mide 360×203. Entre las ocho capturas de referencia y
las ocho mejoradas, **99 de 584.640 píxeles difieren**. Son 261 canales,
sin diferencias de alfa; la diferencia máxima es 96 en un canal de 0 a 255.
Sólo una de las ocho capturas A/B es completamente idéntica. Los hashes de
los campos de entrada registrados coinciden en las comparaciones.

| Escena | Píxeles distintos al repetir referencia | Al repetir mejorada | Entre versiones |
| --- | ---: | ---: | ---: |
| Tierra, preparación 0 | 6 | 12 | 7 |
| Tierra y campos, preparación 1 | 1 | 1 | 2 |
| Saturno | 6 | 3 | 4 |
| Sol | 33 | 24 | 24 |
| Entrada a Tierra | 0 | 0 | 0 |
| Entrada a Júpiter | 0 | 0 | 8 |
| Luna, escala real | 5 | 2 | 1 |
| Urano | 0 | 0 | 53 |

La referencia también cambia al repetirse, pero eso no explica por sí solo
todos los pares: Júpiter y Urano son estables dentro de cada APK y difieren
entre versiones. **La causa definitiva de esas diferencias y la ausencia
de regresión visual Android no están demostradas.** La corrección de la
selección de teselas y las optimizaciones de material modifican el shader;
el registro no aísla todavía sus efectos en este controlador. No procede
atribuir todos los cambios al emulador o a una sola optimización.

Se solicitó compilación ART `speed` y el sistema respondió `Success`, pero
el log de instrumentación registra un desajuste del contexto de carga y
extracción alternativa desde APK. Esa solicitud no demuestra que todas las
clases se ejecutaran AOT ni descarta ART como factor. Esta ejecución usó el
runner ya cargado de `c2c0c5f`; las correcciones de tolerancia a timeouts del
commit de cierre se comprobaron por separado.

`android-closure-runner.json` y `android-closure-comparison.json` conservan
el resultado. Las capturas RGBA, memoria, instrumentación y logcat completos
están incluidos en el paquete de recuperación. El fallo inicial de apertura
del socket local de ADB quedó registrado aparte y se resolvió mediante la
escalación autorizada para ejecutar el emulador.

La comparación exacta de 80.688.032 píxeles hasta 8K sigue correspondiendo
al **shader del anfitrión** y sus fixtures. No se extiende ese aprobado a
la app Android completa. Esta entrega permite probar la integración y
conserva sus mejoras comprobadas; no declara superada la validación visual
estricta de Android ni una mejora general de FPS.

## Conexiones reales

| Componente | Qué ejecuta y qué comparte |
| --- | --- |
| `QuantumSolarBridge` | Crea `NativeCore.infinite`, prepara ocho sitios y dos reglas globales, solicita un lote de trece observables y publica una observación inmutable. No deduce órbitas ni fuerzas a partir de qubits. |
| `NativeCore` → JNI compartido → `InfiniteSession` | Una sesión nativa real conserva la preparación finita, reglas, época y revisión. La observación es de sólo lectura. El bloqueo de la sesión sigue serializando sus operaciones; no se añadieron hilos internos. |
| `EtherCore` / `InfiniteCore` | Ether describe/evalúa reglas globales sobre la preparación que posee InfiniteCore. El dominio lógico puede extenderse sin enumerar cada dirección; la preparación activa, los observadores y sus temporales son finitos. |
| `CompactCore` | Cada componente materializado de InfiniteCore contiene el CompactCore original. Solar elige la sesión Infinite, que sí usa estos tableaux finitos internamente; no crea una segunda sesión Compact independiente. |
| `SolarRenderer` | Consume ocho coeficientes locales y cuatro globales de una misma observación, junto a geometría, cámara y entorno. El quinto observable global permanece disponible para verificación. |
| `SolarData` → `Ephemeris` | Calcula las posiciones físicas del catálogo de 23 cuerpos. El lote reutiliza la posición del padre sin cambiar el orden aritmético de cada resultado. |
| `SolarVisualEther` | Deriva candidatos geométricos por tesela de pantalla. Comparte la intención de reconstruir sólo lo necesario, pero no es otra sesión cuántica ni el almacén del estado orbital. |
| `solar.frag` | Conserva el recorrido original de eclipses. Evita evaluar ruido de material que no se usa, sin reducir resolución, geometría ni muestras atmosféricas. |
| `SolarOcclusion` experimental | Vive en `solar-android/tests`, fuera de la APK. Se verificó su corrección geométrica, pero las medidas no justificaron conservar su coste de preparación y ramas en producción. |
| `SolarEnvironment` / `SolarFlightPhysics` | Entorno y física local de entrada atmosférica. La integración física conserva sus resultados válidos; ahora rechaza entradas inválidas y confirma un paso completo o ninguno. |
| `SolarPng` → `StreamPng` de Graphics | Exporta las filas RGBA mediante el codificador original compartido. Elimina el Bitmap completo adicional; no cambia los píxeles. |
| COSMIC, Graphics, PC, MicroLab | Conservan las integraciones y backends documentados en las rondas anteriores. La física 2D de PhysicsWorld no pasa a controlar las órbitas de Solar ni toda la geometría 3D. |

La estructura ojo + reconstrucción bajo demanda continúa: un triángulo de
pantalla genera rayos y el shader reconstruye esferas, anillos, materiales y
atmósferas a partir del catálogo. Solar tiene un shader propio. El índice de
excepciones del mundo de cajas y los espejos planos de Graphics no se copiaron
a este shader esférico ni se les atribuye automáticamente su aceleración.

## Correcciones conservadas

1. **Publicación coherente del núcleo.** Antes podían leerse campos y arrays de
   diferentes revisiones mientras cambiaba un preset. Una sola referencia
   `volatile` publica coeficientes y metadatos inmutables. El renderer retiene
   esa observación y copia sus coeficientes a buffers propios sin asignar arrays.
   Los getters de compatibilidad devuelven copias para impedir que un consumidor
   corrompa el estado publicado.
2. **Efemérides compartidas.** Un espacio de trabajo conserva las posiciones
   intermedias y calcula cada padre una vez por lote. Cuando fecha y escala son
   exactamente iguales, el renderer conserva la geometría ya calculada. Las
   fechas nuevas siguen haciendo la evaluación; no se cuantiza el tiempo.
3. **Visibilidad reutilizable.** La máscara de teselas y su buffer directo se
   conservan. La subida GLES sólo se omite cuando dimensiones y contenido son
   idénticos. Se corrigió la conversión píxel→tesela en las últimas teselas
   parciales. Se libera la textura de máscaras al cerrar el renderer y se
   invalida correctamente tras recrear el contexto.
4. **Trabajo de fragmento evitado.** Se evita ruido procedural cuyo resultado
   no se usa y el cálculo de nubes de Tierra que no aparecen durante la entrada.
   Se probaron también cápsulas conservadoras para descartar eclipsantes, con
   extracción de bits, índices uniformes y un atajo para máscaras vacías. La
   corrección pasó, pero las aceleraciones no fueron consistentes entre vistas
   y campañas. **Se retiró todo ese filtro de producción**, incluido su cálculo
   CPU y la subida de uniformes. Los cuatro experimentos quedan reproducibles
   en los resultados; la versión entregada usa el eclipse original.
5. **Física con confirmación completa.** Se validan cuerpo, entorno, radios,
   estado y resultados finitos. Un desbordamiento no deja media actualización.
   Esto mejora la corrección; no se presenta como una aceleración física.
6. **Exportación sin segunda imagen completa.** Se comparte `StreamPng` con
   Graphics. A 7680×4320 evita un Bitmap de 132.710.400 bytes de píxeles
   (126,56 MiB), además de su sobrecarga. Conserva RGBA, alfa y orientación.

Las APIs públicas de núcleo, formatos de snapshots, proyectos y reglas no
cambian. La nueva `Observation` y `Ephemeris` son interfaces internas/aditivas.
La máscara prestada por `SolarVisualEther.Workspace` es válida hasta su siguiente
actualización; `plan()` conserva el contrato anterior de arrays independientes.

## Exactitud, memoria y límites comprobados

Los registros completos están en `results/solar-integration-2026-10-20/`.
La campaña de la versión entregada es **`shipping/`**, sobre el código
`c2c0c5f727f56c2491c26fea074109e6eae50df2`; los directorios/archivos anteriores
registran las variantes que se replantearon. El informe y el empaquetado se
confirman en commits posteriores sin cambiar ese código de producción.

### Rendimiento del anfitrión

GLES 3 de Mesa llvmpipe, un CPU asignado, cuadros completos y `glFinish` antes
de terminar cada muestra. Misma escena, precisión, texturas, fecha, cámara y
coeficientes consultados al JNI original. No se devuelve una imagen guardada.
Se registra aparte la primera observación de cada vista nueva.

| Carga | Antes, ms | Después, ms | Alcance |
| --- | ---: | ---: | --- |
| 12.000 fechas nuevas × 23 cuerpos | 126,753 | 85,063 | Mediana de cinco pares tras un par de calentamiento; orden alternado |
| Shader 320×180 | 11,541 | 11,525 | 32 escenas, cinco muestras por escena; prácticamente igual |
| Shader 961×541 | 106,350 | 108,690 | 32 escenas, tres muestras; 2,2 % peor en esta campaña |
| Shader 1920×1080 | 464,574 | 417,553 | Seis escenas, tres muestras |
| Shader 3840×2160 | 2.439,338 | 2.324,103 | Dos escenas, dos muestras |
| Shader 7680×4320 | 6.451,317 | 6.238,655 | Una escena y una muestra; frontera, no rendimiento sostenido |

Las cifras gráficas son medianas de medianas por escena. Las cargas entre
resoluciones tienen diferentes selecciones para respetar el presupuesto: no
constituyen una curva controlada de escalado. Sí se mantiene idéntica la carga
antes/después dentro de cada fila. **No se concluye una ganancia general de
FPS.** Las campañas exploratorias también exhiben variación del anfitrión;
los resultados crudos permiten comprobarla. Se retiró el filtro de eclipses
que no justificaba su complejidad y coste adicional.

El lote orbital reduce la latencia **32,89 %** y las asignaciones rastreadas
**44,04 %**, de 41.856.000 a 23.424.000 bytes para la misma carga. Conserva el
checksum y cada coordenada; sigue habiendo temporales de cálculo. Las fechas
son distintas, por lo que esta mejora no procede de reutilizar una respuesta
para la misma fecha. La reutilización de fecha idéntica del renderer es otra
optimización separada.

En el shader entregado se compararon **80.688.032 píxeles de color**, más otros
tantos de identidad y profundidad cuantizada a 24 bits: cero diferencias en
los tres resultados, 242.064.096 comparaciones de píxel en total. Los procesos
terminaron dentro de 2 GiB de espacio virtual y 180 s de CPU por resolución.
El máximo RSS del comparador fue **1.083.660 KiB (1.058,26 MiB)**, a 8K; incluye
las dos versiones, los programas auxiliares y buffers de comparación. No es
la memoria de la app Android ni sólo la memoria del núcleo.

| Prueba | Resultado |
| --- | --- |
| Datos Solar históricos | 567 comprobaciones aprobadas |
| Visibilidad Solar histórica | 41.567 comprobaciones aprobadas |
| Efemérides, máscaras y experimento de eclipses | 10.239.383 comprobaciones; incluye 9.956.160 del filtro experimental retirado de la APK |
| Física antes/después | 588.800 valores de estado idénticos bit a bit; entradas inválidas y rollback aprobados |
| Java → JNI → núcleo real | 301.014 comprobaciones; 100.000 lecturas concurrentes con 1.200 cambios de preset y 1.000 sesiones adicionales; 2.205 construcciones en total |
| PNG pequeño, decodificador ImageIO independiente | 553.061 píxeles exactos, alfa, orientación y propiedad del stream comprobados |
| PNG 8K, decodificador Pillow independiente | 33.177.600 píxeles, cero canales diferentes |

La exportación 8K completó con heap Java de 256 MiB y límite de espacio virtual
de 2 GiB: RSS máximo de 184.088 KiB en el proceso codificador y 64.104 bytes de
asignaciones Java adicionales rastreadas durante la escritura. Esta última
cifra excluye la captura RGBA ya existente y la memoria nativa de zlib. Una
escritura tardó 472,98 ms en este anfitrión; no es una comparación con el antiguo
compresor Android ni una cifra del teléfono.

**La prueba con heap de 192 MiB falló al reservar la captura RGBA**, antes de
codificar, con la ocupación y distribución de heap de esa ejecución Serial.
Se conserva el fallo. La siguiente mejora para ese límite es
capturar teselas/filas y escribirlas al stream sin acumular toda la imagen;
requeriría revisar el contrato asíncrono de `Capture` y su guardado en Android.
El trabajo actual elimina la copia duplicada, no la captura original.

El emulador Android 15, con 2 GB y sin KVM, agotó los 300 segundos de arranque;
no ejecutó las APK. Android 10 sí arrancó con los mismos 2 GB configurados y
SwiftShader. Un emulador con 2 GB de RAM de invitado no es un proceso completo
de 2 GB de RSS: el sistema anfitrión y su GPU por software tienen sobrecarga.
Las pruebas GLES del anfitrión usan por separado un límite duro de 2 GiB de
espacio virtual, que tampoco equivale al heap disponible de una app móvil.

La primera comparación Android completó ambas APK pero detectó diferencias.
El arnés fijó después la fecha antes de elegir cámara y la orientación lunar
que los constructores derivaban de la hora de arranque. Estas condiciones se
aplican por igual al código original y al nuevo, dentro de la instrumentación.
Los hashes de entradas y la repetición de capturas permiten distinguir entradas
distintas de cambios reales en el resultado. No se usaron las primeras cifras
exploratorias como prueba de aceleración.

## Cobertura y pendientes

| Archivos/rutas | Cobertura de esta ronda |
| --- | --- |
| `QuantumSolarBridge`, `NativeCore`, JNI e InfiniteSession | Compilación ARM64/x86_64; JNI real en anfitrión y x86_64 Android; presets, lectura concurrente y destrucción de sesiones |
| `SolarData`, `SolarEnvironment`, `SolarFlightPhysics` | Pruebas Java, referencia anterior de física, fechas nuevas y padres/lunas; integración Android de escenas y entradas atmosféricas |
| `SolarRenderer`, `SolarVisualEther`, shaders y texturas | Compilación GLES, imágenes, máscara, cámara, nuevas vistas y cuadros repetidos; registros por resolución y controlador |
| `tests/SolarOcclusion` y variantes de shader | Experimento de eclipses con referencia independiente, retirado tras medirlo; no se presenta como aceleración entregada |
| `SolarPng`, `StreamPng`, `TilePlan`, `RenderResolution` | Compilación Android y decodificación independiente de exportación hasta 8K |
| `SolarActivity`, `SolarSurface`, manifiesto, Gradle, CMake | Arranque e instrumentación de la APK, variantes normal/review, bibliotecas ABI, permisos y empaquetado; no se afirma cobertura de cada gesto o diálogo |
| `SolarMapView`, opciones de UI, MediaStore | Inspección de integración y compilación; no una campaña exhaustiva de interacción/guardado Android |
| Núcleo nativo, Graphics, COSMIC, PC y MicroLab anteriores | Procedencia y regresión comunes; consultar también los informes anteriores. No se repitió en un teléfono toda la campaña del repositorio |

No se han medido FPS, temperatura, batería, consumo eléctrico ni estabilidad de
horas en un Samsung A16. Tampoco se demuestra rendimiento comercial, usuarios,
ventas, patentabilidad o capacidad para ejecutar cualquier circuito cuántico.
La representación lógica amplia no elimina el trabajo por píxel, las fuerzas
activas, las excepciones ni el crecimiento del evaluador. Solar conserva el
catálogo de 23 cuerpos y el límite de 24 del shader; no se subió un número para
presentarlo como mayor capacidad física.

## Repetición

Requisitos usados: JDK 17.0.20.1, Gradle 8.9, Android SDK 35, NDK
27.2.12479018, CMake 3.22.1; para el anfitrión C++17, Python, NumPy/Pillow y
GLES 3 por EGL. Las dependencias se descargaron una vez; las compilaciones
comparativas posteriores se hicieron offline.

```sh
# Desde la raíz, con JAVA_HOME y ANDROID_HOME configurados.
bash tools/solar_host_checks.sh
gradle -p solar-android --no-daemon --max-workers=2 \
  :app:assembleDebug :app:assembleDebugAndroidTest

# Paquete de revisión instalable junto al original, sin sustituirlo.
gradle -p solar-android --no-daemon --max-workers=2 -PsolarReview=true \
  :app:assembleDebug :app:assembleDebugAndroidTest

# Referencia de producción intacta; sólo se comparte el arnés de prueba.
git worktree add --detach ../solar-before af0b1797573b55a739dcaef7b784eb98825ff07c
cp solar-android/app/src/androidTest/java/com/mundox/solar/{SolarInstrumentation,SolarPerformanceChecks}.java \
  ../solar-before/solar-android/app/src/androidTest/java/com/mundox/solar/
gradle -p ../solar-before/solar-android --no-daemon --max-workers=2 \
  :app:assembleDebug :app:assembleDebugAndroidTest
```

Guardar las APK normales antes de compilar la variante review, o emplear un
segundo worktree como en esta entrega: Gradle comparte la carpeta de salida.
`tools/solar_performance_android.py` espera las dos variantes normales.

```sh
python3 tools/solar_performance_android.py --sdk "$ANDROID_HOME" \
  --baseline ../solar-before/solar-android/app/build/outputs/apk \
  --after solar-android/app/build/outputs/apk --output android-pair \
  --system-image 'system-images;android-29;default;x86_64' --no-accel --boot-seconds 180

# Para otro sistema instalado o KVM, ajustar system-image y omitir no-accel.
# Cada GL worker del anfitrión impone 2 GiB y 180 segundos de CPU.
java -Xmx256m -Djava.library.path=build/solar-host/jni \
  -cp build/solar-host/classes com.mundox.solar.SolarRenderFixtures 961 541 fixtures.json
python3 tools/solar_shader_check.py --fixtures fixtures.json --repeats 3 --output shader-961.json

# Campaña completa por resoluciones, sin procesos de medición simultáneos.
python3 tools/solar_campaign.py --output results/solar-repeat
```

No ejecutar compilaciones ni emuladores simultáneamente con los cronómetros
del anfitrión. La workflow `solar-performance.yml` deja preparada la repetición
en GitHub, pero no se ejecutó remotamente porque la publicación está bloqueada.
