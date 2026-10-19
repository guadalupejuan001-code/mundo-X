# COSMIC X PC — motor x86/x64 propio para Android

Este módulo desarrolla un emulador de PC propio dentro del proyecto COSMIC X. Interpreta instrucciones x86/x64 y conecta su CPU con RAM, vídeo de texto, teclado BIOS y lectura de disco. La versión 0.2 añade memoria por demanda y un arranque de prueba que pasa de modo real a protegido y a modo largo de 64 bits. **Esta versión todavía no ejecuta ni instala Windows completo ni juegos de PC.**

La aplicación contiene el nuevo intérprete C++ `cosmic_pc`; no contiene QEMU, Limbo, Bochs, Wine ni otro motor de emulación. El código de este módulo se escribió para este proyecto. El núcleo cuántico y el motor gráfico existentes no se usan como sustitutos de una CPU x86, y esta versión no acredita aceleración cuántica.

## Ejecutar la prueba en Android

1. Instalar el APK `COSMIC-X-PC.apk`. La compilación de desarrollo admite Android 8.0 o posterior en ARM64 y x86_64.
2. Pulsar **Arranque de prueba** y después **Ejecutar**.
3. El programa invitado calcula `0x1234 + 0x0102`, escribe y comprueba la RAM, solicita un sector por `INT 13h` y verifica su contenido. Solo entonces imprime `ALU + RAM + lectura de disco: OK`.
4. Escribir una tecla y pulsar **Enviar**. El invitado recibe esa tecla por `INT 16h`, la imprime por `INT 10h` y ejecuta `HLT`.

**Paso** ejecuta un paso de CPU. **Pausa** detiene el bucle entre pasos; **Reiniciar** vuelve a cargar el arranque. La pantalla corresponde a los caracteres de la memoria de vídeo del invitado en `0xB8000`. No es una reproducción de un escritorio de Windows.

### Prueba de 64 bits

Pulsar **Prueba x64** y después **Ejecutar**. El programa invitado configura la GDT, entra en modo protegido, configura CR3/CR4/EFER, activa paginación y salta a un segmento de código de 64 bits. Las instrucciones del invitado calculan y conservan `0x2345678922222223` en RAX, R9 y RAM física `0x9000`. La interfaz debe mostrar `long64` y la detención por `HLT`. **Reiniciar** vuelve al modo real y permite repetir toda la transición.

Esta prueba ejecuta instrucciones reales del invitado con el intérprete propio. No carga Windows ni representa una prueba de compatibilidad con un lanzador o juego.

## Perfil de memoria para un presupuesto de 2 GB

La app abre **2048 MiB de RAM virtual** con un **límite de 1024 MiB para las páginas del invitado**. El espacio lógico no se reserva entero: una página ausente se lee como ceros y recibe respaldo cuando se necesita. La tabla de dos niveles también se asigna parcialmente. Se distinguen tres medidas en pantalla:

| Medida | Qué significa |
| --- | --- |
| RAM virtual | Direcciones de RAM que el PC presenta al invitado: 2048 MiB |
| RAM usada por invitado / límite | Respaldo de páginas de 4 KiB realmente asignadas, con límite de 1024 MiB |
| PSS de la app | Muestra de consumo del proceso Android, tomada en el hilo de trabajo y actualizada como máximo una vez por segundo |

El límite del invitado deja margen para el código, la interfaz, las tablas, el asignador y Android. **No garantiza un límite total del proceso ni convierte 1 GiB de respaldo en 2 GiB de datos activos.** Si el invitado requiere más páginas de las permitidas, el motor se detiene con un fallo explícito de presupuesto. No hay intercambio a disco ni compresión general de páginas. La RAM se conserva durante las pausas y se libera al reiniciar o destruir la máquina.

Las escrituras de bloques preparan el respaldo antes de cambiar datos. El rollback de la CPU restaura bytes sin asignar memoria adicional. Las páginas no se liberan automáticamente durante una instrucción; la liberación de páginas vacías, cuando se solicita desde código, debe hacerse fuera de una transacción.

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

## Componentes

| Archivo | Función |
| --- | --- |
| `native/x86_cpu.hpp` | Intérprete x86/x64, descriptores de ring 0, traducción de páginas y diagnóstico de fallos |
| `native/sparse_ram.hpp` | RAM por páginas de 4 KiB, presupuesto explícito y métricas de respaldo/metadatos |
| `native/boot_media.hpp` | Lectura acotada de medios raw e ISO, validación del catálogo y plan de arranque |
| `native/pc_machine.hpp` | Memoria de PC y servicios BIOS de vídeo, teclado, disco y memoria |
| `native/demo_boot.hpp` | Generador del programa x86 propio y su disco de dos sectores |
| `native/long_mode_guest.hpp` | Programa de arranque propio que configura y comprueba la transición a 64 bits |
| `native/pc_cli.cpp` | Ejecución del mismo motor desde terminal |
| `app/src/main/cpp/pc_jni.cpp` | Enlace Android/JNI y acceso al medio mediante `pread` |
| `app/src/main/java/com/cosmicx/pc/` | Actividad Android, ejecución en un hilo de trabajo y terminal |

El intérprete incluye registros de 8/16/32/64 bits, R8..R15, prefijos `66`, `67` y REX, direccionamiento ModRM/SIB, operaciones aritméticas y lógicas, pila, llamadas, saltos y operaciones de cadenas. Los servicios BIOS se invocan desde modo real. Las pruebas describen el subconjunto implementado; esto no implica cobertura completa de la ISA x86.

La paginación implementada recorre cuatro niveles, con páginas de 4 KiB y 2 MiB, direcciones canónicas, permisos de escritura de supervisor y NX. El modo protegido/largo se limita a ring 0. Los fallos de protección/página se notifican al anfitrión con diagnóstico; todavía no se entregan a un manejador del sistema operativo invitado.

Los campos JSON `regs_hex` y `rip_hex` conservan exactamente los 64 bits. Deben usarse para diagnóstico: consumidores que conviertan los campos numéricos históricos `regs`/`ip` a enteros de 32 bits o números de doble precisión pueden perder bits.

Cada operación REP procesa un elemento por paso para mantener acotada la ejecución. El campo JSON histórico `instructions` cuenta **pasos CPU**, no instrucciones retiradas por hardware. Los fallos revierten registros y escrituras efectuadas por los métodos de la CPU, pero no prometen revertir efectos externos de un callback.

## Límite actual para Windows

La transición de arranque probada cubre un subconjunto de la CPU, y las funciones no implementadas producen un diagnóstico. Todavía faltan:

1. Privilegios de usuario, entrega de excepciones/interrupciones y cambio de contexto operativo.
2. Más ISA y funciones del procesador que Windows y los juegos requieren; incluyendo SIMD y un modelo completo de capacidades de CPU. No está implementada la paginación heredada de 32 bits ni la paginación de cinco niveles.
3. Hardware de PC operativo: controladores de interrupciones y temporizador, almacenamiento escribible con controlador, buses y dispositivos que descubra Windows, y vídeo gráfico.
4. Pruebas de arranque e instalación con una ISO oficial concreta y medición en el teléfono real.

Los registros de máscara PIC, algunos registros CMOS, A20 y la salida serie son servicios parciales; no equivalen a implementar esos dispositivos completos. No hay entrega de IRQ, x87/SSE, ACPI, APIC, JIT, red ni audio en esta versión. El indicador `windows_ready` permanece en `false`.

## Compilar y verificar

### Motor nativo

Desde este directorio, con un compilador C++17:

```sh
bash tools/test-native.sh
```

El script compila los tests de RAM, CPU, transición de modos, medios e integración de PC/ISO. Después ejecuta ambos arranques y guarda sus imágenes en `build/native/cosmic-x86-demo.img` y `build/native/cosmic-x64-demo.img`.

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
build/native/cosmic_pc_cli --image /ruta/al/medio.iso --max-instructions 1000000
```

### Android

Herramientas declaradas: JDK 17, Gradle 8.9, Android Gradle Plugin 8.7.3, SDK 35, NDK 27.2.12479018 y CMake 3.22.1. No hay Gradle Wrapper incluido: utilizar Gradle 8.9 instalado o el workflow del repositorio.

```sh
gradle --no-daemon :app:assembleDebug :app:assembleDebugAndroidTest
```

El workflow `.github/workflows/own-pc-android.yml` compila las dos ABI, verifica la firma y las bibliotecas empaquetadas, y ejecuta doce pruebas instrumentales con el APK construido. El dispositivo virtual oficial de Android se utiliza como infraestructura de pruebas: el motor que ejecuta el huésped x86/x64 dentro de la app es exclusivamente `cosmic_pc`.

El artefacto de CI contiene APK, SHA-256, commit, logs, resultado instrumental y captura de pantalla. La firma es de desarrollo; el workflow no publica una versión de distribución ni compra licencias de Windows.

## Qué prueban los tests

- La ALU de 8 bits se compara exhaustivamente con fórmulas de resultado y flags independientes para ADD, ADC, SUB, SBB y CMP.
- En un anfitrión Linux x86_64, las mismas cinco operaciones de 64 bits se comparan con instrucciones nativas del procesador y sus flags definidos. Ese oráculo se omite explícitamente en otras arquitecturas y no forma parte de la app.
- Se verifican direccionamiento, saltos, pila, cadenas, condiciones de fallo y restauración del estado de CPU.
- El lector rechaza medios truncados, firmas y catálogos inválidos, tamaños fuera de rango y modos no implementados.
- Un invitado de la ISO de prueba consulta EDD, lee un sector distinto al de arranque y comprueba sus extremos. Datos correctos producen `I`; datos alterados producen `F` desde el propio invitado.
- La integración prueba teclado, reinicio, memoria, acceso al archivo después de cerrar el descriptor Java y la actividad operando la CPU real.
- La memoria por demanda se compara con datos de referencia y se prueba en límites, reinicios y fallos de presupuesto. El perfil de 2 GiB también se verifica a través de JNI y JSON.
- El invitado de 64 bits ejecuta la transición de modos, conserva resultados por encima de 32 bits y repite el proceso tras un reinicio. La Activity presenta una muestra numérica de PSS.

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
