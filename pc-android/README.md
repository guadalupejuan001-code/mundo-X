# COSMIC X PC — motor x86/x64 propio para Android

## Mundo X Desktop 0.5.0 — entrada práctica

**La APK abre ahora Mundo X Desktop como una única aplicación con Inicio, Web,
Notas, Archivos, Quantum Workstation y COSMIC X PC.** El navegador usa WebView y
servicios de red de Android; las notas se guardan localmente y los documentos
se abren con el selector de archivos de Android. Quantum Workstation usa
directamente Ether/InfiniteCore, Inverse Ether Memory y XASM por medio de los
puentes nativos ya disponibles.

El emulador x86/x64 de este módulo continúa incluido y accesible desde Inicio;
no se sustituye ni se hace pasar por el sistema Android ni por hardware
cuántico. Manual y límites: [MUNDO_X_DESKTOP.md](MUNDO_X_DESKTOP.md) y
[QUANTUM_WORKSTATION.md](QUANTUM_WORKSTATION.md).

Este módulo desarrolla un emulador de PC propio dentro del proyecto COSMIC X. Interpreta instrucciones x86/x64 y conecta su CPU con RAM, vídeo de texto y framebuffer gráfico, teclado BIOS y lectura de disco. La versión 0.3.0 incorporó SHRD/SHLD, un dispositivo VBE lineal hasta 8K y un presentador GLES 3 propio; la versión **0.4.0** conserva la memoria por demanda y el arranque de prueba real → protegido → largo de 64 bits. **Esta versión todavía no ejecuta ni instala Windows completo ni juegos de PC.**

La versión **0.4.0** conecta **InfiniteCore como dispositivo del PC y el motor gráfico GLES canónico**. El huésped x86/x64, los comandos de la app y el observador cuántico acceden a una misma sesión. La pantalla de texto/framebuffer que escribe el huésped y el mundo del observador cuántico tienen sus propias vistas. Se conservan el intérprete, la RAM, los medios de arranque y el presentador VBE/GLES incorporado en 0.3.0.

La aplicación contiene el intérprete C++ `cosmic_pc`; no contiene QEMU, Limbo, Bochs, Wine ni otro motor de emulación. El código de este módulo se escribió para este proyecto. El presentador utiliza la clase de geometría y validación [RenderResolution](../graphics-android/app/src/main/java/com/mundox/graphics/RenderResolution.java) compartida con Graphics. El núcleo de 1.000 qubits no ejecuta ni acelera las instrucciones x86/x64: el estado de la CPU y la imagen que escribe el huésped conservan su propio significado.

## Quantum Workstation: plataforma de uso diario

La misma APK incluye ahora una entrada independiente **Mundo X Quantum** en el lanzador Android. Abre un entorno de trabajo con editor de programas `.xi`, terminal, archivos y sesiones `.mqx` persistentes, conectado directamente a **QuantumRuntime (Ether/InfiniteCore + Inverse Ether Memory)**. No requiere arrancar x86 ni inicializar sus 2 GiB de RAM lógica.

La interfaz ofrece **ejecutar, abrir, guardar y restaurar**. En modo XI, estado y memoria se actualizan transaccionalmente. En modo XASM, se ejecutan circuitos Compact/Reduced (incluidas puertas T) con estado nuevo por programa. No se mezclan silenciosamente las dos rutas. [Manual y límites](QUANTUM_WORKSTATION.md).

## Fusion Lab · física nuclear en tu PC

La tarjeta del núcleo ahora abre **Fusion Lab**: tabla de los 118 elementos, elección de isótopos y exploración de reacciones nucleares. Los canales conocidos calculan energía Q con masas tabuladas; las combinaciones arbitrarias permanecen hipótesis sin predicción de fusión. No usa MIL Core como simulador nuclear validado. [Manual de Fusion Lab](FUSION_LAB.md).

## Usar el núcleo y su motor gráfico desde el PC

La tarjeta **Núcleo cuántico · Motor gráfico** permite aplicar programas `.xi`, observar dos direcciones decimales y guardar/abrir el estado del núcleo. Se puede usar antes de seleccionar un medio de arranque. Las operaciones se serializan con los pasos de la CPU en el hilo de trabajo de la app.

Este programa prepara un par Bell entre direcciones alejadas y consulta correlaciones reales:

```text
h 0
cx 0 1000000000
expect X:0 X:1000000000
expect Y:0 Y:1000000000
stats
```

En un fondo cero, sus resultados son `XX=1`, `YY=-1`, `ZZ=1` y probabilidad Z individual `0.5`. La tarjeta muestra esas correlaciones junto con ZI/IZ, qubits activos, componentes, revisión y época. Las direcciones viajan como texto ASCII decimal; admiten valores superiores a 64 bits, incluidos `10^100`, dentro del presupuesto de dígitos. La distancia entre direcciones no asigna RAM intermedia.

El visor usa los shaders, `QuantumRenderer` y el puente `mx_graphics` del motor gráfico existente. Su propietario nativo retiene la misma `InfiniteSession` del PC. Las consultas para dibujar devuelven valores, revisión y época bajo una sola operación de sesión. No se exporta/importa el estado para refrescar cada imagen. La cámara y **Acoplamiento: sí/no** cambian la visualización; el editor cambia el estado del núcleo. El visor solicita imágenes al cambiar el estado, sin animación continua por defecto.

**Prueba núcleo x86** y **Prueba núcleo x64** cargan otros programas de arranque propios. Su primera etapa lee la segunda por BIOS; el huésped envía el programa por `OUT`, comprueba presencia/error con `IN` y decide qué texto escribir. La variante x64 realiza además su transición real → protegido → largo. **Prueba Z del huésped** prepara Bell, aplica Z desde el huésped y verifica `XX=-1`, `YY=1`, `ZZ=1`; con acoplamiento, el cambio llega al mundo del motor gráfico.

**Fondo cero/Bell** reinicia solo el núcleo y conserva la CPU, RAM, medio montado y canal de respuesta del huésped. **Reiniciar** y cargar un arranque válido restablecen el PC y su dispositivo a fondo cero. Una imagen inválida se valida antes de sustituir la máquina. Pausar y girar el teléfono conservan la sesión actual mediante el ciclo de vida de la Activity.

**Guardar núcleo** exporta un archivo `.mxi` con el estado exacto MXI1/MXI2, las reglas Ether presentes y el generador aleatorio; **Abrir núcleo** lo valida por completo antes de sustituir la sesión. Estos archivos guardan el núcleo, no una imagen de RAM/disco del PC ni el mundo gráfico completo. El selector Android concede acceso al archivo elegido. Los errores de lectura, formato o presupuesto conservan el estado previo.

### Contrato del dispositivo InfiniteCore

El modelo dispone de dos puertos de 16 bits, accedidos con instrucciones de **un byte**, por ejemplo `IN AL,DX` y `OUT DX,AL`:

| Puerto | Lectura | Escritura |
| --- | --- | --- |
| `0x5850` | Siguiente byte ASCII de respuesta; `0` si está vacía | Añade un byte a la petición |
| `0x5851` | Estado: bit 7 presente, bit 0 respuesta disponible, bit 1 error, bit 2 desbordamiento | `1`: vaciar canal; `2`: ejecutar petición acumulada |

El huésped vacía el canal, envía los bytes del programa y escribe `2` al puerto de control. La ejecución es síncrona y atómica por petición completa, incluidas aserciones, medidas y RNG. Las consultas producen líneas JSON; puertas y aserciones válidas pueden no producir texto. Una respuesta de error contiene `{"ok":false,"error":"..."}`. El huésped debe consultar el bit de disponibilidad antes de leer. Enviar otra petición sustituye la respuesta anterior. Consultar el panel o ejecutar un comando de la app no drena ni reemplaza el canal del huésped.

Se acepta la gramática del núcleo: `h/s/x/y/z`, `cx/cz`, `expect`, `probz`, `measure`, `postselect`, `seed`, `compact`, `stats`, `begin/end` y `assert expect/probz`. `background zero|bell`, como primer comando de una petición, reinicia explícitamente su fondo. No se admiten NUL, direcciones no decimales ni comandos arbitrarios de anfitrión. Un error de programa, un ancho de puerto incorrecto o un desbordamiento se informa en el dispositivo; no se convierte en un fallo de CPU. Tras desbordamiento hay que vaciar el canal: nunca se ejecuta el prefijo truncado.

### Ether Memory v0.2 · ventana x86 directa

> **Compatibilidad/experimento histórico.** Esta ventana permanece disponible
> para el PC, pero ya no define la arquitectura principal de memoria de la
> simulación cuántica. La ruta cuántica pura usa
> [Inverse Ether Memory](../docs/inverse-ether-memory.md) y no compila PC,
> SparseRam, VBE ni GLES.


El mismo lenguaje Infinite acepta `qload8 DIRECCION` y `qstore8 DIRECCION VALOR`.
Un byte lógico se representa mediante ocho qubits consecutivos
`q(8a)..q(8a+7)`. `qload8` no mide: si un bit no tiene un valor Z determinista,
rechaza la lectura. `qstore8` tampoco colapsa estados cuánticos; modifica
únicamente bytes clásicos y aplica los X necesarios como un batch atómico.

Además de los puertos `0x5850/0x5851`, `MemoryFabric` reserva **256 bytes de
memoria física x86 directa** en `0x47FFF000..0x47FFF0FF`. Esa zona está dentro
de la cola ya reservada de la apertura VBE de 128 MiB y empieza 1.507.328 bytes
después del final del framebuffer 8K máximo, por lo que no pisa sus píxeles.
El byte físico `0x47FFF000+n` observa el byte Ether
`1000000000000+n`. Una carga x86 devuelve exactamente el byte clásico; si el
estado correspondiente es no determinista en Z, la carga falla en vez de medirlo
silenciosamente. Una escritura x86 usa la misma sesión InfiniteCore que el panel,
el dispositivo por puertos y el visor.

Los **2 GiB de RAM general siguen respaldados por SparseRam**. La ventana prueba
que una carga/almacenamiento x86 puede usar Ether como autoridad de estado sin
convertir toda la RAM del Galaxy A16 en qRAM física. El presupuesto de SparseRam
y los presupuestos de InfiniteCore siguen separados. [Contrato v0.1](../docs/ether-memory-v0-1.md)
y [MemoryFabric v0.2](../docs/ether-memory-v0-2.md).

Presupuestos del perfil PC:

| Recurso | Límite |
| --- | ---: |
| Petición huésped/app | 65.536 bytes ASCII |
| Qubits activos | 4096 |
| Qubits por componente | 1024 |
| Payload del tableau | 16 MiB |
| Dígitos por dirección de entrada | 4096 |
| Snapshot MXI1/MXI2 | 16 MiB |
| Vista del último resultado en la app | 8192 bytes, con aviso si se recorta |

Estos presupuestos son independientes del respaldo de RAM x86. El tableau, las direcciones, el transporte, el visor y Android añaden consumo al proceso, medido por PSS. El dominio formal de direcciones no implica recursos físicos infinitos, ejecución de Windows ni mejoras de FPS acreditadas.

## Ejecutar la prueba en Android

1. Instalar el APK `COSMIC-X-PC.apk`. La compilación de desarrollo admite Android 8.0 o posterior y GLES 3 en ARM64 y x86_64.
2. Pulsar **Arranque de prueba** y después **Ejecutar**.
3. El programa invitado calcula `0x1234 + 0x0102`, escribe y comprueba la RAM, solicita un sector por `INT 13h` y verifica su contenido. Solo entonces imprime `ALU + RAM + lectura de disco: OK`.
4. Escribir una tecla y pulsar **Enviar**. El invitado recibe esa tecla por `INT 16h`, la imprime por `INT 10h` y ejecuta `HLT`.

**Paso** ejecuta un paso de CPU. **Pausa** detiene el bucle entre pasos; **Reiniciar** vuelve a cargar el arranque. En modo texto, la pantalla corresponde a 80 × 25 pares de carácter/atributo de la memoria del invitado en `0xB8000`, presentados con GLES. El diagnóstico aparece junto al estado de ejecución y permanece visible si falla la carga de un medio.

### Prueba de 64 bits

Pulsar **Prueba x64** y después **Ejecutar**. El programa invitado configura la GDT, entra en modo protegido, configura CR3/CR4/EFER, activa paginación y salta a un segmento de código de 64 bits. Las instrucciones del invitado calculan y conservan `0x2345678922222223` en RAX, R9 y RAM física `0x9000`. La interfaz debe mostrar `long64` y la detención por `HLT`. **Reiniciar** vuelve al modo real y permite repetir toda la transición.

Esta prueba ejecuta instrucciones reales del invitado con el intérprete propio. No carga Windows ni representa una prueba de compatibilidad con un lanzador o juego.

### Prueba gráfica: fuente huésped y salida 8K

1. Pulsar **Prueba gráfica** y elegir **1280 × 720**, **3840 × 2160 (4K)** o **7680 × 4320 (8K)**.
2. Pulsar **Ejecutar**. El programa huésped consulta VBE, selecciona un framebuffer lineal, entra en modo protegido, dibuja mediante instrucciones `REP STOSD` y comprueba sus propios píxeles antes de detenerse con `HLT`.
3. Los botones **Auto / 4K / 8K** seleccionan la resolución de salida del presentador; **Auto** adapta la salida a la superficie visible. Para comprobar una fuente y una salida 8K, se elige 8K en ambos lugares y se consultan sus dimensiones por separado.

| Medida de vídeo | Qué controla | Ejemplo 8K |
| --- | --- | --- |
| Fuente huésped | Dimensiones del framebuffer que programa y escribe la CPU emulada | 7680 × 4320 píxeles BGRX de 32 bits |
| Salida de renderizado | Framebuffer GLES al que el presentador lleva esa fuente, con su relación de aspecto conservada | 7680 × 4320, o 4320 × 7680 según orientación, si GL lo admite |
| Superficie visible de Android | Tamaño de la vista dentro de la app que finalmente muestra la salida | Depende de la ventana y del teléfono; no es una medición del panel físico completo |

El botón de salida 8K también puede presentar texto o una fuente 720p: en ese caso adapta esa fuente a una salida mayor. **No modifica el modo de vídeo del huésped ni la resolución física del teléfono.** La interfaz informa del modo solicitado, del realmente asignado, de los límites GL y de cualquier error. Si la GPU o el presupuesto rechazan una asignación, se conserva la última salida completa; al recuperar un contexto sin salida válida se intenta una salida adaptativa y se mantiene visible el motivo del rechazo.

El dispositivo de [vídeo](native/pc_video.hpp) implementa las funciones VBE `4F00`–`4F03`, ocho modos lineales entre 640 × 480 y 7680 × 4320 y una apertura de 128 MiB en `0x40000000`. El búfer usa BGRX8888, con X reservado; el puente convierte las filas que presenta a RGBA opaco. El modo texto original sigue disponible. Esta interfaz no anuncia VGA por bancos, cambio de páginas de vídeo ni un procesador 3D virtual.

La [demostración gráfica](native/graphics_demo.hpp) escribe bandas y marcadores mediante el huésped, incluidos los extremos de la imagen. Toca aproximadamente el 6,6 % del espacio de una fuente 8K; el resto permanece inicialmente a cero. Por ello prueba direccionamiento y presentación de un framebuffer completo con una carga dispersa, sin representar un benchmark de un juego o de rellenar todos los píxeles en cada cuadro.

## Perfil de memoria para un presupuesto de 2 GB

La app abre **2048 MiB de RAM virtual** con un **límite de 1024 MiB para las páginas del invitado**. El espacio lógico no se reserva entero: una página ausente se lee como ceros y recibe respaldo cuando se necesita. La tabla de dos niveles también se asigna parcialmente. Se distinguen tres medidas en pantalla:

| Medida | Qué significa |
| --- | --- |
| RAM virtual | Direcciones de RAM que el PC presenta al invitado: 2048 MiB |
| RAM usada por invitado / límite | Respaldo de páginas de 4 KiB realmente asignadas, con límite de 1024 MiB |
| PSS de la app | Muestra de consumo del proceso Android, tomada en el hilo de trabajo y actualizada como máximo una vez por segundo |

El límite del invitado deja margen para el código, la interfaz, las tablas, el asignador y Android. **No garantiza un límite total del proceso ni convierte 1 GiB de respaldo en 2 GiB de datos activos.** Si el invitado requiere más páginas de las permitidas, el motor se detiene con un fallo explícito de presupuesto. No hay intercambio a disco ni compresión general de páginas. La RAM se conserva durante las pausas y se libera al reiniciar o destruir la máquina.

Las escrituras de bloques preparan el respaldo antes de cambiar datos. El rollback de la CPU restaura bytes sin asignar memoria adicional. Las páginas no se liberan automáticamente durante una instrucción; la liberación de páginas vacías, cuando se solicita desde código, debe hacerse fuera de una transacción.

### Presupuestos separados de vídeo

| Reserva o medida | Límite / alcance |
| --- | --- |
| Páginas de RAM del huésped | Hasta 1024 MiB; incluye las páginas realmente escritas de su región de vídeo |
| Apertura lógica de vídeo | 128 MiB en el mapa del PC; no se reserva todo ese espacio al seleccionar un modo |
| Instantáneas nativas del puente JNI | Presupuesto global de 256 MiB para copias inmutables retenidas por las máquinas y sus lectores |
| Texturas GLES simultáneas | Presupuesto de 512 MiB para fuente, reemplazo, salida y atlas; se valida el pico antes de asignar |
| Transferencia de filas a GL | Buffer directo de hasta 2 MiB; no requiere otro bitmap RGBA 8K completo en Java |
| PSS | Medición aparte del proceso Android; incluye categorías de memoria que no están limitadas por los dos presupuestos gráficos |

Una imagen de 7680 × 4320 × 4 ocupa **132.710.400 bytes (126,56 MiB)** cuando se materializa completa. Cada instantánea nativa de esa imagen consume esa cantidad aunque la RAM original tenga páginas dispersas. La carga a GL usa franjas de filas, mientras que las texturas de fuente y de salida sí tienen sus dimensiones completas. Los límites de 512/256 MiB son presupuestos del código, no memoria permanentemente reservada ni una garantía sobre el total de la app; se suman al respaldo necesario del huésped y a las demás reservas de Android/controlador.

Una instantánea adquirida conserva su contenido aunque se reinicie o destruya la máquina que la produjo. Se libera después de que todos sus lectores devuelvan la adquisición. Si no queda presupuesto para otra captura o textura, se informa el fallo y se conserva el último cuadro completo disponible. [Puente JNI](app/src/main/cpp/pc_jni.cpp), [presentador GLES](app/src/main/java/com/cosmicx/pc/PcVideoRenderer.java).

### Medición reproducible

`bash tools/benchmark-memory.sh` compara dos procesos Linux independientes: un vector denso inicializado de 2 GiB y la RAM nueva con el mismo espacio lógico y los mismos 8 MiB de datos dispersos. Verifica todos los bytes leídos y un hash FNV-1a idéntico: `75186b12a481b7df`.

Una ejecución en el entorno de desarrollo x86_64 produjo:

| Métrica | Vector denso | RAM por demanda |
| --- | ---: | ---: |
| Espacio lógico | 2048 MiB | 2048 MiB |
| Datos de la carga de prueba | 8 MiB | 8 MiB |
| Respaldo de páginas | 2048 MiB | 8 MiB |
| RSS/pico observado del proceso | 2049,75 MiB | 13,79 MiB |
| Escritura de los datos después de inicializar | 3,90 ms | 14,67 ms |

La asignación por demanda ahorra memoria en esta carga y añade trabajo para localizar/asignar páginas. **No es una medición de FPS, de Windows ni del Samsung A16.** Una carga que escriba más páginas consume más respaldo. El workflow conserva los valores completos de su propia ejecución en `memory-benchmark.json`; los tiempos y RSS varían entre máquinas y ejecuciones.

## Abrir un medio propio

**Abrir ISO/disco** utiliza el selector de archivos de Android y abre el medio en lectura. El descriptor nativo se conserva sin copiar la ISO completa a RAM ni a otra carpeta. Se exige un archivo local de tamaño conocido con acceso aleatorio. **Reabrir última imagen** utiliza el permiso persistente cuando el proveedor lo concede.

| Medio | Implementado |
| --- | --- |
| Disco raw `.img` | Sector inicial de 512 bytes con firma `55 AA`, carga en `0000:7C00`, unidad BIOS `80h`; lecturas CHS y LBA |
| ISO de arranque BIOS | Catálogo El Torito, entrada predeterminada x86 sin emulación, unidad `E0h`; lectura extendida con sectores de 2048 bytes |
| UEFI o entradas alternativas del catálogo | Pendiente; se informa el formato no soportado |
| QCOW2, VHD/VHDX y escritura de disco | Pendiente |

Reconocer un catálogo y ejecutar sus primeros bytes **no acredita que el sistema operativo de esa ISO sea compatible**. Una operación no implementada detiene la CPU con dirección `CS:IP`, opcode, bytes y motivo. **Copiar diagnóstico** permite traer ese estado al desarrollo.

### Corrección de `0F AD` al abrir una ISO

La CPU de la versión anterior se detenía al encontrar SHRD `0F AD`, con el motivo `two-byte opcode is not implemented`. La versión 0.3.0 incorpora **SHRD `0F AC/AD` y SHLD `0F A4/A5`** en 16, 32 y 64 bits; acepta cuenta inmediata o `CL`, destinos de registro/memoria y los prefijos correspondientes.

La regresión mínima sitúa `0F AD D8` en `07c0:039c`: con `AX=1234h`, `BX=5678h` y `CL=4`, obtiene `AX=8123h`, conserva BX y avanza a `039fh`. Las pruebas de CPU comprueban máscaras de cuenta, banderas definidas, operandos compartidos, límites de memoria y rollback. El oráculo nativo compara **38.912 vectores de SHLD/SHRD** con el x86_64 del anfitrión; la política del emulador para cuentas de palabra arquitectónicamente indefinidas se prueba aparte.

Esta corrección permite ejecutar esa familia de instrucciones. La ISO concreta del usuario no forma parte del repositorio ni se declara arrancada por esta regresión: un cargador puede alcanzar después otra instrucción o dispositivo todavía no implementado.

## Componentes

| Archivo | Función |
| --- | --- |
| `native/x86_cpu.hpp` | Intérprete x86/x64, descriptores de ring 0, traducción de páginas y diagnóstico de fallos |
| `native/sparse_ram.hpp` | RAM por páginas de 4 KiB, presupuesto explícito y métricas de respaldo/metadatos |
| `native/boot_media.hpp` | Lectura acotada de medios raw e ISO, validación del catálogo y plan de arranque |
| `native/pc_machine.hpp` | Memoria de PC, dispositivo de vídeo y servicios BIOS de teclado, disco y memoria |
| `native/pc_video.hpp` | Framebuffer lineal propio, subconjunto VBE, modos, regiones de copia y conversión BGRX/RGBA |
| `native/quantum_device.hpp` | Dispositivo IN/OUT acotado sobre la sesión InfiniteCore compartida |
| `native/quantum_guest.hpp` | Arranques propios que usan el dispositivo desde x86/x64 y comprueban sus resultados |
| `native/demo_boot.hpp` | Generador del programa x86 propio y su disco de dos sectores |
| `native/long_mode_guest.hpp` | Programa de arranque propio que configura y comprueba la transición a 64 bits |
| `native/graphics_demo.hpp` | Programa huésped propio que configura VBE, escribe píxeles y verifica el resultado |
| `native/pc_cli.cpp` | Ejecución del mismo motor desde terminal |
| `app/src/main/cpp/pc_jni.cpp` | Enlace Android/JNI, acceso al medio mediante `pread` e instantáneas de vídeo con propiedad explícita |
| `app/src/main/java/com/cosmicx/pc/PcVideoRenderer.java` | Presentador GLES 3 de texto/VRAM, subida por filas y presupuesto de texturas |
| `app/src/main/java/com/cosmicx/pc/PcVideoView.java` | Contexto gráfico, cola GL y recuperación de la superficie |
| `app/src/main/java/com/cosmicx/pc/` | Actividad Android, ejecución en un hilo de trabajo, controles y diagnóstico |

El intérprete incluye registros de 8/16/32/64 bits, R8..R15, prefijos `66`, `67` y REX, direccionamiento ModRM/SIB, operaciones aritméticas y lógicas, pila, llamadas, saltos y operaciones de cadenas. Los servicios BIOS se invocan desde modo real. Las pruebas describen el subconjunto implementado; esto no implica cobertura completa de la ISA x86.

La paginación implementada recorre cuatro niveles, con páginas de 4 KiB y 2 MiB, direcciones canónicas, permisos de escritura de supervisor y NX. El modo protegido/largo se limita a ring 0. Los fallos de protección/página se notifican al anfitrión con diagnóstico; todavía no se entregan a un manejador del sistema operativo invitado.

Los campos JSON `regs_hex` y `rip_hex` conservan exactamente los 64 bits. Deben usarse para diagnóstico: consumidores que conviertan los campos numéricos históricos `regs`/`ip` a enteros de 32 bits o números de doble precisión pueden perder bits.

Cada operación REP procesa un elemento por paso para mantener acotada la ejecución. El campo JSON histórico `instructions` cuenta **pasos CPU**, no instrucciones retiradas por hardware. Los fallos revierten registros y escrituras efectuadas por los métodos de la CPU, pero no prometen revertir efectos externos de un callback.

## Límite actual para Windows

La transición de arranque probada cubre un subconjunto de la CPU, y las funciones no implementadas producen un diagnóstico. Todavía faltan:

1. Privilegios de usuario, entrega de excepciones/interrupciones y cambio de contexto operativo.
2. Más ISA y funciones del procesador que Windows y los juegos requieren; incluyendo SIMD y un modelo completo de capacidades de CPU. No está implementada la paginación heredada de 32 bits ni la paginación de cinco niveles.
3. Hardware de PC operativo: controladores de interrupciones y temporizador, almacenamiento escribible con controlador, buses y dispositivos que descubra Windows, y una GPU/controladores compatibles con sus interfaces gráficas. El framebuffer VBE nuevo cubre salida lineal básica; no implementa Direct3D ni aceleración 3D del huésped.
4. Pruebas de arranque e instalación con una ISO oficial concreta y medición en el teléfono real.

Los registros de máscara PIC, algunos registros CMOS, A20 y la salida serie son servicios parciales; no equivalen a implementar esos dispositivos completos. No hay entrega de IRQ, x87/SSE, ACPI, APIC, JIT, red ni audio en esta versión. El indicador `windows_ready` permanece en `false`.

## Compilar y verificar

### Motor nativo

Desde este directorio, con un compilador C++17:

```sh
bash tools/test-native.sh
```

El script compila **diez suites**: RAM, CPU, transición de modos, oráculo nativo, medios, PC, ISO, memoria, vídeo y dispositivo InfiniteCore. `pc_video_test` ejecuta programas huéspedes 720p y 8K y comprueba VBE, píxeles, copias y presupuestos. `pc_quantum_test` verifica sesión, puertos, atomicidad y huéspedes del núcleo. Después ejecuta los arranques de consola x86/x64 y los dos del dispositivo; guarda sus imágenes en `build/native/`.

También admite CMake:

```sh
cmake -S . -B build/cmake
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

Uso de la CLI:

```sh
build/native/cosmic_pc_cli --demo
build/native/cosmic_pc_cli --demo64 --ram-mib 2048 --resident-mib 1024
build/native/cosmic_pc_cli --demo-quantum
build/native/cosmic_pc_cli --demo-quantum64
build/native/cosmic_pc_cli --demo --quantum programa.xi
build/native/cosmic_pc_cli --image /ruta/al/medio.iso --max-instructions 1000000
```

### Android

Herramientas declaradas: JDK 17, Gradle 8.9, Android Gradle Plugin 8.7.3, SDK 35, NDK 27.2.12479018 y CMake 3.22.1. No hay Gradle Wrapper incluido: utilizar Gradle 8.9 instalado o el workflow del repositorio.

```sh
gradle --no-daemon :app:assembleDebug :app:assembleDebugAndroidTest
```

El workflow [.github/workflows/own-pc-android.yml](../.github/workflows/own-pc-android.yml) compila las dos ABI, verifica firma, identidad 0.4.0, bibliotecas y clases de ambos puentes gráficos, y está configurado para ejecutar **29 pruebas instrumentales** con ese APK: 12 heredadas, ocho de ISO/vídeo y nueve de InfiniteCore. Se ejecuta con Android 15 y un AVD de 2048 MiB; ese tamaño pertenece al dispositivo virtual de pruebas. La app mantiene el perfil de huésped 2048/1024 MiB y comprueba su propia muestra de PSS. `libcosmic_pc.so` enlaza con una única `libmx_graphics.so`, que contiene los puentes canónicos; ambas usan `libc++_shared.so`.

El artefacto de CI conserva APK, SHA-256, commit, hashes del puente y de `RenderResolution.java`, logs, `pc-tests.json`, `pc-x64-state.json`, `pc-video-state.json` y las capturas `pc-screen.png`/`pc-video-screen.png`. La firma es de desarrollo. La configuración y el inventario de tests no declaran por sí mismos un resultado aprobado: el resultado válido es el de [Actions](https://github.com/guadalupejuan001-code/mundo-X/actions/workflows/own-pc-android.yml) para el commit correspondiente.

## Qué prueban los tests

- La ALU de 8 bits se compara exhaustivamente con fórmulas de resultado y flags independientes para ADD, ADC, SUB, SBB y CMP.
- En un anfitrión Linux x86_64, las mismas cinco operaciones de 64 bits y el producto IMUL se comparan con instrucciones nativas del procesador y sus flags definidos. SHLD/SHRD se contrastan en 16/32/64 bits para ambas codificaciones de cuenta. Ese oráculo se omite explícitamente en otras arquitecturas y no forma parte de la app.
- Se verifican direccionamiento, saltos, pila, cadenas, condiciones de fallo y restauración del estado de CPU.
- El lector rechaza medios truncados, firmas y catálogos inválidos, tamaños fuera de rango y modos no implementados.
- Un invitado de la ISO de prueba consulta EDD, lee un sector distinto al de arranque y comprueba sus extremos. Datos correctos producen `I`; datos alterados producen `F` desde el propio invitado.
- La integración prueba teclado, reinicio, memoria, acceso al archivo después de cerrar el descriptor Java y la actividad operando la CPU real.
- La memoria por demanda se compara con datos de referencia y se prueba en límites, reinicios y fallos de presupuesto. El perfil de 2 GiB también se verifica a través de JNI y JSON.
- El invitado de 64 bits ejecuta la transición de modos, conserva resultados por encima de 32 bits y repite el proceso tras un reinicio. La Activity presenta una muestra numérica de PSS.
- Los invitados del núcleo ejercitan ramas de éxito y fallo en modo real y largo. Se verifica rollback completo de comandos, FIFO acotada, errores sin fallo de CPU, direcciones largas y conservación de arranque/RAM/teclado.
- La instrumentación exige que un Z del huésped cambie píxeles del viewport GLES acoplado, que la vista desacoplada permanezca idéntica y que un Z inverso desde la app restaure los píxeles. También verifica que el propietario gráfico comparte la sesión y puede cerrarse independientemente del PC.
- El dispositivo de vídeo comprueba tablas y modos VBE, píxeles escritos por el huésped, conversión de color, copias acotadas, modo texto y rechazos de presupuesto. Android añade verificación del enlace JNI/GLES, resolución real de fuente/salida, lectura de píxeles e instantáneas conservadas durante cambios de máquina.

Durante el desarrollo de 0.3.0, GCC 13.3/C++17 aprobó **5.902.496 comprobaciones CPU**, **304 aserciones de modo largo** y los oráculos de **7.872 vectores ALU + 38.912 SHLD/SHRD**. Las suites CPU y modo largo también aprobaron UBSan. Estas mediciones locales corresponden a la CPU; la ejecución Android de la revisión completa se documenta mediante sus artefactos de CI.

El número de comprobaciones no representa compatibilidad con Windows. Los logs de cada commit son la evidencia de su compilación y ejecución; la validación en un Android virtual no mide el rendimiento en el Samsung físico.

## Referencias de arquitectura

- [Intel Software Developer's Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html): formatos y semántica de instrucciones x86.
- [El Torito Bootable CD-ROM Format Specification](https://pdos.csail.mit.edu/6.828/2005/readings/boot-cdrom.pdf): estructuras del arranque desde CD.
- [Microsoft Oscdimg](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/oscdimg-command-line-options): distinción entre entradas de arranque BIOS y UEFI.

Estas referencias describen interfaces. No se incorporó código fuente de otros emuladores.

## Referencias de RAM y objetivo de juegos

Consultadas el 19 de octubre de 2026:

- [Microsoft: requisitos de Windows 10](https://support.microsoft.com/en-us/windows/windows-10-system-requirements-6d4e9a79-66bf-7950-467c-795cf0386715): 2 GB de RAM mínima para 64 bits.
- [Epic: requisitos de Fortnite para PC](https://www.epicgames.com/help/c-34254770/c-5719357839387/a16548002?lang=es-ES): 8 GB mínimos y 16 GB recomendados.
- [Epic Store: Alan Wake 2](https://store.epicgames.com/p/alan-wake-2?lang=en-US): 16 GB mínimos.
- [Android: Debug.MemoryInfo](https://developer.android.com/reference/android/os/Debug.MemoryInfo): lectura de PSS del proceso en kB.

Estos requisitos de juegos describen el PC completo; no se suman mecánicamente al mínimo de Windows. La optimización medida no acredita que un presupuesto total de 2 GB para la app cumpla esos requisitos ni sustituye una GPU y sus controladores.
