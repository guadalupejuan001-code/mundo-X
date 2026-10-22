# COSMIC X Engine · Mundo X

**Núcleo cuántico propio, motor gráfico, laboratorios, explorador solar y PC x86/x64 en un mismo repositorio.**

COSMIC X Engine es el título del conjunto. Dentro se conserva la computación cuántica de Mundo X, sus representaciones originales y el motor gráfico que observa el estado compartido. La idea, creación y dirección del proyecto corresponden a **X, titular del repositorio**; el historial documenta el desarrollo con asistencia de IA. La simulación se ejecuta por software y el motor gráfico utiliza OpenGL ES.

**Estado:** desarrollo activo · **Rama principal:** `main` · **Revisión:** 21 de octubre de 2026.

Esta revisión reúne los proyectos que se desarrollaron en ramas separadas: **Ether**, el perfil finito de **1.000 qubits**, Graphics **4K/8K con espejos**, **Solar con atmósferas y mapa**, y **COSMIC X PC**. Conserva los laboratorios, Ecos, la VM de 8 bits, el micromundo, los lenguajes y la línea Transformer X. La [consolidación del repositorio](docs/repository-unification-2026-10-21.md) identifica los aportes recuperados, los borradores superados y la evidencia de cada plataforma.

**InfiniteCore integrado.** El [dominio por reglas y componentes finitos](docs/infinite_core.md)
dispone de una [sesión compartida con el emulador y el motor gráfico](docs/infinite_integration.md).
El invitado x86/x64 envía programas al núcleo por puertos IN/OUT; el viewport del PC
observa esa misma sesión. Los proyectos Infinite guardan su estado actual y RNG
en `.cosmic` v2, y el perfil finito de 1.000 qubits conserva `.cosmic` v1.

## Ether Core · arquitectura común

**Ether será la fuente común de estado y cálculo de Mundo X.** La dirección del
proyecto es que memoria lógica, ejecución, gráficos, física y demás sistemas
trabajen sobre la misma estructura dentro de cada sesión. Cada componente debe
definir qué consulta y qué transforma en Ether, conservando las identidades,
relaciones y cambios que permiten recuperar el estado.

El dominio se describe mediante reglas; las operaciones y observaciones deben
obtener respuestas con trabajo finito. La representación del estado y la forma
de calcular sobre él se diseñan juntas. Las vistas y expansiones temporales
tienen un coste que también debe medirse: ahorrar en el núcleo no acredita por
sí solo un ahorro en la aplicación completa.

El [contrato de arquitectura Ether](docs/ether-architecture.md) fija las
responsabilidades por componente, la conservación del estado, la coherencia de
las observaciones y los criterios de integración y rendimiento.

La primera capa de [Ether Memory](docs/ether-memory-v0-1.md) y la ventana
MemoryFabric x86 permanecen como compatibilidad y evidencia histórica. **La
dirección principal de simulación cuántica pasa a
[Inverse Ether Memory](docs/inverse-ether-memory.md):** regla global, diferencias
finitas y enlaces cuánticos explícitos, sin un arreglo denso ni un mapeo global
de ocho qubits por byte.

### Ruta cuántica pura

`quantum-runtime/` configura una compilación aislada que incluye
Ether/InfiniteCore/CompactCore, XI e Inverse Ether Memory y **no configura PC,
Graphics, Solar, física, JNI ni GLES**. El workflow
`quantum-only.yml` inspecciona las unidades compiladas para mantener esa
separación.

```sh
cmake -S quantum-runtime -B quantum-build -DCMAKE_BUILD_TYPE=Release
cmake --build quantum-build --parallel 2
ctest --test-dir quantum-build --output-on-failure
./quantum-build/mundo_x_quantum examples/inverse_memory.xi
```

Los comandos XI nuevos son `mem_reset`, `mem_store`, `mem_bind`,
`mem_unbind`, `mem_read`, `mem_materialize` y `mem_stats`. Una lectura
de un enlace cuántico no determinista devuelve un handle y **no mide** el estado.

**Fusión implementada:** Ether y los componentes finitos ahora describen un
único estado cuántico por sesión. Las reglas globales, puertas locales,
mediciones, consultas y snapshots usan esa misma autoridad. Graphics y COSMIC
inician mundos nuevos con Ether; Solar obtiene sus dos grupos de señales del
mismo estado; el huésped PC y su visor conservan la misma sesión. El
[contrato técnico y sus pruebas](docs/ether-fusion.md) detalla la representación
`Ψ = U_Ether φ`, las migraciones y los límites comprobados.

La [compactación exacta de reglas](docs/ether-rule-compaction.md) guarda la
secuencia global mediante una regla y su longitud. Incluye evaluación algebraica
de consultas de Pauli y comparaciones reproducibles con la versión anterior.

La [evaluación por intervalos y su recorrido gráfico](docs/ether-evaluation-render.md)
extiende ese ahorro a productos mixtos y evita reconstrucciones que se cancelan.
Incluye comparaciones de píxeles y del recorrido Ether–escena–imagen con un
presupuesto de 2 GiB por proceso en el anfitrión.

La [optimización conjunta de Ether, física y selección](docs/ether-system-optimization.md)
y el [índice compacto de excepciones del renderizador](docs/ether-render-optimization.md)
conservan los resultados mediante comparaciones antes/después. El segundo evita
buscar repetidamente todas las ediciones por cada candidato de un rayo y se
comparte entre escritorio, Graphics Android, COSMIC y el visor de PC. Sus
mediciones son del anfitrión; no certifican FPS de una APK en un teléfono.

Los modelos orbitales, la CPU/RAM x86, la escena procedural y GLES mantienen sus
algoritmos y recursos actuales. Su representación integral mediante Ether sigue
siendo parte del objetivo de arquitectura. Los proyectos finitos y otros núcleos
conservan sus contratos propios y no reciben un Ether auxiliar oculto.

### Espejos sobre un solo mundo

El [observador con espejos](docs/ether-mirrors.md) calcula reflejos sobre las
mismas reglas, coeficientes e IDs. Una imagen reflejada conserva el objeto de
origen: seleccionarlo, editarlo u ocultarlo actualiza sus reflejos. Los tramos
periódicos que cumplen las condiciones geométricas se resuelven por composición;
los demás siguen un recorrido explícito con presupuesto. El coste no equivale a
copiar una escena por cada reflejo.

**Graphics → Espejos** y **COSMIC → Motor → Escena → Espejos del mismo mundo**
permiten usar suelo reflectante o dos espejos enfrentados. La sesión privada
conserva esos ajustes. El formato portable `.cosmic` todavía no transporta la
configuración óptica; abrir un proyecto conserva el contrato del formato actual.

Android reutiliza el framebuffer completado cuando todas las entradas visuales
son exactamente iguales. Cámara, núcleo, ediciones, resolución, selección y
parámetros ópticos invalidan esa reutilización. La medición sincronizada fuerza
un dibujo real. La interfaz identifica una imagen conservada sin presentarla
como un dibujo instantáneo ni como una cifra de FPS.

### Ether Render X · núcleo de renderizado nuevo

Esta versión integra **Ether Render X v0.2** como motor nativo basado en reglas
para los comandos `render*` de XASM. Planifica candidatos conservadores por
región, traza rayos, selecciona intersecciones y compara píxel/ID/profundidad
contra el recorrido exhaustivo. La función `render_train` compara tamaños de
teselas con medianas medidas y valida cero diferencias de imagen.

```sh
./build/mundo_x run examples/ether_render.x
ctest --test-dir build -R '^(rule_renderer|xasm_renderer)

| Componente | Función y alcance | Código y documentación |
| --- | --- | --- |
| Ether y perfil finito compatible | Nuevos mundos Android con reglas globales y modificaciones locales en el mismo estado; perfil finito explícito `q0`–`q999`, puertas Clifford y ampliación de proyectos anteriores. | [Núcleo nativo](native/), [perfil de 1.000](docs/core_1000_qubits.md), [sesión compartida](docs/cosmic_x_engine.md) |
| COSMIC X Engine Android | Paneles Motor, Núcleo y Proyectos, más 14 experimentos originales dentro de la misma instalación. | [App COSMIC](cosmic-android/README.md) |
| Graphics Android 4K/8K | Escena procedural, selección y edición por ID, renderizado UHD, modo 8K sujeto a capacidades y exportación PNG por mosaicos. | [Graphics](graphics-android/README.md), [4K](graphics-android/docs/4k.md), [8K](graphics-android/docs/8k.md) |
| Motor gráfico de escritorio | Compila el mismo puente nativo, observa la misma representación y abre/guarda archivos `.cosmic` compatibles con Android. | [Escritorio](graphics-desktop/README.md), [formato](graphics-desktop/PROJECT_FORMAT.md) |
| COSMIC X Solar | Sol, ocho planetas y catorce lunas; navegación, mapa orbital, tiempo variable, ambientes de descenso y captura UHD. Su preparación actúa sobre ocho direcciones del mismo dominio Ether que alimenta sus señales visuales. | [Solar](solar-android/README.md), [modelos orbitales](solar-android/docs/solar-science.md), [ambientes](solar-android/docs/atmosphere-science.md) |
| COSMIC X PC | Intérprete propio de un subconjunto x86/x64, RAM por demanda, SHLD/SHRD, BIOS de texto/teclado/disco y framebuffer huésped VBE hasta 8K presentado con GLES 3; dispositivo InfiniteCore y viewport sobre la sesión cuántica compartida. Arranques propios hasta modo largo de 64 bits. | [PC propia](pc-android/README.md) |
| XLab, MicroLab, VM y Ecos | Mesa de circuitos, protocolos, memoria, objetos, micromundo, máquina de 8 bits y experimentos de evolución/perturbación/reversión. | [XLab](docs/xlab_0_2.md), [objetos](docs/xlab_0_3_objetos.md), [VM y Ecos](android-experiments/README.md) |
| Lenguajes y núcleos experimentales | XASM `.x`, circuitos `.mx`, fases generales `.qx`, reducción de T y factorización con presupuestos explícitos. | [XASM](docs/xasm_v0_2.md), [.mx](docs/mx_language_v0_1.md), [fases](docs/state-transfer.md) |
| Física nativa 2D y 3D | Modelos clásicos finitos con recuperación de fallos; el CLI `physics3d` ejecuta la base 3D rescatada. Sus cuerpos aún no son estado Ether ni alimentan las órbitas de Solar. | [Auditoría del núcleo](docs/repository-core-review-2026-10-21.md) |
| Bridge X y Transformer X | Codificación de bytes en modos y modelo PyTorch entrenable. Se conserva como línea independiente; todavía no alimenta CompactCore. | [Bridge X](src/bridge_x.py), [Transformer X](src/transformer_x.py), [entrenamiento](experiments/train_file_v0_1.py) |

La APK principal `com.mundox.cosmic` reúne el motor, el núcleo y los laboratorios. **Solar y PC son aplicaciones separadas dentro del mismo código fuente.** La VM didáctica de 8 bits está incluida en COSMIC; el emulador x86/x64 de PC conserva su propio destino de compilación.

## Ether y compatibilidad con 1.000 qubits

Una sesión gráfica nueva en Android utiliza **Ether**, con fondo entrelazado definido por dos reglas y sin un tamaño total de registro. El selector de nuevo proyecto permite elegir un registro finito de **1 a 1.000 qubits**. En Android, **Ampliar a 1000** reproduce el historial de una sesión anterior en el registro mayor y agrega qubits en `|0>`. Conserva los observables originales, sus signos, la semilla, los objetos, las ediciones, la cámara y el tiempo. Los proyectos guardados conservan su dimensión al abrirse; los ejemplos Bell y GHZ pequeños mantienen su tamaño didáctico.

El contrato comprueba entrelazamiento GHZ sobre los 1.000 qubits, correlaciones, medición, signos Pauli y operaciones sobre `q999`. La [documentación del perfil](docs/core_1000_qubits.md) distingue esas pruebas del oráculo denso pequeño y explica la compatibilidad de archivos.

| Representación | Alcance que debe usarse al programar |
| --- | --- |
| Sesión gráfica / CompactCore | Perfil portable de 1 a 1.000 qubits; H, S, X, Y, Z, CX y CZ. El perfil finito de 1.000 conserva 3.000 observables iniciales. |
| CompactCore nativo / XASM | Circuitos estabilizadores, entrelazamiento Clifford y medición Z reproducible; incluye `examples/ghz1000.x`. |
| Reduced Exact T | Interfaz XASM de 1 a 1.000 qubits, 128 puertas T y 65.536 componentes como límites. Las máscaras Pauli son multi-palabra; el costo depende del crecimiento real de componentes. |
| FactorizedCore | Mantiene factores independientes y bloques con presupuesto; el límite de bloque denso sigue siendo 20 qubits. |
| Solar | Prepara ocho direcciones y observa trece señales de una única sesión Ether; esas direcciones no limitan el dominio total. |
| Laboratorios | Cada experimento conserva la representación que requiere su modelo; no se sustituyen silenciosamente sus backends. |

El tableau de 1.000 almacena **257.000 bytes** en sus buffers. Esa cifra no incluye observador, historial, Java, Android ni GPU. El perfil compacto corresponde a estados estabilizadores; los circuitos generales con muchas puertas T no adquieren por este cambio el mismo costo de memoria. [Fórmula, benchmark de 20/60/1.000 y límites](docs/core_1000_qubits.md#memoria-y-observación).

### Compilar el conjunto nativo

Desde Linux, con CMake y un compilador C++17:

```sh
git clone https://github.com/guadalupejuan001-code/mundo-X.git
cd mundo-X
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure

./build/mundo_x_infinite examples/ether_fusion.xi
./build/mundo_x run examples/ghz1000.x
./build/mundo_x run examples/t60.x
./build/mundo_x run-mx examples/mx_layers.mx
./build/mundo_x_phase examples/adder50.qx adaptive
./build/mundo_x physics3d 256 120
./build/mundo_x_vm --self-test
./build/pc-android/cosmic_pc_cli --demo64 --ram-mib 2048 --resident-mib 1024
```

La compilación raíz incluye los contratos del núcleo, la VM y las diez suites de PC, incluidas `pc_video` y `pc_quantum`. `BUILD_TESTING=OFF` permite construir los ejecutables sin los tests. Las instrucciones de PC ejecutan los arranques propios de demostración; no arrancan Windows.

### Crear o ampliar un proyecto de escritorio

```sh
python -m pip install -r graphics-desktop/requirements.txt
python graphics-desktop/demo.py --no-render --export-project mundo-1000.cosmic
python graphics-desktop/demo.py --project mundo-anterior.cosmic \
  --expand-qubits 1000 --gate X:999 --no-render \
  --export-project mundo-ampliado.cosmic
```

Sustituye `mundo-anterior.cosmic` por tu archivo. La exportación sin render no necesita un contexto gráfico; para producir imágenes, el adaptador de escritorio necesita EGL/OpenGL ES y actualmente construye una biblioteca Linux `.so`. [Uso y ejemplos](graphics-desktop/README.md).

## Aplicaciones Android y resoluciones

Requisitos de construcción declarados: **Java 17, Gradle 8.9, AGP 8.7.3, SDK 35, NDK 27.2.12479018 y CMake 3.22.1**. Los proyectos no incluyen Gradle Wrapper. Los comandos siguientes se ejecutan desde la raíz con Gradle instalado; las aplicaciones nuevas admiten Android 8.0 o posterior y las ABI ARM64 y x86_64.

| Destino | Identidad y versión | Compilar desde la raíz |
| --- | --- | --- |
| COSMIC unificado | `com.mundox.cosmic` · 0.3.1 | `gradle -p cosmic-android :app:assembleDebug` |
| Graphics | `com.mundox.graphics` · 0.4.0 | `gradle -p graphics-android :app:assembleDebug` |
| Graphics 4K | `com.mundox.graphics.uhd` · 0.4.0 | `gradle -p graphics-android -Pcosmic4kStandalone=true :app:assembleDebug` |
| Graphics 8K | `com.mundox.graphics.ultra` · 0.4.0 | `gradle -p graphics-android -Pcosmic8kStandalone=true :app:assembleDebug` |
| Solar | `com.cosmicx.solar` · 1.3.0 | `gradle -p solar-android :app:assembleDebug` |
| Solar 4K | `com.cosmicx.solar.uhd` · 1.3.0 | `gradle -p solar-android -Psolar4kStandalone=true :app:assembleDebug` |
| Solar 8K | `com.cosmicx.solar.ultra` · 1.3.0 | `gradle -p solar-android -Psolar8kStandalone=true :app:assembleDebug` |
| PC propia | `com.cosmicx.pc` · 0.4.0 | `gradle -p pc-android :app:assembleDebug` |
| XLab original | Proyecto Android de la raíz | `gradle :app:assembleDebug` |

Los indicadores de Graphics 4K y 8K son mutuamente excluyentes. Se puede añadir `:app:assembleDebugAndroidTest` para construir las pruebas instrumentales. GitHub Actions conserva APK, firma, SHA-256, revisión del código y evidencia; los artefactos corresponden a la ejecución seleccionada.

### Qué hacen 4K y 8K

Graphics conserva hasta **1.048.576 cajas** como límite del modelo, con IDs estables y edición individual. El modo UHD usa **3840 × 2160** o **2160 × 3840**. La ruta Ultra incorpora **7680 × 4320**, límites comprobados del dispositivo y exportación PNG por mosaicos, que evita mantener toda la imagen RGBA 8K en un único buffer de CPU. La preferencia de modo UHD pertenece al dispositivo y se preserva al ampliar una sesión; ese modo se omite del archivo `.cosmic` portable, mientras que el límite adaptativo `resolutionLimit` sí forma parte del proyecto. [Pruebas de 4K](graphics-android/docs/4k.md), [mosaicos y presupuesto 8K](graphics-android/docs/8k.md).

Los paneles Motor y Núcleo de COSMIC comparten `NativeCore`. Observar, seleccionar o mover la cámara no añade puertas ni realiza una medición proyectiva. Las pruebas comparan imágenes y estado para comprobar esta relación. La cantidad de objetos y la resolución admitida no expresan una garantía de FPS en un teléfono.

### Solar y PC

Solar reutiliza el núcleo y la geometría de resolución compartidos, pero sus órbitas, escalas, tiempo y ambientes tienen modelos propios. Incluye **23 cuerpos**, descenso a 22 planetas/lunas, mapa interactivo, texturas incorporadas y representaciones magnéticas. Sus aproximaciones orbitales y visuales están delimitadas en [ciencia solar](solar-android/docs/solar-science.md) y [atmósferas y mapa](solar-android/docs/atmosphere-science.md); no constituyen una simulación climática completa.

COSMIC X PC **0.4.0** ejecuta un intérprete C++ propio. Su prueba x64 realiza la transición real → protegido → largo y verifica datos de 64 bits en registros y RAM. La CPU incorpora SHRD (`0F AC/AD`) y SHLD (`0F A4/A5`) en 16/32/64 bits, incluida la instrucción que antes detenía un cargador ISO con `two-byte opcode is not implemented`. La regresión reproduce el opcode; la compatibilidad de la ISO completa necesita su propio arranque comprobado.

PC incorpora ahora un dispositivo de vídeo lineal con una interfaz VBE limitada. **Prueba gráfica** permite generar píxeles desde instrucciones del huésped a **1280 × 720, 3840 × 2160 y 7680 × 4320**. Un presentador GLES 3 propio consume ese framebuffer mediante JNI y utiliza la misma clase [RenderResolution](graphics-android/app/src/main/java/com/mundox/graphics/RenderResolution.java) que Graphics. La interfaz distingue **fuente huésped**, **salida de renderizado** y **superficie visible de Android**: seleccionar salida 8K no convierte una fuente de texto o de menor resolución en un huésped 8K ni cambia la resolución física del teléfono. La asignación 8K comprueba las capacidades GL y conserva la última salida válida si el dispositivo la rechaza.

El panel Núcleo permite enviar programas al mismo `InfiniteSession` que utiliza el invitado mediante los puertos `0x5850` y `0x5851`. El viewport cuántico utiliza `QuantumRenderer` para observar esa sesión; **Guardar núcleo** conserva su estado actual y RNG en un archivo `.mxi`. Las pruebas comprueban que una instrucción del invitado cambia observables y píxeles del viewport. [Conexión, comandos y persistencia](docs/infinite_integration.md).

El perfil Android conserva **2 GiB de RAM virtual** con un límite de **1 GiB de páginas respaldadas**. Vídeo tiene presupuestos separados: **512 MiB para texturas GL simultáneas** y **256 MiB para instantáneas nativas**; PSS informa el proceso Android por separado. Esos límites no garantizan un consumo total fijo de 2 GiB. La CPU x86/x64 no recibe aceleración del núcleo de qubits por compartir este repositorio. **Todavía no instala ni ejecuta Windows completo o juegos de PC.** [Uso, dispositivo de vídeo, pruebas y límites](pc-android/README.md).

### Windows x64

Los ejecutables nativos también se construyen con MSVC:

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
.\build\Release\mundo_x.exe run examples\ghz1000.x
.\build\Release\mundo_x_vm.exe --self-test
.\build\pc-android\Release\cosmic_pc_cli.exe --demo64
```

El workflow [Windows x64](.github/workflows/windows-x64.yml) conserva el paquete de ejecutables, ejemplos e instrucciones. Este destino ejecuta Mundo X **sobre Windows anfitrión**; la compatibilidad del emulador PC con un Windows invitado es un asunto distinto.

## Verificación y procedencia de la integración

Los workflows comprueban los cambios de código en solicitudes de integración a `main` y conservan los resultados del commit ejecutado. Las suites cubren ámbitos diferentes:

| Ruta | Comprobaciones que conserva |
| --- | --- |
| [Contratos nativos y escritorio](.github/workflows/cosmic-contracts.yml) | Núcleos, GHZ de 1.000, qubit 999, presupuestos, restauración, proyectos `.cosmic`, VM, CPU/PC y relación causal entre núcleo e imagen. |
| [COSMIC Android](.github/workflows/cosmic-android.yml) | 26 casos COSMIC, 22 Graphics y 8 MicroLab sobre las fuentes compartidas. |
| [Graphics](.github/workflows/graphics-android.yml), [4K](.github/workflows/graphics-4k.yml), [8K](.github/workflows/graphics-8k.yml) | Inventarios de 23, 26 y 30 casos: registro de 1.000, Infinite, estado portable, reutilización exacta de fotogramas, espejos en el perfil normal, selección, orientación y PNG. |
| [Solar](.github/workflows/solar-android.yml), [Solar 4K](.github/workflows/solar-4k.yml) y [Solar 8K](.github/workflows/solar-8k.yml) | 511 comprobaciones analíticas; 13 casos Android del perfil normal, 19 del perfil 4K/ambientes y 15 del perfil 8K. |
| [PC propia](.github/workflows/own-pc-android.yml) | Diez suites nativas y 30 casos Android previstos: arranque, memoria, regresión ISO ENTER/LEAVE, vídeo huésped/GLES hasta 8K, propiedad de instantáneas JNI y sesión Infinite compartida entre CPU y viewport. |
| [Windows](.github/workflows/windows-x64.yml), [runtime visual](.github/workflows/visual-runtime.yml), [XLab](.github/workflows/android-apk.yml) | Compilación del conjunto en sus plataformas y regresiones de los experimentos conservados. |

Los recuentos describen el inventario esperado; el resultado de cada ejecución está en [Actions](https://github.com/guadalupejuan001-code/mundo-X/actions). Una prueba que pasa demuestra las condiciones examinadas. Las pruebas GLES en Android virtual no sustituyen una medición de rendimiento en el teléfono físico.

El auditor común [cosmic_source_provenance.py](tools/cosmic_source_provenance.py) compara los archivos compartidos byte a byte con las revisiones de referencia. Los informes 4K/8K/Solar declaran la actualización al núcleo de 1.000 y las diferencias reales respecto al núcleo 4K anterior; también comprueban firma, identidad y bibliotecas de sus APK.

La procedencia de PC 0.4.0 registra la identidad real del APK, el commit y los SHA-256 de su puente de vídeo, del dispositivo VBE, de la CPU, del puente Infinite y de las clases gráficas compartidas. Comprueba las bibliotecas `libcosmic_pc.so`, `libmx_graphics.so` y `libc++_shared.so` para ARM64 y x86_64. Sus pruebas guardan por separado el estado del huésped, el estado del presentador y capturas; el inventario documentado no sustituye al resultado de Actions de esa revisión.

| Trabajo incorporado | Revisión de origen |
| --- | --- |
| Perfil completo de 1.000 y correcciones de entrada | [`ef46362`](https://github.com/guadalupejuan001-code/mundo-X/commit/ef4636240af73351a18710e988346791fbdc3b58) |
| Graphics 8K, incluyendo la línea 4K y optimizaciones propias | [`9fd8fc7`](https://github.com/guadalupejuan001-code/mundo-X/commit/9fd8fc7865cfee6f91430ad4726d6ed841f2b3f6) |
| Solar 4K, mapa y atmósferas | [`b6a324f`](https://github.com/guadalupejuan001-code/mundo-X/commit/b6a324f7d9a13709771f4589c8c082883607b788) |
| PC VBE/GLES hasta 8K y corrección SHLD/SHRD | [`bab0dd5`](https://github.com/guadalupejuan001-code/mundo-X/commit/bab0dd54e88f57191067a4e2b960291f16640acf) |
| PC x86/x64 y RAM por demanda | [`155f954`](https://github.com/guadalupejuan001-code/mundo-X/commit/155f954a79027a1cb3f2b9c1a43e5da8661b3303) |
| Consola original de la VM `.mxvm`, recuperada junto al motor ya integrado | [`9d095a8`](https://github.com/guadalupejuan001-code/mundo-X/commit/9d095a81b3820f4c50fc48fecd9c7ff08bd22b40) |

## Principio y componentes del motor

> **Principio de diseño:** representar y transformar lo necesario; no materializar todos los estados, ramas u objetos cuando su comportamiento puede obtenerse a partir de estructuras, relaciones y reglas de evolución.

Mundo X nació como investigación sobre representación de información y cálculo de su evolución. Un vector de estado denso requiere `2^n` amplitudes; las representaciones del proyecto aprovechan la estructura cuando el circuito la permite. La CPU sigue ejecutando instrucciones convencionales. Cada núcleo conserva su alcance, presupuesto y referencia de corrección. [Origen y recorrido](docs/origen_y_recorrido_cuantico.md), [mapa de motores](results/engine_map_2026-10-18.md).

### 1. Quantum Core y Dense Core — referencia matemática

**Archivos:** [`src/quantum_core.py`](src/quantum_core.py), [`native/include/mundo_x/dense_core.hpp`](native/include/mundo_x/dense_core.hpp).

Son simuladores de vectores de estado completos, útiles para verificar que las transformaciones del resto del sistema conservan el comportamiento esperado. Contienen primitivas para superposición, fase, interferencia, puertas como H/X/CNOT, entrelazamiento y otras operaciones según la implementación.

Su función principal dentro de Mundo X es ser la **referencia de corrección**, no la representación de escala que se pretende utilizar en producción.

### 2. Compact Core — núcleo de simulación compacta

**Archivo:** [`native/include/mundo_x/compact_core.hpp`](native/include/mundo_x/compact_core.hpp).

Motor C++17 que representa estados **Clifford/estabilizadores** mediante un *tableau* binario empaquetado. No almacena un vector completo de `2^n` amplitudes para esa familia.

Operaciones implementadas:

- Hadamard (`H`), fase (`S`), Pauli (`X`/`Z`).
- CNOT (`CX`) y CZ.
- Medición proyectiva `Z` y operaciones de validación de estados.
- Consulta de probabilidades Z y medición con muestreo Born seudoaleatorio reproducible.
- Construcción y comprobación de GHZ, estados cluster 2D y firmas de estabilizadores.

El almacenamiento del tableau escala aproximadamente con **`O(n²)` bits**, en vez de `O(2^n)` amplitudes complejas. Este resultado se refiere a **estados compatibles con estabilizadores**, no a todos los posibles estados cuánticos.

### 3. Exact T y Reduced Exact T — evolución no-Clifford

**Archivo:** [`native/include/mundo_x/exact_t_experiment.hpp`](native/include/mundo_x/exact_t_experiment.hpp).

El proyecto tiene dos representaciones que permiten estudiar circuitos con puerta no-Clifford `T`:

**A. Exact T, referencia por ramas.** Utiliza la identidad matemática:

~~~text
T = αI + βZ
α = (1 + exp(iπ/4)) / 2
β = (1 - exp(iπ/4)) / 2
~~~

Cada aplicación de `T` crea dos alternativas estabilizadoras ponderadas. Esta referencia preserva las ramas para estudiar corrección; su costo ingenuo crece como `2^T` en el número de puertas T.

**B. Reduced Exact T, representación durante la evolución.** No almacena un tableau independiente por cada rama ingenua. Mantiene:

~~~text
base estabilizadora compartida
+ etiquetas Pauli canónicas
+ coeficientes complejos
+ registro de operaciones
~~~

Las etiquetas equivalentes módulo el grupo estabilizador se identifican y **sus coeficientes se acumulan en el momento de operar**. Así se evita generar las `2^T` ramas completas en los circuitos probados.

La reconstrucción densa se utiliza **para validar al final los circuitos pequeños**, no como estado interno de evolución de la implementación reducida. La fidelidad numérica contra Dense Core fue cercana a 1 en los casos registrados.

**Límite importante:** evitar las `2^T` ramas no demuestra evitar el crecimiento `2^n` en el peor caso. Los circuitos probados de 8 qubits llegaron a 256 componentes; todavía hay un problema abierto de escalabilidad de coeficientes.

#### Estado *magic* y crecimiento de componentes

El comando `magic-witness` comprueba el estado `H → T` de un qubit: compara su geometría con los seis estados estabilizadores puros de un qubit. **Ese testigo concreto usa Dense Core**. Es una prueba de estado no-estabilizador, distinta de la validación de Reduced Exact T.

También hay trazas puerta por puerta para estudiar cuándo crece el número de componentes y cuándo aumenta su dificultad de factorización. Se observó crecimiento de soporte asociado a T en dos circuitos distintos; **no se confirmó** que Hadamard siempre incremente el rango interno.

### 4. Teletransportación de estado cuántico simulado

**Archivo:** [`native/include/mundo_x/teleport.hpp`](native/include/mundo_x/teleport.hpp).

Ejecuta el protocolo de teletransportación de **estado** usando un par de Bell, mediciones y correcciones clásicas. Verifica seis estados estabilizadores de un qubit frente a las cuatro ramas de medición, para **24 comprobaciones**.

No transporta materia ni transmite un qubit físico: es una simulación matemática del protocolo.

### 5. XASM — lenguaje mínimo para el runtime

**Archivo:** [`native/include/mundo_x/xasm.hpp`](native/include/mundo_x/xasm.hpp).

Formato de instrucciones `.x` con el que se ejecutan circuitos sobre Compact Core. Incluye `qubits`, `h`, `x`, `z`, `s`, `cx`, `show`, `bytes`, comprobaciones `assert` además de `probz`, `measure`, `seed` y consultas `expect`.

`probz q` consulta las probabilidades de medición Z; `measure q` obtiene un
resultado y proyecta el estado. `seed n` controla el muestreo seudoaleatorio
(semilla inicial 0). Los ejemplos [interferencia](examples/measurement_interference.x)
y [Bell](examples/measurement_bell.x) muestran fases y correlaciones.
Estas instrucciones también están en `practice`; aún no hay controles Android
para programarlas. Las consultas deterministas generales pueden requerir
eliminación del tableau: [costos y alcance](docs/origen_y_recorrido_cuantico.md#recorrido-programable-añadido).
**XASM v0.2** añade el modo experimental `backend reduced`, declarado antes de `qubits`, para programar circuitos Clifford+T con `t <q>`. Conserva Compact Core como modo predeterminado. El modo reducido permite de 1 a 1.000 qubits, hasta 128 puertas T y 65.536 componentes. Sus etiquetas Pauli usan 16 palabras de 64 bits para cubrir q0..q999. `expect` y `assert expectation` consultan operadores Pauli; `assert norm` comprueba los componentes. `assert fidelity` activa la referencia densa y se limita a 12 qubits. `show` informa componentes y puertas T, y `bytes` identifica la carga lógica estimada; esa cifra no mide la RAM total. El ancho de 1.000 no elimina el crecimiento potencial de componentes en circuitos no-Clifford generales. Consulta [sintaxis, límites y ejemplos de XASM v0.2](docs/xasm_v0_2.md).

~~~text
qubits 2
h 0
cx 0 1
show
bytes
assert ghz
~~~

Ejemplos reales: [`examples/hello.x`](examples/hello.x), [`examples/bell.x`](examples/bell.x), [`examples/ghz4.x`](examples/ghz4.x), [`examples/magic_t.x`](examples/magic_t.x), [`examples/clifford_t.x`](examples/clifford_t.x).

### 6. Lenguaje de alto nivel Mundo X — prototipo `.mx`

El compilador [`mxlang.hpp`](native/include/mundo_x/mxlang.hpp) permite describir circuitos mediante constantes, expresiones, repeticiones y bloques reutilizables con parámetros de tipo `qubit` o `count`. Así se evita escribir manualmente cada instrucción. El comando `compile` genera XASM v0.2 y `run-mx` compila y ejecuta con los motores existentes; la compilación comprueba tipos, índices, límites y ubicaciones de origen antes de ejecutar.

~~~text
backend compact;
qubits 2;
circuit bell(a: qubit, b: qubit) {
    h(a);
    cx(a, b);
}
bell(q[0], q[1]);
assert ghz;
~~~

La primera versión es un **lenguaje estático de circuitos**, con reglas documentadas y expansión acotada. Sus bloques se convierten en una secuencia XASM; no añade ejecución paralela ni nuevas operaciones matemáticas al motor. Consulta la [guía del lenguaje `.mx`](docs/mx_language_v0_1.md) y los ejemplos [`mx_bell.mx`](examples/mx_bell.mx), [`mx_magic.mx`](examples/mx_magic.mx) y [`mx_layers.mx`](examples/mx_layers.mx).

### 7. Bridge X y Transformer X — aprendizaje sobre modos

**Archivos:** [`src/bridge_x.py`](src/bridge_x.py), [`src/transformer_x.py`](src/transformer_x.py), [`experiments/train_file_v0_1.py`](experiments/train_file_v0_1.py).

**Bridge X** transforma fragmentos de bytes en hasta 256 modos identificables. Para cada modo activo representa actividad y posición mediante una pareja de componentes reales e imaginarias `z = [Re, Im]`, de la que pueden obtenerse amplitud y fase.

**Transformer X** es una red neuronal PyTorch real, con *TransformerEncoder*, atención y parámetros entrenables. Recibe identificadores de modo y estados complejos representados como dos números reales. Aprende a estimar una corrección:

~~~text
identidad de modo + estado (Re, Im)
       ↓
Transformer X
       ↓
corrección Δz
       ↓
estado estimado z + Δz
~~~

El script de entrenamiento usa ventanas consecutivas de un archivo e informa error de validación frente a una predicción identidad. **No es un modelo conversacional ni está integrado todavía a Compact Core**; el repositorio tampoco incluye un checkpoint entrenado listo para usarse como modelo general. Su utilidad práctica deberá medirse contra alternativas equivalentes.

### 8. FactorizedCore y fases generales

[FactorizedCore](native/include/mundo_x/factorized_core.hpp) conserva factores independientes y bloques, evita determinadas uniones que no generan entrelazamiento y verifica separaciones. Los ensayos de escala describen familias producto; los bloques generales siguen teniendo crecimiento exponencial. [Estructura y estabilidad](results/factorized_structure_2026-10-18.md), [ejecución en flujo](results/scale_exams_2026-10-18.md).

[AdaptivePhaseCore](native/include/mundo_x/adaptive_phase_core.hpp) ejecuta fases de ángulo general y transfiere el estado producto al motor reducido cuando aparece entrelazamiento. El ejecutable `mundo_x_phase` trabaja con `.qx`; la reproducción de la primera versión permanece como `adaptive-replay`. El ensayo de sumador de 50 qubits registró 1.600 bytes de carga lógica del motor producto, 393 ejecuciones de comparación y 16 entradas adicionales. Esta ruta nativa conserva sus límites y todavía no está conectada al panel gráfico Android. [Transferencia y evidencia](docs/state-transfer.md).

### 9. VM, Ecos y runtime visual

La VM didáctica utiliza CompactCore para cuatro registros de ocho bits y 16 bytes de RAM, con un intérprete clásico que contrasta cada instrucción. Su ejecutable restaurado `mundo_x_vm` permite ejecutar programas `.mxvm` desde terminal. [Sintaxis y comandos de la VM](docs/basis_vm_cli.md). Ecos conserva evolución, perturbación y reversión Clifford+T, con cancelación y presupuesto. [Código y modelos Android](android-experiments/README.md).

El runtime visual conecta resultados de cálculo con reglas, IDs y cambios de una escena. Puede exportar a XLab mediante **Restaurar escena**. La planificación espacial, las mallas y los lotes gráficos permanecen integrados. [Motor visual](docs/motor_x_visual.md), [planificación por regiones](docs/graphics_spatial.md).

## Resultados históricos medidos

Las cifras de esta sección corresponden a los registros fechados enlazados. Se conservan como evidencia de esas revisiones, separadas de las ejecuciones de la integración actual.

Las siguientes cifras proceden de ejecuciones registradas del proyecto y, donde corresponde, de capturas en hardware Android real. **No todas son benchmarks A/B controlados ni pruebas independientes de terceros.**

| Área / prueba | Resultado registrado | Alcance |
|---|---|---|
| GHZ 24 qubits, Compact Core | 408 bytes de estado | Runtime nativo |
| GHZ 10,000 qubits, Compact Core | ~23.97 MiB de estado | Estado estabilizador estructurado |
| GHZ 1,000 qubits, Android | 257,000 bytes; 8.909 ms | Captura en dispositivo |
| Cluster 2D 32 × 32, Android | 1,024 qubits; 263,168 bytes; 53.212 ms | Firma del cluster PASS |
| Teletransportación simulada | 24/24 PASS; 51 bytes de estado | Seis estados × cuatro ramas |
| Exact T, 4 qubits / 8 T | 256 ramas; fidelidad 1.000000000 | Referencia de ramas; también probado en Android |
| Reduced Exact T, 4 qubits / 12 T | 4,096 ramas teóricas → 16 componentes | Fidelidad 1.000000000 en CI |
| Reduced Exact T, 8 qubits / 64 T | `2^64` ramas teóricas **no materializadas** → 256 componentes | Fidelidad 1.000000000 en CI |
| GPU visual, Samsung A16 5G | 96,000 elementos a ~60–65 FPS | OpenGL ES, una llamada de dibujo por fotograma |

En la prueba `4 qubits / 12 T`, el registro reporta **737,562 bytes** para la representación ingenua explícita frente a **1,072 bytes de carga lógica** en la representación reducida. Son mediciones de estructuras diferentes dentro del programa; **la carga lógica no equivale a la RAM total del proceso**.

En la investigación posterior, una factorización secuencial de bajo rango **no** comprimió los coeficientes del caso de 8 qubits: el vector de 256 coeficientes complejos dio una estimación de 680 escalares para esa factorización. Se conserva ese resultado negativo porque identifica una limitación técnica real.

La [revisión del 17 de octubre](results/architecture_limits_2026-10-17.md) añade
un banco aislado de escala, comprobaciones numéricas independientes y controles
de entradas. La creación y evolución de Reduced Exact T mejoran aproximadamente 2–2,25× en
los tres circuitos comparados; el crecimiento exponencial de sus componentes
persiste. En esta revisión, su referencia y reconstrucción usan `DenseCore64`
(double), y el reporte declara esa precisión y sus bytes. `DenseCore` conserva
float32 para los benchmarks y resultados históricos. Los tiempos, presupuestos,
fallos y límites de cada comparación quedan documentados en el informe.

**Precisión sobre el renderizado:** los 608 bytes que aparece en las capturas `COMPACT → GPU` representan el **estado lógico explícito instrumentado**, no toda la memoria que emplea Android, los shaders o la GPU. La mejora de FPS registrada provino del traslado del renderizado Canvas a OpenGL ES, el procesamiento por lotes y la reconstrucción local; no se atribuye a una aceleración cuántica física.

Para los detalles, resultados y limitaciones de cada medición:

- [Estado funcional y validaciones](TESTED_FUNCTIONAL_STATUS_2026-10-14.md).
- [Resultados de Exact T, Reduced T, magic y trazas](results/exact_t_decomposition_2026-10-14.md).
- [Benchmark nativo: Compact frente a Dense](results/native_benchmark_2026-10-13.md).
- [Capturas y mediciones visuales Android](results/visual_android_samsung_a16_5g_2026-10-14.md).
- [Teletransportación cuántica simulada](results/compact_quantum_teleportation_2026-10-14.md).

## Otras herramientas y organización

| Ruta | Contenido |
| --- | --- |
| `native/` | Núcleos C++17, ABI compartida, lenguajes y contratos. |
| `cosmic-android/` | App principal que consume fuentes compartidas de Graphics y laboratorios. |
| `graphics-android/`, `graphics-desktop/` | Observador gráfico, resolución, proyectos `.cosmic` y validaciones. |
| `solar-android/` | Explorador solar, catálogo, modelos, texturas y pruebas. |
| `pc-android/` | Intérprete x86/x64 propio, memoria, medios, CLI y app. |
| `app/`, `android-experiments/` | XLab, MicroLab, VM, Ecos y experimentos Android originales. |
| `src/`, `experiments/`, `tests/` | Referencia Python, Bridge/Transformer y experimentos. |
| `examples/`, `docs/`, `results/` | Programas, contratos, fuentes de datos y evidencia fechada. |
| `tools/`, `.github/workflows/` | Auditores, benchmarks, compilación y verificación automatizada. |

### Referencia Python y Transformer X

```sh
python -m pip install -r requirements.txt pytest
python hello_mundo_x.py
python -m pytest tests/test_quantum_core.py
python src/transformer_x.py
python experiments/train_file_v0_1.py ruta/a/un_archivo --epochs 20
```

El entrenamiento usa ventanas del archivo indicado y guarda `transformer_x_v0_1.pt`. El repositorio conserva el programa de entrenamiento; no incluye un checkpoint general ya entrenado. [Implementación y medición](experiments/train_file_v0_1.py).

### Red experimental

`python tools/mundox_relay.py` inicia el relay TCP de pruebas en el puerto 7777. Las actividades HOST/JOIN intercambian eventos para reconstruir el estado localmente. El protocolo actual carece de cifrado y autenticación; su uso documentado corresponde a una red de confianza. La verificación física completa entre dos dispositivos sigue pendiente. [Relay](tools/mundox_relay.py).

### Distribuciones y registros anteriores

La publicación [XLab 0.3.2, ZIP con APK firmado](https://github.com/guadalupejuan001-code/mundo-X/releases/download/v0.3.2/XLab-Objetos-0.3.2.zip) se conserva como **distribución histórica de XLab**. [Descarga alternativa](https://raw.githubusercontent.com/guadalupejuan001-code/mundo-X/6b206566022afa8b7b20b9e6dd3153ce8dad9182/dist/XLab-Objetos-0.3.2.zip), [validación de objetos y millón de instancias](results/xlab_millon_2026-10-18.md). Esa APK corresponde a su revisión publicada; los proyectos integrados se construyen con los destinos y workflows anteriores.

El titular usó «computasis» como analogía de homeostasis. El controlador añadido posteriormente no define la arquitectura original y fue retirado del código activo; algunos informes anteriores lo mencionan como parte de su historia. [Revisión de origen y medición](results/revision_origen_y_medicion_2026-10-18.md), [exámenes originales](results/native_exams_2026-10-18.md).

## Alcance y autoría

El proyecto conserva simulación compacta Clifford, experimentos no Clifford, lenguajes, aplicaciones y pruebas reproducibles. Quedan abiertos la escala de circuitos generales con T, la evaluación de Transformer X, la compatibilidad completa de PC y mediciones en dispositivos físicos. Los resultados disponibles no establecen una simulación eficiente de todos los estados arbitrarios ni una aceleración de hardware cuántico.

**Mundo X es un proyecto concebido y dirigido por el titular del repositorio.** El historial conserva implementaciones, revisiones y resultados, incluidos los negativos. [Estado histórico](PROJECT_STATUS_2026-10-14.md), [historial de `main`](https://github.com/guadalupejuan001-code/mundo-X/commits/main), [compilaciones y evidencia](https://github.com/guadalupejuan001-code/mundo-X/actions).

El código está visible y continúa en desarrollo. La raíz no incluye un archivo `LICENSE`; este README no concede una licencia adicional de reutilización.
 --output-on-failure
```

**Compatibilidad primero:** Ether Render X v0.2 sigue siendo un renderizador
**CPU** para hasta **64 esferas**. Aún no sustituye a `QuantumRenderer` de GLES,
a Solar, a los espejos ni a los adaptadores de PC y laboratorios. Estos se
conservan sin modificar mientras la migración incremental alcanza sus
funciones, exactitud y rendimiento; no se presentan pruebas CPU como FPS de GPU.
No se incorpora la eliminación de componentes de la PR #33 ni el prototipo
APK de la PR #34. [Diseño](docs/ether-render-x.md) ·
[Inventario](docs/render-engine-inventory.md).

## Qué incluye el código integrado

| Componente | Función y alcance | Código y documentación |
| --- | --- | --- |
| Ether y perfil finito compatible | Nuevos mundos Android con reglas globales y modificaciones locales en el mismo estado; perfil finito explícito `q0`–`q999`, puertas Clifford y ampliación de proyectos anteriores. | [Núcleo nativo](native/), [perfil de 1.000](docs/core_1000_qubits.md), [sesión compartida](docs/cosmic_x_engine.md) |
| COSMIC X Engine Android | Paneles Motor, Núcleo y Proyectos, más 14 experimentos originales dentro de la misma instalación. | [App COSMIC](cosmic-android/README.md) |
| Graphics Android 4K/8K | Escena procedural, selección y edición por ID, renderizado UHD, modo 8K sujeto a capacidades y exportación PNG por mosaicos. | [Graphics](graphics-android/README.md), [4K](graphics-android/docs/4k.md), [8K](graphics-android/docs/8k.md) |
| Motor gráfico de escritorio | Compila el mismo puente nativo, observa la misma representación y abre/guarda archivos `.cosmic` compatibles con Android. | [Escritorio](graphics-desktop/README.md), [formato](graphics-desktop/PROJECT_FORMAT.md) |
| COSMIC X Solar | Sol, ocho planetas y catorce lunas; navegación, mapa orbital, tiempo variable, ambientes de descenso y captura UHD. Su preparación actúa sobre ocho direcciones del mismo dominio Ether que alimenta sus señales visuales. | [Solar](solar-android/README.md), [modelos orbitales](solar-android/docs/solar-science.md), [ambientes](solar-android/docs/atmosphere-science.md) |
| COSMIC X PC | Intérprete propio de un subconjunto x86/x64, RAM por demanda, SHLD/SHRD, BIOS de texto/teclado/disco y framebuffer huésped VBE hasta 8K presentado con GLES 3; dispositivo InfiniteCore y viewport sobre la sesión cuántica compartida. Arranques propios hasta modo largo de 64 bits. | [PC propia](pc-android/README.md) |
| XLab, MicroLab, VM y Ecos | Mesa de circuitos, protocolos, memoria, objetos, micromundo, máquina de 8 bits y experimentos de evolución/perturbación/reversión. | [XLab](docs/xlab_0_2.md), [objetos](docs/xlab_0_3_objetos.md), [VM y Ecos](android-experiments/README.md) |
| Lenguajes y núcleos experimentales | XASM `.x`, circuitos `.mx`, fases generales `.qx`, reducción de T y factorización con presupuestos explícitos. | [XASM](docs/xasm_v0_2.md), [.mx](docs/mx_language_v0_1.md), [fases](docs/state-transfer.md) |
| Física nativa 2D y 3D | Modelos clásicos finitos con recuperación de fallos; el CLI `physics3d` ejecuta la base 3D rescatada. Sus cuerpos aún no son estado Ether ni alimentan las órbitas de Solar. | [Auditoría del núcleo](docs/repository-core-review-2026-10-21.md) |
| Bridge X y Transformer X | Codificación de bytes en modos y modelo PyTorch entrenable. Se conserva como línea independiente; todavía no alimenta CompactCore. | [Bridge X](src/bridge_x.py), [Transformer X](src/transformer_x.py), [entrenamiento](experiments/train_file_v0_1.py) |

La APK principal `com.mundox.cosmic` reúne el motor, el núcleo y los laboratorios. **Solar y PC son aplicaciones separadas dentro del mismo código fuente.** La VM didáctica de 8 bits está incluida en COSMIC; el emulador x86/x64 de PC conserva su propio destino de compilación.

## Ether y compatibilidad con 1.000 qubits

Una sesión gráfica nueva en Android utiliza **Ether**, con fondo entrelazado definido por dos reglas y sin un tamaño total de registro. El selector de nuevo proyecto permite elegir un registro finito de **1 a 1.000 qubits**. En Android, **Ampliar a 1000** reproduce el historial de una sesión anterior en el registro mayor y agrega qubits en `|0>`. Conserva los observables originales, sus signos, la semilla, los objetos, las ediciones, la cámara y el tiempo. Los proyectos guardados conservan su dimensión al abrirse; los ejemplos Bell y GHZ pequeños mantienen su tamaño didáctico.

El contrato comprueba entrelazamiento GHZ sobre los 1.000 qubits, correlaciones, medición, signos Pauli y operaciones sobre `q999`. La [documentación del perfil](docs/core_1000_qubits.md) distingue esas pruebas del oráculo denso pequeño y explica la compatibilidad de archivos.

| Representación | Alcance que debe usarse al programar |
| --- | --- |
| Sesión gráfica / CompactCore | Perfil portable de 1 a 1.000 qubits; H, S, X, Y, Z, CX y CZ. El perfil finito de 1.000 conserva 3.000 observables iniciales. |
| CompactCore nativo / XASM | Circuitos estabilizadores, entrelazamiento Clifford y medición Z reproducible; incluye `examples/ghz1000.x`. |
| Reduced Exact T | Interfaz XASM de 1 a 1.000 qubits, 128 puertas T y 65.536 componentes como límites. Las máscaras Pauli son multi-palabra; el costo depende del crecimiento real de componentes. |
| FactorizedCore | Mantiene factores independientes y bloques con presupuesto; el límite de bloque denso sigue siendo 20 qubits. |
| Solar | Prepara ocho direcciones y observa trece señales de una única sesión Ether; esas direcciones no limitan el dominio total. |
| Laboratorios | Cada experimento conserva la representación que requiere su modelo; no se sustituyen silenciosamente sus backends. |

El tableau de 1.000 almacena **257.000 bytes** en sus buffers. Esa cifra no incluye observador, historial, Java, Android ni GPU. El perfil compacto corresponde a estados estabilizadores; los circuitos generales con muchas puertas T no adquieren por este cambio el mismo costo de memoria. [Fórmula, benchmark de 20/60/1.000 y límites](docs/core_1000_qubits.md#memoria-y-observación).

### Compilar el conjunto nativo

Desde Linux, con CMake y un compilador C++17:

```sh
git clone https://github.com/guadalupejuan001-code/mundo-X.git
cd mundo-X
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure

./build/mundo_x_infinite examples/ether_fusion.xi
./build/mundo_x run examples/ghz1000.x
./build/mundo_x run examples/t60.x
./build/mundo_x run-mx examples/mx_layers.mx
./build/mundo_x_phase examples/adder50.qx adaptive
./build/mundo_x physics3d 256 120
./build/mundo_x_vm --self-test
./build/pc-android/cosmic_pc_cli --demo64 --ram-mib 2048 --resident-mib 1024
```

La compilación raíz incluye los contratos del núcleo, la VM y las diez suites de PC, incluidas `pc_video` y `pc_quantum`. `BUILD_TESTING=OFF` permite construir los ejecutables sin los tests. Las instrucciones de PC ejecutan los arranques propios de demostración; no arrancan Windows.

### Crear o ampliar un proyecto de escritorio

```sh
python -m pip install -r graphics-desktop/requirements.txt
python graphics-desktop/demo.py --no-render --export-project mundo-1000.cosmic
python graphics-desktop/demo.py --project mundo-anterior.cosmic \
  --expand-qubits 1000 --gate X:999 --no-render \
  --export-project mundo-ampliado.cosmic
```

Sustituye `mundo-anterior.cosmic` por tu archivo. La exportación sin render no necesita un contexto gráfico; para producir imágenes, el adaptador de escritorio necesita EGL/OpenGL ES y actualmente construye una biblioteca Linux `.so`. [Uso y ejemplos](graphics-desktop/README.md).

## Aplicaciones Android y resoluciones

Requisitos de construcción declarados: **Java 17, Gradle 8.9, AGP 8.7.3, SDK 35, NDK 27.2.12479018 y CMake 3.22.1**. Los proyectos no incluyen Gradle Wrapper. Los comandos siguientes se ejecutan desde la raíz con Gradle instalado; las aplicaciones nuevas admiten Android 8.0 o posterior y las ABI ARM64 y x86_64.

| Destino | Identidad y versión | Compilar desde la raíz |
| --- | --- | --- |
| COSMIC unificado | `com.mundox.cosmic` · 0.3.1 | `gradle -p cosmic-android :app:assembleDebug` |
| Graphics | `com.mundox.graphics` · 0.4.0 | `gradle -p graphics-android :app:assembleDebug` |
| Graphics 4K | `com.mundox.graphics.uhd` · 0.4.0 | `gradle -p graphics-android -Pcosmic4kStandalone=true :app:assembleDebug` |
| Graphics 8K | `com.mundox.graphics.ultra` · 0.4.0 | `gradle -p graphics-android -Pcosmic8kStandalone=true :app:assembleDebug` |
| Solar | `com.cosmicx.solar` · 1.3.0 | `gradle -p solar-android :app:assembleDebug` |
| Solar 4K | `com.cosmicx.solar.uhd` · 1.3.0 | `gradle -p solar-android -Psolar4kStandalone=true :app:assembleDebug` |
| Solar 8K | `com.cosmicx.solar.ultra` · 1.3.0 | `gradle -p solar-android -Psolar8kStandalone=true :app:assembleDebug` |
| PC propia | `com.cosmicx.pc` · 0.4.0 | `gradle -p pc-android :app:assembleDebug` |
| XLab original | Proyecto Android de la raíz | `gradle :app:assembleDebug` |

Los indicadores de Graphics 4K y 8K son mutuamente excluyentes. Se puede añadir `:app:assembleDebugAndroidTest` para construir las pruebas instrumentales. GitHub Actions conserva APK, firma, SHA-256, revisión del código y evidencia; los artefactos corresponden a la ejecución seleccionada.

### Qué hacen 4K y 8K

Graphics conserva hasta **1.048.576 cajas** como límite del modelo, con IDs estables y edición individual. El modo UHD usa **3840 × 2160** o **2160 × 3840**. La ruta Ultra incorpora **7680 × 4320**, límites comprobados del dispositivo y exportación PNG por mosaicos, que evita mantener toda la imagen RGBA 8K en un único buffer de CPU. La preferencia de modo UHD pertenece al dispositivo y se preserva al ampliar una sesión; ese modo se omite del archivo `.cosmic` portable, mientras que el límite adaptativo `resolutionLimit` sí forma parte del proyecto. [Pruebas de 4K](graphics-android/docs/4k.md), [mosaicos y presupuesto 8K](graphics-android/docs/8k.md).

Los paneles Motor y Núcleo de COSMIC comparten `NativeCore`. Observar, seleccionar o mover la cámara no añade puertas ni realiza una medición proyectiva. Las pruebas comparan imágenes y estado para comprobar esta relación. La cantidad de objetos y la resolución admitida no expresan una garantía de FPS en un teléfono.

### Solar y PC

Solar reutiliza el núcleo y la geometría de resolución compartidos, pero sus órbitas, escalas, tiempo y ambientes tienen modelos propios. Incluye **23 cuerpos**, descenso a 22 planetas/lunas, mapa interactivo, texturas incorporadas y representaciones magnéticas. Sus aproximaciones orbitales y visuales están delimitadas en [ciencia solar](solar-android/docs/solar-science.md) y [atmósferas y mapa](solar-android/docs/atmosphere-science.md); no constituyen una simulación climática completa.

COSMIC X PC **0.4.0** ejecuta un intérprete C++ propio. Su prueba x64 realiza la transición real → protegido → largo y verifica datos de 64 bits en registros y RAM. La CPU incorpora SHRD (`0F AC/AD`) y SHLD (`0F A4/A5`) en 16/32/64 bits, incluida la instrucción que antes detenía un cargador ISO con `two-byte opcode is not implemented`. La regresión reproduce el opcode; la compatibilidad de la ISO completa necesita su propio arranque comprobado.

PC incorpora ahora un dispositivo de vídeo lineal con una interfaz VBE limitada. **Prueba gráfica** permite generar píxeles desde instrucciones del huésped a **1280 × 720, 3840 × 2160 y 7680 × 4320**. Un presentador GLES 3 propio consume ese framebuffer mediante JNI y utiliza la misma clase [RenderResolution](graphics-android/app/src/main/java/com/mundox/graphics/RenderResolution.java) que Graphics. La interfaz distingue **fuente huésped**, **salida de renderizado** y **superficie visible de Android**: seleccionar salida 8K no convierte una fuente de texto o de menor resolución en un huésped 8K ni cambia la resolución física del teléfono. La asignación 8K comprueba las capacidades GL y conserva la última salida válida si el dispositivo la rechaza.

El panel Núcleo permite enviar programas al mismo `InfiniteSession` que utiliza el invitado mediante los puertos `0x5850` y `0x5851`. El viewport cuántico utiliza `QuantumRenderer` para observar esa sesión; **Guardar núcleo** conserva su estado actual y RNG en un archivo `.mxi`. Las pruebas comprueban que una instrucción del invitado cambia observables y píxeles del viewport. [Conexión, comandos y persistencia](docs/infinite_integration.md).

El perfil Android conserva **2 GiB de RAM virtual** con un límite de **1 GiB de páginas respaldadas**. Vídeo tiene presupuestos separados: **512 MiB para texturas GL simultáneas** y **256 MiB para instantáneas nativas**; PSS informa el proceso Android por separado. Esos límites no garantizan un consumo total fijo de 2 GiB. La CPU x86/x64 no recibe aceleración del núcleo de qubits por compartir este repositorio. **Todavía no instala ni ejecuta Windows completo o juegos de PC.** [Uso, dispositivo de vídeo, pruebas y límites](pc-android/README.md).

### Windows x64

Los ejecutables nativos también se construyen con MSVC:

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
.\build\Release\mundo_x.exe run examples\ghz1000.x
.\build\Release\mundo_x_vm.exe --self-test
.\build\pc-android\Release\cosmic_pc_cli.exe --demo64
```

El workflow [Windows x64](.github/workflows/windows-x64.yml) conserva el paquete de ejecutables, ejemplos e instrucciones. Este destino ejecuta Mundo X **sobre Windows anfitrión**; la compatibilidad del emulador PC con un Windows invitado es un asunto distinto.

## Verificación y procedencia de la integración

Los workflows comprueban los cambios de código en solicitudes de integración a `main` y conservan los resultados del commit ejecutado. Las suites cubren ámbitos diferentes:

| Ruta | Comprobaciones que conserva |
| --- | --- |
| [Contratos nativos y escritorio](.github/workflows/cosmic-contracts.yml) | Núcleos, GHZ de 1.000, qubit 999, presupuestos, restauración, proyectos `.cosmic`, VM, CPU/PC y relación causal entre núcleo e imagen. |
| [COSMIC Android](.github/workflows/cosmic-android.yml) | 26 casos COSMIC, 22 Graphics y 8 MicroLab sobre las fuentes compartidas. |
| [Graphics](.github/workflows/graphics-android.yml), [4K](.github/workflows/graphics-4k.yml), [8K](.github/workflows/graphics-8k.yml) | Inventarios de 23, 26 y 30 casos: registro de 1.000, Infinite, estado portable, reutilización exacta de fotogramas, espejos en el perfil normal, selección, orientación y PNG. |
| [Solar](.github/workflows/solar-android.yml), [Solar 4K](.github/workflows/solar-4k.yml) y [Solar 8K](.github/workflows/solar-8k.yml) | 511 comprobaciones analíticas; 13 casos Android del perfil normal, 19 del perfil 4K/ambientes y 15 del perfil 8K. |
| [PC propia](.github/workflows/own-pc-android.yml) | Diez suites nativas y 30 casos Android previstos: arranque, memoria, regresión ISO ENTER/LEAVE, vídeo huésped/GLES hasta 8K, propiedad de instantáneas JNI y sesión Infinite compartida entre CPU y viewport. |
| [Windows](.github/workflows/windows-x64.yml), [runtime visual](.github/workflows/visual-runtime.yml), [XLab](.github/workflows/android-apk.yml) | Compilación del conjunto en sus plataformas y regresiones de los experimentos conservados. |

Los recuentos describen el inventario esperado; el resultado de cada ejecución está en [Actions](https://github.com/guadalupejuan001-code/mundo-X/actions). Una prueba que pasa demuestra las condiciones examinadas. Las pruebas GLES en Android virtual no sustituyen una medición de rendimiento en el teléfono físico.

El auditor común [cosmic_source_provenance.py](tools/cosmic_source_provenance.py) compara los archivos compartidos byte a byte con las revisiones de referencia. Los informes 4K/8K/Solar declaran la actualización al núcleo de 1.000 y las diferencias reales respecto al núcleo 4K anterior; también comprueban firma, identidad y bibliotecas de sus APK.

La procedencia de PC 0.4.0 registra la identidad real del APK, el commit y los SHA-256 de su puente de vídeo, del dispositivo VBE, de la CPU, del puente Infinite y de las clases gráficas compartidas. Comprueba las bibliotecas `libcosmic_pc.so`, `libmx_graphics.so` y `libc++_shared.so` para ARM64 y x86_64. Sus pruebas guardan por separado el estado del huésped, el estado del presentador y capturas; el inventario documentado no sustituye al resultado de Actions de esa revisión.

| Trabajo incorporado | Revisión de origen |
| --- | --- |
| Perfil completo de 1.000 y correcciones de entrada | [`ef46362`](https://github.com/guadalupejuan001-code/mundo-X/commit/ef4636240af73351a18710e988346791fbdc3b58) |
| Graphics 8K, incluyendo la línea 4K y optimizaciones propias | [`9fd8fc7`](https://github.com/guadalupejuan001-code/mundo-X/commit/9fd8fc7865cfee6f91430ad4726d6ed841f2b3f6) |
| Solar 4K, mapa y atmósferas | [`b6a324f`](https://github.com/guadalupejuan001-code/mundo-X/commit/b6a324f7d9a13709771f4589c8c082883607b788) |
| PC VBE/GLES hasta 8K y corrección SHLD/SHRD | [`bab0dd5`](https://github.com/guadalupejuan001-code/mundo-X/commit/bab0dd54e88f57191067a4e2b960291f16640acf) |
| PC x86/x64 y RAM por demanda | [`155f954`](https://github.com/guadalupejuan001-code/mundo-X/commit/155f954a79027a1cb3f2b9c1a43e5da8661b3303) |
| Consola original de la VM `.mxvm`, recuperada junto al motor ya integrado | [`9d095a8`](https://github.com/guadalupejuan001-code/mundo-X/commit/9d095a81b3820f4c50fc48fecd9c7ff08bd22b40) |

## Principio y componentes del motor

> **Principio de diseño:** representar y transformar lo necesario; no materializar todos los estados, ramas u objetos cuando su comportamiento puede obtenerse a partir de estructuras, relaciones y reglas de evolución.

Mundo X nació como investigación sobre representación de información y cálculo de su evolución. Un vector de estado denso requiere `2^n` amplitudes; las representaciones del proyecto aprovechan la estructura cuando el circuito la permite. La CPU sigue ejecutando instrucciones convencionales. Cada núcleo conserva su alcance, presupuesto y referencia de corrección. [Origen y recorrido](docs/origen_y_recorrido_cuantico.md), [mapa de motores](results/engine_map_2026-10-18.md).

### 1. Quantum Core y Dense Core — referencia matemática

**Archivos:** [`src/quantum_core.py`](src/quantum_core.py), [`native/include/mundo_x/dense_core.hpp`](native/include/mundo_x/dense_core.hpp).

Son simuladores de vectores de estado completos, útiles para verificar que las transformaciones del resto del sistema conservan el comportamiento esperado. Contienen primitivas para superposición, fase, interferencia, puertas como H/X/CNOT, entrelazamiento y otras operaciones según la implementación.

Su función principal dentro de Mundo X es ser la **referencia de corrección**, no la representación de escala que se pretende utilizar en producción.

### 2. Compact Core — núcleo de simulación compacta

**Archivo:** [`native/include/mundo_x/compact_core.hpp`](native/include/mundo_x/compact_core.hpp).

Motor C++17 que representa estados **Clifford/estabilizadores** mediante un *tableau* binario empaquetado. No almacena un vector completo de `2^n` amplitudes para esa familia.

Operaciones implementadas:

- Hadamard (`H`), fase (`S`), Pauli (`X`/`Z`).
- CNOT (`CX`) y CZ.
- Medición proyectiva `Z` y operaciones de validación de estados.
- Consulta de probabilidades Z y medición con muestreo Born seudoaleatorio reproducible.
- Construcción y comprobación de GHZ, estados cluster 2D y firmas de estabilizadores.

El almacenamiento del tableau escala aproximadamente con **`O(n²)` bits**, en vez de `O(2^n)` amplitudes complejas. Este resultado se refiere a **estados compatibles con estabilizadores**, no a todos los posibles estados cuánticos.

### 3. Exact T y Reduced Exact T — evolución no-Clifford

**Archivo:** [`native/include/mundo_x/exact_t_experiment.hpp`](native/include/mundo_x/exact_t_experiment.hpp).

El proyecto tiene dos representaciones que permiten estudiar circuitos con puerta no-Clifford `T`:

**A. Exact T, referencia por ramas.** Utiliza la identidad matemática:

~~~text
T = αI + βZ
α = (1 + exp(iπ/4)) / 2
β = (1 - exp(iπ/4)) / 2
~~~

Cada aplicación de `T` crea dos alternativas estabilizadoras ponderadas. Esta referencia preserva las ramas para estudiar corrección; su costo ingenuo crece como `2^T` en el número de puertas T.

**B. Reduced Exact T, representación durante la evolución.** No almacena un tableau independiente por cada rama ingenua. Mantiene:

~~~text
base estabilizadora compartida
+ etiquetas Pauli canónicas
+ coeficientes complejos
+ registro de operaciones
~~~

Las etiquetas equivalentes módulo el grupo estabilizador se identifican y **sus coeficientes se acumulan en el momento de operar**. Así se evita generar las `2^T` ramas completas en los circuitos probados.

La reconstrucción densa se utiliza **para validar al final los circuitos pequeños**, no como estado interno de evolución de la implementación reducida. La fidelidad numérica contra Dense Core fue cercana a 1 en los casos registrados.

**Límite importante:** evitar las `2^T` ramas no demuestra evitar el crecimiento `2^n` en el peor caso. Los circuitos probados de 8 qubits llegaron a 256 componentes; todavía hay un problema abierto de escalabilidad de coeficientes.

#### Estado *magic* y crecimiento de componentes

El comando `magic-witness` comprueba el estado `H → T` de un qubit: compara su geometría con los seis estados estabilizadores puros de un qubit. **Ese testigo concreto usa Dense Core**. Es una prueba de estado no-estabilizador, distinta de la validación de Reduced Exact T.

También hay trazas puerta por puerta para estudiar cuándo crece el número de componentes y cuándo aumenta su dificultad de factorización. Se observó crecimiento de soporte asociado a T en dos circuitos distintos; **no se confirmó** que Hadamard siempre incremente el rango interno.

### 4. Teletransportación de estado cuántico simulado

**Archivo:** [`native/include/mundo_x/teleport.hpp`](native/include/mundo_x/teleport.hpp).

Ejecuta el protocolo de teletransportación de **estado** usando un par de Bell, mediciones y correcciones clásicas. Verifica seis estados estabilizadores de un qubit frente a las cuatro ramas de medición, para **24 comprobaciones**.

No transporta materia ni transmite un qubit físico: es una simulación matemática del protocolo.

### 5. XASM — lenguaje mínimo para el runtime

**Archivo:** [`native/include/mundo_x/xasm.hpp`](native/include/mundo_x/xasm.hpp).

Formato de instrucciones `.x` con el que se ejecutan circuitos sobre Compact Core. Incluye `qubits`, `h`, `x`, `z`, `s`, `cx`, `show`, `bytes`, comprobaciones `assert` además de `probz`, `measure`, `seed` y consultas `expect`.

`probz q` consulta las probabilidades de medición Z; `measure q` obtiene un
resultado y proyecta el estado. `seed n` controla el muestreo seudoaleatorio
(semilla inicial 0). Los ejemplos [interferencia](examples/measurement_interference.x)
y [Bell](examples/measurement_bell.x) muestran fases y correlaciones.
Estas instrucciones también están en `practice`; aún no hay controles Android
para programarlas. Las consultas deterministas generales pueden requerir
eliminación del tableau: [costos y alcance](docs/origen_y_recorrido_cuantico.md#recorrido-programable-añadido).
**XASM v0.2** añade el modo experimental `backend reduced`, declarado antes de `qubits`, para programar circuitos Clifford+T con `t <q>`. Conserva Compact Core como modo predeterminado. El modo reducido permite de 1 a 1.000 qubits, hasta 128 puertas T y 65.536 componentes. Sus etiquetas Pauli usan 16 palabras de 64 bits para cubrir q0..q999. `expect` y `assert expectation` consultan operadores Pauli; `assert norm` comprueba los componentes. `assert fidelity` activa la referencia densa y se limita a 12 qubits. `show` informa componentes y puertas T, y `bytes` identifica la carga lógica estimada; esa cifra no mide la RAM total. El ancho de 1.000 no elimina el crecimiento potencial de componentes en circuitos no-Clifford generales. Consulta [sintaxis, límites y ejemplos de XASM v0.2](docs/xasm_v0_2.md).

~~~text
qubits 2
h 0
cx 0 1
show
bytes
assert ghz
~~~

Ejemplos reales: [`examples/hello.x`](examples/hello.x), [`examples/bell.x`](examples/bell.x), [`examples/ghz4.x`](examples/ghz4.x), [`examples/magic_t.x`](examples/magic_t.x), [`examples/clifford_t.x`](examples/clifford_t.x).

### 6. Lenguaje de alto nivel Mundo X — prototipo `.mx`

El compilador [`mxlang.hpp`](native/include/mundo_x/mxlang.hpp) permite describir circuitos mediante constantes, expresiones, repeticiones y bloques reutilizables con parámetros de tipo `qubit` o `count`. Así se evita escribir manualmente cada instrucción. El comando `compile` genera XASM v0.2 y `run-mx` compila y ejecuta con los motores existentes; la compilación comprueba tipos, índices, límites y ubicaciones de origen antes de ejecutar.

~~~text
backend compact;
qubits 2;
circuit bell(a: qubit, b: qubit) {
    h(a);
    cx(a, b);
}
bell(q[0], q[1]);
assert ghz;
~~~

La primera versión es un **lenguaje estático de circuitos**, con reglas documentadas y expansión acotada. Sus bloques se convierten en una secuencia XASM; no añade ejecución paralela ni nuevas operaciones matemáticas al motor. Consulta la [guía del lenguaje `.mx`](docs/mx_language_v0_1.md) y los ejemplos [`mx_bell.mx`](examples/mx_bell.mx), [`mx_magic.mx`](examples/mx_magic.mx) y [`mx_layers.mx`](examples/mx_layers.mx).

### 7. Bridge X y Transformer X — aprendizaje sobre modos

**Archivos:** [`src/bridge_x.py`](src/bridge_x.py), [`src/transformer_x.py`](src/transformer_x.py), [`experiments/train_file_v0_1.py`](experiments/train_file_v0_1.py).

**Bridge X** transforma fragmentos de bytes en hasta 256 modos identificables. Para cada modo activo representa actividad y posición mediante una pareja de componentes reales e imaginarias `z = [Re, Im]`, de la que pueden obtenerse amplitud y fase.

**Transformer X** es una red neuronal PyTorch real, con *TransformerEncoder*, atención y parámetros entrenables. Recibe identificadores de modo y estados complejos representados como dos números reales. Aprende a estimar una corrección:

~~~text
identidad de modo + estado (Re, Im)
       ↓
Transformer X
       ↓
corrección Δz
       ↓
estado estimado z + Δz
~~~

El script de entrenamiento usa ventanas consecutivas de un archivo e informa error de validación frente a una predicción identidad. **No es un modelo conversacional ni está integrado todavía a Compact Core**; el repositorio tampoco incluye un checkpoint entrenado listo para usarse como modelo general. Su utilidad práctica deberá medirse contra alternativas equivalentes.

### 8. FactorizedCore y fases generales

[FactorizedCore](native/include/mundo_x/factorized_core.hpp) conserva factores independientes y bloques, evita determinadas uniones que no generan entrelazamiento y verifica separaciones. Los ensayos de escala describen familias producto; los bloques generales siguen teniendo crecimiento exponencial. [Estructura y estabilidad](results/factorized_structure_2026-10-18.md), [ejecución en flujo](results/scale_exams_2026-10-18.md).

[AdaptivePhaseCore](native/include/mundo_x/adaptive_phase_core.hpp) ejecuta fases de ángulo general y transfiere el estado producto al motor reducido cuando aparece entrelazamiento. El ejecutable `mundo_x_phase` trabaja con `.qx`; la reproducción de la primera versión permanece como `adaptive-replay`. El ensayo de sumador de 50 qubits registró 1.600 bytes de carga lógica del motor producto, 393 ejecuciones de comparación y 16 entradas adicionales. Esta ruta nativa conserva sus límites y todavía no está conectada al panel gráfico Android. [Transferencia y evidencia](docs/state-transfer.md).

### 9. VM, Ecos y runtime visual

La VM didáctica utiliza CompactCore para cuatro registros de ocho bits y 16 bytes de RAM, con un intérprete clásico que contrasta cada instrucción. Su ejecutable restaurado `mundo_x_vm` permite ejecutar programas `.mxvm` desde terminal. [Sintaxis y comandos de la VM](docs/basis_vm_cli.md). Ecos conserva evolución, perturbación y reversión Clifford+T, con cancelación y presupuesto. [Código y modelos Android](android-experiments/README.md).

El runtime visual conecta resultados de cálculo con reglas, IDs y cambios de una escena. Puede exportar a XLab mediante **Restaurar escena**. La planificación espacial, las mallas y los lotes gráficos permanecen integrados. [Motor visual](docs/motor_x_visual.md), [planificación por regiones](docs/graphics_spatial.md).

## Resultados históricos medidos

Las cifras de esta sección corresponden a los registros fechados enlazados. Se conservan como evidencia de esas revisiones, separadas de las ejecuciones de la integración actual.

Las siguientes cifras proceden de ejecuciones registradas del proyecto y, donde corresponde, de capturas en hardware Android real. **No todas son benchmarks A/B controlados ni pruebas independientes de terceros.**

| Área / prueba | Resultado registrado | Alcance |
|---|---|---|
| GHZ 24 qubits, Compact Core | 408 bytes de estado | Runtime nativo |
| GHZ 10,000 qubits, Compact Core | ~23.97 MiB de estado | Estado estabilizador estructurado |
| GHZ 1,000 qubits, Android | 257,000 bytes; 8.909 ms | Captura en dispositivo |
| Cluster 2D 32 × 32, Android | 1,024 qubits; 263,168 bytes; 53.212 ms | Firma del cluster PASS |
| Teletransportación simulada | 24/24 PASS; 51 bytes de estado | Seis estados × cuatro ramas |
| Exact T, 4 qubits / 8 T | 256 ramas; fidelidad 1.000000000 | Referencia de ramas; también probado en Android |
| Reduced Exact T, 4 qubits / 12 T | 4,096 ramas teóricas → 16 componentes | Fidelidad 1.000000000 en CI |
| Reduced Exact T, 8 qubits / 64 T | `2^64` ramas teóricas **no materializadas** → 256 componentes | Fidelidad 1.000000000 en CI |
| GPU visual, Samsung A16 5G | 96,000 elementos a ~60–65 FPS | OpenGL ES, una llamada de dibujo por fotograma |

En la prueba `4 qubits / 12 T`, el registro reporta **737,562 bytes** para la representación ingenua explícita frente a **1,072 bytes de carga lógica** en la representación reducida. Son mediciones de estructuras diferentes dentro del programa; **la carga lógica no equivale a la RAM total del proceso**.

En la investigación posterior, una factorización secuencial de bajo rango **no** comprimió los coeficientes del caso de 8 qubits: el vector de 256 coeficientes complejos dio una estimación de 680 escalares para esa factorización. Se conserva ese resultado negativo porque identifica una limitación técnica real.

La [revisión del 17 de octubre](results/architecture_limits_2026-10-17.md) añade
un banco aislado de escala, comprobaciones numéricas independientes y controles
de entradas. La creación y evolución de Reduced Exact T mejoran aproximadamente 2–2,25× en
los tres circuitos comparados; el crecimiento exponencial de sus componentes
persiste. En esta revisión, su referencia y reconstrucción usan `DenseCore64`
(double), y el reporte declara esa precisión y sus bytes. `DenseCore` conserva
float32 para los benchmarks y resultados históricos. Los tiempos, presupuestos,
fallos y límites de cada comparación quedan documentados en el informe.

**Precisión sobre el renderizado:** los 608 bytes que aparece en las capturas `COMPACT → GPU` representan el **estado lógico explícito instrumentado**, no toda la memoria que emplea Android, los shaders o la GPU. La mejora de FPS registrada provino del traslado del renderizado Canvas a OpenGL ES, el procesamiento por lotes y la reconstrucción local; no se atribuye a una aceleración cuántica física.

Para los detalles, resultados y limitaciones de cada medición:

- [Estado funcional y validaciones](TESTED_FUNCTIONAL_STATUS_2026-10-14.md).
- [Resultados de Exact T, Reduced T, magic y trazas](results/exact_t_decomposition_2026-10-14.md).
- [Benchmark nativo: Compact frente a Dense](results/native_benchmark_2026-10-13.md).
- [Capturas y mediciones visuales Android](results/visual_android_samsung_a16_5g_2026-10-14.md).
- [Teletransportación cuántica simulada](results/compact_quantum_teleportation_2026-10-14.md).

## Otras herramientas y organización

| Ruta | Contenido |
| --- | --- |
| `native/` | Núcleos C++17, ABI compartida, lenguajes y contratos. |
| `cosmic-android/` | App principal que consume fuentes compartidas de Graphics y laboratorios. |
| `graphics-android/`, `graphics-desktop/` | Observador gráfico, resolución, proyectos `.cosmic` y validaciones. |
| `solar-android/` | Explorador solar, catálogo, modelos, texturas y pruebas. |
| `pc-android/` | Intérprete x86/x64 propio, memoria, medios, CLI y app. |
| `app/`, `android-experiments/` | XLab, MicroLab, VM, Ecos y experimentos Android originales. |
| `src/`, `experiments/`, `tests/` | Referencia Python, Bridge/Transformer y experimentos. |
| `examples/`, `docs/`, `results/` | Programas, contratos, fuentes de datos y evidencia fechada. |
| `tools/`, `.github/workflows/` | Auditores, benchmarks, compilación y verificación automatizada. |

### Referencia Python y Transformer X

```sh
python -m pip install -r requirements.txt pytest
python hello_mundo_x.py
python -m pytest tests/test_quantum_core.py
python src/transformer_x.py
python experiments/train_file_v0_1.py ruta/a/un_archivo --epochs 20
```

El entrenamiento usa ventanas del archivo indicado y guarda `transformer_x_v0_1.pt`. El repositorio conserva el programa de entrenamiento; no incluye un checkpoint general ya entrenado. [Implementación y medición](experiments/train_file_v0_1.py).

### Red experimental

`python tools/mundox_relay.py` inicia el relay TCP de pruebas en el puerto 7777. Las actividades HOST/JOIN intercambian eventos para reconstruir el estado localmente. El protocolo actual carece de cifrado y autenticación; su uso documentado corresponde a una red de confianza. La verificación física completa entre dos dispositivos sigue pendiente. [Relay](tools/mundox_relay.py).

### Distribuciones y registros anteriores

La publicación [XLab 0.3.2, ZIP con APK firmado](https://github.com/guadalupejuan001-code/mundo-X/releases/download/v0.3.2/XLab-Objetos-0.3.2.zip) se conserva como **distribución histórica de XLab**. [Descarga alternativa](https://raw.githubusercontent.com/guadalupejuan001-code/mundo-X/6b206566022afa8b7b20b9e6dd3153ce8dad9182/dist/XLab-Objetos-0.3.2.zip), [validación de objetos y millón de instancias](results/xlab_millon_2026-10-18.md). Esa APK corresponde a su revisión publicada; los proyectos integrados se construyen con los destinos y workflows anteriores.

El titular usó «computasis» como analogía de homeostasis. El controlador añadido posteriormente no define la arquitectura original y fue retirado del código activo; algunos informes anteriores lo mencionan como parte de su historia. [Revisión de origen y medición](results/revision_origen_y_medicion_2026-10-18.md), [exámenes originales](results/native_exams_2026-10-18.md).

## Alcance y autoría

El proyecto conserva simulación compacta Clifford, experimentos no Clifford, lenguajes, aplicaciones y pruebas reproducibles. Quedan abiertos la escala de circuitos generales con T, la evaluación de Transformer X, la compatibilidad completa de PC y mediciones en dispositivos físicos. Los resultados disponibles no establecen una simulación eficiente de todos los estados arbitrarios ni una aceleración de hardware cuántico.

**Mundo X es un proyecto concebido y dirigido por el titular del repositorio.** El historial conserva implementaciones, revisiones y resultados, incluidos los negativos. [Estado histórico](PROJECT_STATUS_2026-10-14.md), [historial de `main`](https://github.com/guadalupejuan001-code/mundo-X/commits/main), [compilaciones y evidencia](https://github.com/guadalupejuan001-code/mundo-X/actions).

El código está visible y continúa en desarrollo. La raíz no incluye un archivo `LICENSE`; este README no concede una licencia adicional de reutilización.
