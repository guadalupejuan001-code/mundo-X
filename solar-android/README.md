# COSMIC X Solar

## Versión 1.5 — 4K Fluido y fotografía 4K real

**Problema observado:** un framebuffer 3840 × 2160 requiere 8.294.400
fragmentos (más luz, nubes y atmósferas) por fotograma. A 2 FPS, cada
fotograma tarda aproximadamente 500 ms. MIL Core no acelera automáticamente
las operaciones GLES de la GPU del teléfono.

**Nuevo selector Calidad → 4K Fluido:** para navegar se dibuja el
mismo shader propio de Solar en un framebuffer 16:9, empezando en
**540 × 960 (518.400 píxeles)**. Eso reduce 16× el número de píxeles
de sombreado frente a 4K nativo. Un controlador con histéresis ajusta la
vista entre 360 × 640, 540 × 960 y 810 × 1440, según los FPS medidos.
Esta reducción del número de fragmentos **no garantiza multiplicar FPS por 16**:
CPU, memoria, carga de texturas y controlador GPU también influyen.

Al pulsar **Guardar PNG** en 4K Fluido, Solar crea temporalmente un
framebuffer **2160 × 3840** (o **3840 × 2160** en horizontal), renderiza
la escena **a resolución nativa** y lee todos los píxeles. Una vez
capturado, vuelve al visor adaptable y libera el framebuffer UHD.
El PNG NO se crea reescalando una imagen 960p.
Si la GPU no soporta esa reserva, se notifica un error y se conserva
la vista anterior; nunca se exporta silenciosamente una falsa captura 4K.

**4K UHD nativo y 8K UHD nativo siguen disponibles sin cambios:** estos
modos renderizan TODOS los píxeles en cada fotograma y, por tanto,
pueden funcionar lentamente en móviles modestos. El indicador
de pantalla muestra el tamaño real del visor junto a la etiqueta
**PNG 4K** cuando está activo 4K Fluido.

La actualización no modifica los datos planetarios, la física,
los estados de MIL Core, las capas atmosféricas científicas ni
los resultados de Cine X. No es reconstrucción de imagen mediante IA
ni computación cuántica gráfica.

### Verificación

- `SolarSmart4kChecks.java`: prueba de tamaños y orientación,
  518.400 vs. 8.294.400 píxeles, límites GPU, histéresis y cambios
  entre 640/960/1440.
- `SolarInstrumentation`: dibuja realmente el visor con GLES,
  verifica que el framebuffer no se etiqueta 4K nativo y comprueba la
  restauración del modo clásico.
- La batería de pruebas UHD 4K solicita una captura real de 8,3 MP,
  guarda su PNG y verifica que el framebuffer adaptable se recupera.

Los FPS y la temperatura **del Samsung del usuario** no se infieren
de un emulador Android y deben medirse en ese teléfono.


## Actualización visual 1.4 · Cine X

El modo **Cine X** es una mejora gráfica opcional sobre el shader original
de Solar y Ether/COSMIC X. Se activa en **Observatorio → Calidad de imagen →
Cine X**. Añade tres efectos de coste acotado:

- Brillos ópticos de estrellas especialmente intensas, filtrados según la
  huella angular de píxel; es un efecto de cámara ilustrativo.
- Dispersión atmosférica de borde aproximada, modulada por la orientación al Sol
  y la visibilidad durante eclipses; no es un cálculo radiativo medido.
- Anillos estrechos antialias en gigantes gaseosos, atenuados al quedar por
  debajo de la resolución de píxel.

La opción comienza desactivada para preservar las imágenes de referencia,
las pruebas de determinismo y el rendimiento de dispositivos modestos.
No cambia las órbitas, las masas, la gravedad, las efemérides, las capas
cuánticas ni los materiales científicos. El motor sigue siendo el shader
GLES original; **100 qubits GHZ del laboratorio Fusion 3D no son un
acelerador GPU de Solar**.

Los controles de calidad Ahorro/Equilibrada/Alta/4K/8K continúan disponibles.
La resolución UHD no implica 4K/8K a FPS altos: el rendimiento se medirá en
cada dispositivo. La prueba Android de Cine X exige que el shader compile,
produzca píxeles diferentes al activarlo y restaure exactamente el framebuffer
de referencia al desactivarlo.


Explorador del sistema solar para Android, construido como una nueva aplicación
de **COSMIC X Engine**. Integra el núcleo original de Mundo X y amplía su
observador gráfico con planetas esféricos, anillos, órbitas y capas magnéticas.
Esta versión comparte la infraestructura de resolución del motor **4K/8K** y
ofrece un modo **Solar 4K UHD**, un mapa orbital interactivo y una cámara de
descenso para los ocho planetas y las catorce lunas del catálogo.

La integración **1.3.0** publica observaciones atómicas de Ether, comparte las
efemérides y evita evaluar material procedural cuyo resultado no se usa. El
shader evalúa los 23 cuerpos con gradientes de filtrado definidos; el descarte
GPU por teselas se retiró al detectar diferencias frente a la referencia
completa. La exportación usa el `StreamPng` original de Graphics sin crear un
segundo Bitmap completo. La [validación actual de Solar](../docs/solar-shader-determinism-2026-10-21.md)
recoge fuentes, comprobaciones y límites; el [informe anterior](../docs/solar-integration-performance.md)
conserva las medidas y la procedencia de la primera integración.

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

`SolarActivity → QuantumSolarBridge → NativeCore → JNI → InfiniteSession → InfiniteCore`.

La sesión contiene **un único estado**: las reglas globales de Ether y una
preparación finita almacenada mediante los componentes originales de
`CompactCore`. Su dominio es `q0, q1, q2, …`; los **ocho sitios de preparación**
son las direcciones `0` a `7`, no el tamaño total del dominio. Una capa global
se conserva como regla y no crea un registro por cada dirección.

Al elegir un modo, Solar crea explícitamente una nueva sesión con fondo cero.
Base conserva ese fondo; Superposición aplica H en los ocho sitios;
Entrelazado prepara cuatro pares Bell; Fase añade S al primer miembro de cada
par. **Después de esa preparación se aplican H global y CZ entre vecinos al
mismo estado.** Por tanto, los valores visibles corresponden al estado después
de las reglas, y no a ocho qubits Bell aislados. Por ejemplo, en Entrelazado se
observa `⟨Z₀Z₁⟩ = 1`, pero `⟨X₀X₁⟩ = ⟨Y₀Y₁⟩ = 0`; en Fase,
`⟨Y₀⟩ = −1`.

Las ocho señales locales y las cinco señales del vecindario Ether se consultan
en **un solo lote con la misma revisión y época de sesión**. Entre estas últimas
están `⟨Z₀X₁Z₂⟩` y `⟨X₀Z₁⟩`: ambas valen 1 en Base y cambian a 0 en las demás
preparaciones. Esas señales proceden del mismo estado que las puertas locales;
no existe un segundo cluster independiente para alimentar al observador.
Consultar, dibujar o cambiar la cámara no añade reglas ni mide el estado.
Coeficientes y metadatos se publican juntos en una `Observation` inmutable;
el renderer consume ocho coeficientes locales y cuatro globales de esa misma
observación, sin arrays nuevos por cuadro. La quinta señal global sigue
disponible para consulta y verificación.

Base conserva cero sitios materializados y dos reglas globales; las otras
preparaciones materializan ocho sitios. El dominio implícito no hace infinita
la memoria física: puertas, observaciones y componentes usan recursos finitos
y están sujetos a sus presupuestos. La cifra de bytes mostrada cuenta el
**payload del tableau finito**; excluye metadatos de reglas y direcciones,
memoria temporal, framebuffers y la RAM total de la aplicación.

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
gradle -p solar-android --no-daemon \
  :app:assembleDebug :app:assembleDebugAndroidTest
```

La variante normal tiene paquete `com.cosmicx.solar`, versión **1.3.0**, código
de versión 4. `-Psolar4kStandalone=true` y `-Psolar8kStandalone=true` conservan
las identidades `.uhd` y `.ultra`. `-PsolarReview=true` añade `.review` al
paquete elegido y «Pruebas» al nombre; permite instalar esta revisión junto a
la original. Las compilaciones de desarrollo usan firma debug; actualizar un
paquete existente requiere la misma clave con la que se firmó su instalación.

El [workflow de comparación](../.github/workflows/solar-performance.yml)
construye una referencia a partir de `af0b179`, con filtrado definido y
evaluación independiente de todo el catálogo, y el candidato con la misma
instrumentación. El pareado de `b9ec7452` aprobó **8/8 escenas y 584.640
píxeles RGBA sin diferencias**, con repeticiones exactas; también aprobó la
ejecución posterior de `6115ccad`, cuyas fuentes de producto permanecen
idénticas. La [revisión de exactitud](../docs/solar-shader-determinism-2026-10-21.md)
conserva los hashes, resultados y límites de cada comparación. La referencia
definida no equivale a conservar el comportamiento indefinido del shader antiguo.

El workflow [solar-4k.yml](../.github/workflows/solar-4k.yml) construye
el APK, verifica firma y ABI, registra SHA-256 y procedencia del núcleo, y lo
prueba en Android 15 con el driver de software SwiftShader y 2.048 MiB
solicitados al emulador. El runner registra ahora la memoria efectiva: el
argumento solicitado no es un límite de la VM, y se observó un aumento a
2.560 MiB. El perfil [8K](../.github/workflows/solar-8k.yml) usa la imagen AOSP
API35 y aprobó [15/15 casos en `6115ccad`](https://github.com/guadalupejuan001-code/mundo-X/actions/runs/39532390017).
Estas comprobaciones acreditan corrección en software GLES; no miden el
rendimiento de un Samsung A16.
La procedencia verifica las fuentes de los perfiles integrados del núcleo y
de resolución, y registra los cambios frente a la versión 4K original. Los
workflows se ejecutan en `main` y en los pull requests cuando cambian Solar o
sus dependencias compartidas.

La evidencia Android registra pruebas, capturas de los framebuffers reales,
logcat, memoria de proceso y el commit exacto del APK. Las pruebas comprueban
el arranque nativo, las órbitas, las cuatro preparaciones bajo el marco Ether,
la observación común y sin mutaciones, el cambio visual por campos y
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
