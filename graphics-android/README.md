# COSMIC X Engine 0.2.0 · Android

**COSMIC X Engine es el título. Dentro está la computación cuántica de Mundo X,
con su núcleo cuántico y su motor gráfico cuántico integrado.**

La aplicación continúa el código propio de Mundo X Graphics. El núcleo mantiene
el estado; el motor gráfico toma prestada esa misma sesión, consulta sus
observables y reconstruye las cajas por reglas e IDs en OpenGL ES 3.0. La idea,
creación y dirección corresponden a X, con asistencia de IA en el desarrollo.
[Procedencia e integración](../docs/cosmic_x_engine.md).

## Proyectos `.cosmic`

- **Nuevo** permite elegir nombre, 1 a 256 qubits, 1 a 1.048.576 objetos y una
  semilla de 32 bits sin signo. Prepara el estado de grafo del núcleo propio.
- **Abrir .cosmic** importa un proyecto mediante el selector de Android.
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
- **Calidad** controla la resolución interna: 480, 640 o 960 píxeles en el
  lado mayor. Conserva la relación de aspecto y la cantidad de objetos.
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

El mundo inicial usa 20 qubits, 1.000.000 de objetos y 60 observables:
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
reanudación, proyectos portables y recreación de la actividad. El workflow
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
