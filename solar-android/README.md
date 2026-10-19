# COSMIC X Solar 4K

Explorador del sistema solar para Android, construido como una nueva aplicación
de **COSMIC X Engine**. Integra el núcleo original de Mundo X y amplía su
observador gráfico con planetas esféricos, anillos, órbitas y capas magnéticas.
Esta versión comparte la infraestructura de resolución del motor **4K/8K** y
ofrece un modo **Solar 4K UHD**, un mapa orbital interactivo y una cámara de
descenso para los ocho planetas y las catorce lunas del catálogo.

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
| Mapa | Elegir planetas y lunas; ampliar y desplazar el atlas orbital |
| Mapa → Sistema 3D | Recuperar la vista general de las órbitas |
| Descender | Entrar en el ambiente del destino, siguiendo su movimiento |
| Altitud / Subir / Bajar | Cambiar la altura de la cámara de descenso |
| Regresar a órbita | Recuperar la cámara exterior y sus capas |
| Calidad → 4K UHD | Renderizar a 3840 × 2160 o 2160 × 3840 según orientación |
| Guardar PNG | Exportar el framebuffer a una imagen sin los controles |
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
a 640, 960 o 1440 píxeles. **4K UHD** solicita una imagen de 8.294.400 píxeles,
ajustada a la pantalla sin deformarla. Si la GPU rechaza la asignación, el motor
conserva una resolución válida y avisa. Estos modos definen la resolución de la
imagen; el rendimiento depende del dispositivo. El inicio utiliza Equilibrada
para que el usuario pueda activar UHD desde el mismo explorador.

Durante el descenso, arrastrar mira alrededor y el control circular desplaza
la cámara sobre el destino. Pellizcar cambia la altura. La altitud se mide
respecto al radio medio del catálogo, no respecto a un relieve medido. Los
mundos rocosos permiten acercarse hasta 0,5 km sobre esa esfera; los gigantes
permiten recorrer nubes hasta −200 km respecto a su nivel de referencia. No
existe un suelo sólido dibujado para los gigantes. Mercurio y las lunas con
exosfera conservan un cielo sin atmósfera densa. El Sol se explora desde fuera.

El mapa conserva la fecha de la simulación. Comprime las distancias radiales
de forma logarítmica y amplía los marcadores para facilitar su selección; el
panel de lunas utiliza una escala local separada. Estas transformaciones se
indican en pantalla y no alteran los datos orbitales.

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
[procedencia de las imágenes](app/src/main/assets/science-assets.json), además
de [ambientes, descenso y mapa](docs/atmosphere-science.md). La dispersión,
bruma, nubes y relieve aparente son aproximaciones de visualización: no se
calculan meteorología, transferencia radiativa completa ni topografía medida.

## Qué hace el núcleo de COSMIC X

Se compilan directamente, sin una copia divergente:

- `graphics-android/app/src/main/java/com/mundox/graphics/NativeCore.java`;
- `graphics-android/app/src/main/cpp/graphics_jni.cpp`;
- `native/src/graphics_core_bridge.cpp` y las cabeceras originales de Mundo X.

La cadena activa es:

`SolarActivity → QuantumSolarBridge → NativeCore → JNI → EngineSession / CompactCore`.

El núcleo compartido admite registros de hasta **1.000 qubits** en Android.
`QuantumSolarBridge` utiliza una preparación solar de **ocho qubits** mediante puertas
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
gradle -p solar-android --no-daemon -Psolar4kStandalone=true \
  :app:assembleDebug :app:assembleDebugAndroidTest
```

La variante entregada tiene paquete `com.cosmicx.solar.uhd`, versión **1.1.0**
y nombre **COSMIC X Solar 4K**. Puede instalarse junto al Solar anterior
(`com.cosmicx.solar`) y al motor gráfico independiente. Omitir la propiedad
`solar4kStandalone` construye la identidad solar original. Las compilaciones
de desarrollo usan firma debug.

El workflow [solar-4k.yml](../.github/workflows/solar-4k.yml) construye
el APK, verifica firma y ABI, registra SHA-256 y procedencia del núcleo, y lo
prueba en Android 15 con el driver de software SwiftShader y 2 GB configurados
para el emulador. Esas comprobaciones no miden el rendimiento de un Samsung A16.
La procedencia verifica las fuentes de los perfiles integrados del núcleo y
de resolución, y registra los cambios frente a la versión 4K original. Los
workflows se ejecutan en `main` y en los pull requests cuando cambian Solar o
sus dependencias compartidas.

La evidencia Android registra pruebas, capturas de los framebuffers reales,
logcat, memoria de proceso y el commit exacto del APK. Las pruebas comprueban
el arranque nativo, las órbitas, los observables, el cambio visual por campos y
por estado del núcleo, navegación, tiempo, proporciones, ciclo de vida y
límites de resolución. El perfil ampliado mantiene los trece casos solares
originales y añade seis de UHD, ajuste a pantalla, entrada en los 22 destinos,
atmósferas, persistencia de la cámara y selección táctil del mapa. La exportación
PNG se comprueba con el mismo codificador que utiliza el botón de la aplicación.

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
