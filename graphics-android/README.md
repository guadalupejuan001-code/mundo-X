# Mundo X Graphics 0.1.1 · Android

Aplicación Android del observador gráfico de Mundo X Graphics 0.1. Recibe el
`CompactCore` que prepara la aplicación, consulta su estado y reconstruye una
escena de cajas con identidad estable en OpenGL ES 3.0.

La idea y dirección de Mundo X corresponden a X. Esta entrega continúa el
paquete Graphics del 18 de octubre de 2026 y su integración Android pausada.

## Usar la aplicación

- **Arrastra** para girar la cámara; **pellizca** para acercarte; **toca** una
  caja para seleccionarla. El panel muestra su ID.
- **Vista** alterna entre panorama y calle. **Animar** activa la evolución
  visual; al abrir, la escena espera entradas para evitar trabajo continuo.
- **Objetos** selecciona entre 1.024 y 1.048.576 cajas. El inicio usa
  1.000.000. Reducir temporalmente esta cantidad conserva las ediciones de los
  IDs que quedan fuera del rango; reaparecen al ampliarlo.
- **Calidad** cambia el lado mayor de la imagen interna: 480, 640 o 960
  píxeles. Mantiene la relación de aspecto y el número elegido de objetos.
- **Qubit** elige el control. H, S, X, Y y Z actúan sobre él; CX y CZ conectan
  el control con el siguiente qubit, circularmente.
- **Acoplamiento** conecta o desconecta el efecto de los observables en la
  imagen. El núcleo conserva y sigue recibiendo sus operaciones en ambos casos.
- **Subir objeto**, **Ocultar** y **Restaurar ID** editan la caja seleccionada.
- **Restablecer** solicita confirmación y vuelve al estado inicial del núcleo
  y de las ediciones. La cámara, las puertas y las ediciones se guardan para
  retomar la sesión.

## Construir

Requiere Java 17, Gradle 8.9, Android SDK 35, NDK `27.2.12479018` y CMake
`3.22.1`. Las versiones del proyecto están fijadas en Gradle y el workflow.

```bash
cd graphics-android
gradle --no-daemon :app:assembleDebug :app:assembleDebugAndroidTest
```

La salida principal es `app/build/outputs/apk/debug/app-debug.apk`. El
workflow `Mundo X Graphics Android` entrega la APK, sus pruebas y los resultados
del emulador. La firma es de depuración; el identificador de aplicación es
`com.mundox.graphics`, independiente de XLab y MicroLab.

La APK incluye bibliotecas nativas ARM64 y x86-64. Requiere Android 8.0 o
posterior y OpenGL ES 3.0. No necesita conexión, cuentas ni permisos de red o
almacenamiento compartido.

## Núcleo, observación y conservación

`MainActivity` es dueña del núcleo. `QuantumRenderer` toma prestado el mismo
objeto `NativeCore`: su constructor y la cámara no preparan, reinician ni
miden proyectivamente el registro. Una revisión del núcleo invalida la textura
de observables; las entradas de cámara reutilizan los coeficientes anteriores.

La preparación inicial explícita usa 20 qubits en un estado de grafo. El
programa visual mantiene 60 consultas de Pauli: los 20 generadores iniciales
y 40 consultas locales X/Z. Los objetos distribuyen esas consultas mediante
su ID. Se conservan los operadores iniciales incluso después de cerrar y
reabrir la aplicación, para mantener la relación entre núcleo e imagen.

El archivo de sesión conserva la historia de puertas, el programa de
observación, cámara, tiempo, acoplamiento, selección y ediciones. La escritura
es atómica. Un archivo que no pueda restaurarse queda conservado y se informa
del problema antes de sustituirlo mediante Restablecer.

Las cabeceras originales están fijadas al commit
`ebd53fca714e7afaeeaa0044981df3565061022d` y se comprueban por hash en
`app/src/main/cpp/PROVENANCE.json`. Los adaptadores Java/JNI y de observación
amplían la integración sin cambiar esas cabeceras.

## Alcance de esta versión

El mundo es una cuadrícula finita de cajas opacas procedurales con IDs; el
observador recorre celdas e intersecta las cajas para resolver lo visible.
La representación compacta permite reconstruir geometría sin almacenar una
matriz de transformación por cada caja. Las excepciones por ID se conservan
por separado y también tienen un coste de memoria y consulta.

Las operaciones cuánticas pertenecen al conjunto Clifford del `CompactCore`.
La geometría combina sus observables con reglas de altura, tiempo y color.
Esta integración no incorpora mallas arbitrarias ni la puerta T de otros
componentes del repositorio.

La cifra de milisegundos corresponde al dibujo de un fotograma sincronizado
con `glFinish`; no incluye toda la interacción, el guardado ni la presentación
de Android. Las comprobaciones del emulador establecen funcionamiento en ese
entorno. El rendimiento en un Samsung A16 requiere medir la APK en ese teléfono.

## Verificación

Las pruebas viven en `app/src/androidTest/`. Comprueban consultas JNI,
conservación del núcleo prestado, renderizado real, causalidad de una puerta,
control de acoplamiento cero, IDs, ediciones y reanudación. El workflow conserva
el controlador utilizado, resultados estructurados, capturas y registros.

Los resultados anteriores de Graphics 0.1 en Linux se mantienen como referencia
histórica; no se presentan como rendimiento de Android.

Referencias de plataforma: [ciclo de vida GLSurfaceView](https://developer.android.com/reference/android/opengl/GLSurfaceView)
y [márgenes de sistema en Android 15](https://developer.android.com/develop/ui/views/layout/edge-to-edge).
