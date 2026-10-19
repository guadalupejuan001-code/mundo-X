# COSMIC X Engine · Android 0.3.1

Una app nueva para la computación cuántica de Mundo X: su núcleo, su motor gráfico integrado y sus experimentos originales de Android. La aplicación se instala como `com.mundox.cosmic`, junto a las aplicaciones anteriores.

**COSMIC X Engine es el título.** Dentro está la computación cuántica: el núcleo cuántico y el motor gráfico cuántico integrado. Idea, creación y dirección del proyecto: **X**. El desarrollo utiliza el código original de Mundo X con asistencia de IA.

## La nueva interfaz

- **Motor:** ciudad procedural con presentación COSMIC, fachadas iluminadas, cámara cercana, selección de IDs, edición, semilla, resolución y animación. Abre con 16.384 objetos, 1.000 qubits y una resolución máxima de 960 px. La cantidad puede subir hasta 1.048.576 objetos.
- **Núcleo:** controles H, S, X, Z, Y, CX y CZ sobre el mismo `NativeCore` que observa el motor. Muestra expectativas X/Z y la probabilidad derivada P(1) del qubit seleccionado. La cámara y la selección no realizan una medición proyectiva.
- **Laboratorios:** 14 experimentos originales dentro de esta APK, con sus modelos y controles existentes.
- **Proyectos:** nueva escena, ejemplos Bell/GHZ, copias locales y archivos portables `.cosmic`. Importar un archivo inválido conserva el proyecto activo.

El estilo COSMIC añade materiales, ventanas, luz, niebla, cielo y suelo al renderer original. La presentación es independiente del estado portable: no cambia geometría, identidades, puertas, observables ni selección. Los proyectos importados conservan su cámara.

## Modo Infinito

En **Proyectos → Crear un mundo**, el selector de núcleo ofrece **Finito**, **Infinito · pares Bell** e **Infinito · fondo cero**. También hay una tarjeta **Mundo infinito**. Los proyectos finitos existentes conservan sus qubits, sus puertas y el formato `.cosmic` v1.

En **Núcleo**, el botón de dirección admite A y B como cadenas decimales completas. Puedes escribir una dirección larga, usar su pareja Bell o elegir el ejemplo 10¹⁰⁰. Las puertas H/S/X/Y/Z actúan sobre A; CX/CZ conectan A y B. Al observar esas direcciones, el motor usa el programa fijo XX, ZZ, YY, X(A), Z(A). El panel muestra correlaciones XX/ZZ y la probabilidad local P(1), junto con el fondo y la cantidad finita de recursos activos. Cambiar de panel o consultar el fondo no materializa qubits.

**Infinito…** permite consultar recursos, medir, compactar, restablecer el fondo o ejecutar `.xi`. Las consultas y el dibujo conservan el estado y el RNG. Una medición explícita sí puede cambiar ambos. Los proyectos infinitos usan `.cosmic` v2, con un snapshot exacto de los tableaus actuales y del RNG, para continuar desde una medición guardada. La sesión privada conserva además las direcciones seleccionadas.

El caso instrumental `infinite_ui_shared_state_and_persistence` usa los controles reales de direcciones y puertas, compara píxeles acoplados/desacoplados y pasa por los callbacks SAF de guardar/abrir y la recreación de la Activity. Sus métricas se publican bajo `infiniteUi`. La validación del archivo y la sesión candidata nativa se completa antes de reemplazar el mundo activo.

## Experimentos incluidos

| Experimento | Código original |
| --- | --- |
| Circuitos | XLab: mesa, pruebas, protocolos, memoria y bitácora |
| Micromundo | Partículas, colonia y estado de cuatro qubits |
| Objetos | Regiones, IDs y ediciones de la escena |
| Ecos | Evolución, perturbación y reversión |
| Máquina de 8 bits | Registros, RAM e instrucciones sobre CompactCore |
| Interfaz cuántica | Compact, Exact T, Reduced y comparaciones |
| Campo visual | Clásico, generativo y compacto → visual |
| Partículas GPU | Escena y controles táctiles |
| Ciudad procedural | Experimento de juego |
| Escena sincronizada | GPU y estado compartido |
| Red de estados | Conexión y relay |
| Dispositivo | Lanzador experimental |
| Rendimiento | Herramientas y acceso a aplicaciones |
| Laboratorio original | GHZ, cluster, teleportación y Exact T |

Los laboratorios conservan sesiones propias según sus modelos. Los paneles Motor y Núcleo comparten una única sesión activa. La VM de 8 bits es el experimento didáctico original; el proyecto COSMIC X PC, su emulador x86 y su intento de ejecutar Windows están excluidos de esta app.

## Fuentes y compilación

El proyecto reutiliza por `sourceSets` las fuentes de `app/`, `graphics-android/` y `android-experiments/`. CMake compila cuatro bibliotecas originales: `mx_graphics`, `mundox`, `mundox_basis` y `mundox_echoes`. No incluye otro motor gráfico o de emulación.

```sh
gradle -p cosmic-android :app:assembleDebug :app:assembleDebugAndroidTest
```

Entorno de construcción: Java 17, Gradle 8.9, AGP 8.7.3, SDK 35, NDK 27.2.12479018 y CMake 3.22.1. El APK admite ARM64 y x86_64, Android 8.0 o posterior y OpenGL ES 3.0.

El workflow `cosmic-android.yml` conserva APK, certificado, huella SHA-256, auditoría del contenido y evidencias de pruebas instrumentales. Los experimentos numéricos son software ejecutado en hardware convencional. Los tiempos medidos en un emulador no representan el rendimiento de un teléfono físico.
