# COSMIC X Solar

Explorador del sistema solar para Android, construido como una nueva aplicación
de **COSMIC X Engine**. Integra el núcleo original de Mundo X y amplía su
observador gráfico con planetas esféricos, anillos, órbitas y capas magnéticas.

## Explorar

El catálogo incluye el Sol, los ocho planetas y catorce lunas: Luna, Ío, Europa,
Ganímedes, Calisto, Titán, Encélado, Tritón, Fobos, Deimos, Rea, Titania, Oberón y
Miranda. La aplicación funciona sin conexión y no solicita permiso de Internet.

| Control | Acción |
|---|---|
| Arrastrar con un dedo | Girar la cámara alrededor del destino o mirar durante el vuelo |
| Pellizcar | Acercar o alejar la cámara |
| Arrastrar dos dedos | Desplazar la cámara |
| Nombre de planeta / Lunas | Viajar y seguir un cuerpo |
| Sistema | Ver el conjunto de órbitas |
| Volar | Activar un control táctil para avanzar, retroceder y desplazarse |
| Campos | Mostrar el entorno magnético del destino |
| Tiempo | Pausar, acelerar, invertir el tiempo y escoger una fecha |
| Observatorio | Cambiar capas, proporciones y calidad; consultar núcleo y fuentes |
| Ficha → Sonda | Evaluar el dipolo ideal a distintas alturas y latitudes |
| Círculo superior | Ocultar los controles para una vista despejada |

**Exploración** amplía los cuerpos y comprime las distancias para facilitar su
lectura. **Proporciones reales** conserva radios y distancias relativos; los
botones de destino permiten acercarse a cuerpos que a escala del sistema
completo ocuparían menos de un píxel. La escala de visualización no cambia los
datos del catálogo.

La calidad Ahorro, Equilibrada o Alta limita la dimensión mayor del framebuffer
a 640, 960 o 1440 píxeles. Es una preferencia de resolución, no una promesa de FPS.

## Modelos de naturaleza

- **Planetas:** elementos keplerianos y variaciones seculares de NASA/JPL,
  intervalo 1800–2050. Posiciones en doble precisión; coordenadas relativas a
  la cámara para el observador GLES. Corrección aproximada del baricentro
  Tierra–Luna.
- **Lunas:** dimensiones y períodos de referencia; elipses simplificadas y
  fases ilustrativas. Estas fases no constituyen efemérides para predecir
  eclipses, tránsitos o aterrizajes.
- **Superficies:** mapas de NASA para Tierra, Luna y Júpiter; detalles
  procedurales en los demás cuerpos. La orientación de textura y su iluminación
  se usan para la exploración; la app no ofrece cartografía de navegación.
- **Magnetismo:** dipolos de referencia para campos intrínsecos; representación
  del entorno inducido de Venus y del entorno inducido más magnetismo de la
  corteza de Marte. Ganímedes dispone de un dipolo propio de referencia.
- **Sonda:** evalúa el campo exterior de un dipolo ideal en microteslas. Su
  intensidad disminuye como `1/r³`; en el polo es el doble que en el ecuador a
  la misma distancia. No reconstruye las corrientes externas del plasma.
- **Viento, arcos, colas y corona:** trazas visuales para entender geometrías.
  No son una solución magnetohidrodinámica ni observaciones en directo.

Las líneas magnéticas hacen visible una estructura que los ojos no ven
directamente. Su color, densidad y longitud son decisiones de visualización.
La simulación usa aproximaciones concretas y no pretende reproducir todos los
procesos físicos de la naturaleza.

Consulte [datos, convenciones y fuentes](docs/solar-science.md) y
[procedencia de las imágenes](app/src/main/assets/science-assets.json).

## Qué hace el núcleo de COSMIC X

Se compilan directamente, sin una copia divergente:

- `graphics-android/app/src/main/java/com/mundox/graphics/NativeCore.java`;
- `graphics-android/app/src/main/cpp/graphics_jni.cpp`;
- `native/src/graphics_core_bridge.cpp` y las cabeceras originales de Mundo X.

La cadena activa es:

`SolarActivity → QuantumSolarBridge → NativeCore → JNI → EngineSession / CompactCore`.

`QuantumSolarBridge` prepara un registro de ocho qubits mediante puertas
originales y consulta ocho observables de Pauli. Los modos Base, Superposición,
Entrelazado y Fase cambian ese estado. El observador consume sus resultados
para el muestreo y la animación de trazas. Consultar el estado no añade puertas
ni colapsa el registro. La estructura nativa ocupa una cantidad pequeña de
memoria; la cifra mostrada cuenta únicamente esa estructura, **no la RAM total
de la aplicación**.

El observador solar amplía el enfoque de rayos de cámara de `QuantumRenderer`
y `eye.frag`: intersecciones analíticas con esferas y anillos, texturas,
iluminación, atmósferas y geometría de líneas. Emplea GLES del dispositivo.
No incorpora Unity, Unreal, Godot, Three.js ni otro motor o simulador externo.

El acoplamiento cambia las trazas; no calcula las efemérides ni determina las
constantes magnéticas. La integración por sí sola no demuestra aceleración
cuántica ni emula la dinámica cuántica microscópica del sistema solar.

## Compilar

Requisitos de la compilación reproducible:

| Componente | Versión |
|---|---|
| Java | 17 |
| Gradle | 8.9 |
| Android Gradle Plugin | 8.7.3 |
| Android SDK / target | 35 |
| Android mínimo | 26, Android 8 |
| NDK | 27.2.12479018 |
| CMake | 3.22.1 |
| ABI | arm64-v8a y x86_64 |

```sh
gradle -p solar-android --no-daemon :app:assembleDebug :app:assembleDebugAndroidTest
```

El paquete es `com.cosmicx.solar` y su nombre visible es **COSMIC X Solar**.
Puede coexistir con `com.mundox.graphics`. Las compilaciones de desarrollo usan
firma debug; una actualización posterior requiere conservar el mismo
certificado o reinstalar esa aplicación.

El workflow [solar-android.yml](../.github/workflows/solar-android.yml) construye
el APK, verifica firma y ABI, registra SHA-256 y procedencia del núcleo, y lo
prueba en Android 15 con el driver de software SwiftShader y 2 GB configurados
para el emulador. Esas comprobaciones no miden el rendimiento de un Samsung A16.

La evidencia Android registra pruebas, capturas de los framebuffers reales,
logcat, memoria de proceso y el commit exacto del APK. Las pruebas comprueban
el arranque nativo, las órbitas, los observables, el cambio visual por campos y
por estado del núcleo, navegación, tiempo, proporciones, ciclo de vida y
límites de resolución.

## Créditos de los datos visuales

- Tierra: NASA/Goddard Space Flight Center Scientific Visualization Studio;
  Blue Marble, Reto Stockli (NASA/GSFC) y NASA Earth Observatory.
  https://svs.gsfc.nasa.gov/2915/
- Luna: NASA Scientific Visualization Studio, LRO/LROC, Ernie Wright;
  mapa de color de CGI Moon Kit preparado para visualización, con relleno
  polar documentado por la fuente. https://svs.gsfc.nasa.gov/4720/
- Júpiter: NASA/JPL/Space Science Institute, Cassini PIA07782; imágenes de
  diciembre de 2000.
  https://science.nasa.gov/photojournal/cassinis-best-maps-of-jupiter-cylindrical-map/

Los datos de referencia se distinguen del código propio del proyecto.
