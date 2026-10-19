# COSMIC X Engine 0.4.0 · Android, 1.000 qubits y UHD 8K

La revisión 0.4 incorpora salida nativa **7680 × 4320 / 4320 × 7680**,
exportación PNG por regiones con memoria acotada y optimizaciones exactas del
núcleo compacto. La variante independiente se compila con
`-Pcosmic8kStandalone=true` y se instala como `com.mundox.graphics.ultra`.
**Calidad → 8K UHD** utiliza el framebuffer completo cuando el dispositivo lo
admite; **PNG 8K** genera esa resolución desde cualquier calidad de vista.
Las variantes normal, 4K y 8K comparten el perfil inicial de **1.000 qubits**.

[Uso de 8K, memoria, mediciones y alcance](docs/8k.md).

**COSMIC X Engine es el título. Dentro está la computación cuántica de Mundo X,
con su núcleo cuántico y su motor gráfico cuántico integrado.**

La aplicación continúa el código propio de Mundo X Graphics. El núcleo mantiene
el estado; el motor gráfico toma prestada esa misma sesión, consulta sus
observables y reconstruye las cajas por reglas e IDs en OpenGL ES 3.0. La idea,
creación y dirección corresponden a X, con asistencia de IA en el desarrollo.
[Procedencia e integración](../docs/cosmic_x_engine.md).

## Proyectos `.cosmic`

- **Nuevo** permite elegir nombre, 1 a 1000 qubits, 1 a 1.048.576 objetos y una
  semilla de 32 bits sin signo. Prepara el estado de grafo del núcleo propio.
- **Abrir .cosmic** importa un proyecto mediante el selector de Android.
- **Ampliar a 1000 · añadir |0⟩** aparece en proyectos más pequeños. Conserva
  estado, historial, observables, cámara, ediciones y preferencias de resolución;
  los qubits nuevos empiezan en cero. La importación por sí sola conserva el
  tamaño anterior, incluidos proyectos de 2, 8, 20 o 60 qubits.
- **Guardar** exporta estado e historial, observables originales, mundo,
  cámara, tiempo, selección y ediciones a un archivo que elijas.
- **Ejemplos** incluye Observador Bell y Ciudad GHZ, los mismos archivos que
  usa el [visor de escritorio](../graphics-desktop/README.md).

El [formato](../graphics-desktop/PROJECT_FORMAT.md) está versionado. La
importación valida el documento completo y prepara una sesión candidata antes
de reemplazar el proyecto activo. Un archivo inválido conserva el proyecto
actual. El formato v1 admite el perfil `compact-clifford` y la escena
`opaque-grid-v1`; rechaza perfiles y operaciones desconocidos.

## Controles

- **Arrastra** para girar; **pellizca** para acercarte; **toca** una caja para
  seleccionarla y consultar su ID.
- **Vista** alterna panorama y calle. **Animar** activa la evolución visual.
  La escena permanece quieta hasta recibir entradas al abrir.
- **Objetos** cambia la población hasta 1.048.576 cajas. Las ediciones de IDs
  temporalmente fuera del rango reaparecen al ampliarlo.
- **Calidad** conserva los límites de 480, 640 y 960 píxeles y añade 1280 y
  1920, ajustados al tamaño de la vista. **4K UHD** renderiza exactamente
  3840 × 2160 o 2160 × 3840, según el aspecto de la vista, incluso si la pantalla
  tiene menos píxeles. La imagen se encaja sin deformarla. Los dispositivos
  que no pueden asignar ese destino conservan el modo anterior e indican el motivo.
- **Guardar PNG** exporta el framebuffer de color completo mediante el selector
  de archivos de Android. El PNG conserva la resolución real indicada y no incluye
  los controles. El renderizado y la codificación avanzan por bloques en el hilo
  gráfico, con memoria acotada y la escena fija durante la preparación.
  **PNG 8K** guarda 7.680 × 4.320 o 4.320 × 7.680 píxeles desde cualquier calidad
  de vista. La preparación muestra su progreso y puede cancelarse.
- **Estilo** permite alternar entre la presentación original y la presentación
  cósmica que ya forma parte del motor. Ambas conservan los mismos objetos e IDs.
- **Qubit** elige el control; H, S, X, Y y Z actúan sobre él. CX y CZ conectan
  el control con el siguiente qubit, circularmente; requieren al menos dos.
- **Acoplamiento** conecta o desconecta el efecto del núcleo en la imagen.
  Conserva el estado y sigue aceptando puertas. Los proyectos admiten una
  intensidad propia entre −0,25 y 0,25.
- **Subir objeto**, **Ocultar** y **Restaurar ID** editan la caja seleccionada.
- **Restablecer** prepara el estado de grafo con la cantidad actual de qubits
  y borra las ediciones de objetos, después de confirmarlo.

La sesión privada se guarda de forma atómica para retomarla. El historial
nativo es la fuente de verdad; el guardado Java conserva ese historial junto
con los controles visuales. Se mantiene la lectura de las sesiones de Graphics
0.1.1. Los archivos `.cosmic` transportan el proyecto entre plataformas.

## Núcleo y observación

Android y escritorio compilan directamente
[`native/src/graphics_core_bridge.cpp`](../native/src/graphics_core_bridge.cpp)
con los encabezados canónicos de `native/include/mundo_x`, sin copias separadas
del núcleo. La [procedencia](app/src/main/cpp/PROVENANCE.json) registra los
hashes actuales y el punto de partida de Graphics.

`MainActivity` posee el `NativeCore`. `QuantumRenderer` observa ese mismo
objeto: la cámara y el dibujo conservan el estado y el historial. La caché se
invalida al operar o restaurar, incluso si el proyecto restaurado tiene el
mismo número de revisión.

El mundo inicial usa 1.000 qubits, 1.000.000 de objetos y 3.000 observables:
los generadores iniciales y las consultas locales X/Z. Los proyectos
importados conservan su propio conjunto de observables, incluso cuando
hay menos objetos que canales. El observador obtiene valores esperados
de Pauli sin medición proyectiva.

## Construir

Requiere Java 17, Gradle 8.9, Android SDK 35, NDK `27.2.12479018` y CMake
`3.22.1`. Las versiones están fijadas en Gradle y el workflow.

```bash
gradle -p graphics-android --no-daemon :app:assembleDebug :app:assembleDebugAndroidTest
```

Salida: `graphics-android/app/build/outputs/apk/debug/app-debug.apk`.
El workflow **COSMIC X Engine APK and Android validation** entrega
`COSMIC-X-Engine.apk`, su firma, hashes y evidencias del emulador.

La APK es de depuración, contiene ARM64 y x86-64 y conserva el identificador
`com.mundox.graphics`. Requiere Android 8.0 o posterior y OpenGL ES 3.0.
Los selectores de documentos usan el acceso que Android concede al archivo
elegido; la aplicación no necesita permisos de red ni acceso general al
almacenamiento. Instalar una actualización sobre otra APK requiere la misma
firma. Los builds de depuración de distintos entornos pueden usar claves distintas.

## Verificación y alcance

`app/src/androidTest/` prueba JNI, observación sin mutación, renderizado real,
causalidad de puertas, control con acoplamiento cero, IDs, ediciones,
reanudación, proyectos portables y recreación de la actividad. La suite común
exige **18 casos**, incluidos GHZ de 1.000 qubits, puertas sobre el qubit 999,
ampliación de proyectos anteriores y rechazo de una puerta concurrente durante
la preparación. La variante 4K añade cuatro casos (**22 en total**) y la 8K
otros cuatro (**26 en total**). El workflow
conserva resultados, imágenes, proyectos exportados y registros. Un comprobador
de escritorio reproduce los estados exportados por Android con el núcleo
compartido y con la referencia densa.

El perfil gráfico portable usa operaciones Clifford de CompactCore y cajas
opacas. Reduced, Factorized y las fases generales siguen integradas en el
repositorio y en sus herramientas; esta interfaz gráfica todavía no ofrece
esos perfiles. El cómputo cuántico es por software y la imagen usa una GPU convencional.

El tiempo mostrado mide el dibujo de un fotograma sincronizado con `glFinish`.
Las pruebas identifican el controlador utilizado. El rendimiento en un Samsung
A16 se determina ejecutando la APK en ese dispositivo.

## Entrega 4K separada

La variante instalable **COSMIC X Engine 4K** usa el identificador
`com.mundox.graphics.uhd` y se construye con `-Pcosmic4kStandalone=true`.
Puede instalarse junto con Graphics, la aplicación unificada y el sistema solar.
El build normal mantiene `com.mundox.graphics`.

El desarrollo 4K parte de la versión unificada
`e58e9805987ded53cefe454a1f66bc7ad778d16e` y conserva su núcleo nativo y sus shaders.
El workflow dedicado **COSMIC X Engine 4K** compila la variante, conserva las
pruebas gráficas anteriores y exige evidencia de los framebuffers UHD reales.

[Alcance, memoria, compatibilidad y uso de 4K](docs/4k.md).
