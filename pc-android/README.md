# COSMIC X PC — motor x86 propio para Android

Este módulo inicia un emulador de PC propio dentro del proyecto COSMIC X. Interpreta bytes x86, ejecuta un programa de arranque y conecta su CPU con RAM, vídeo de texto, teclado BIOS y lectura de disco. **Esta versión todavía no ejecuta ni instala Windows completo.**

La aplicación contiene el nuevo intérprete C++ `cosmic_pc`; no contiene QEMU, Limbo, Bochs, Wine ni otro motor de emulación. El código de este módulo se escribió para este proyecto. El núcleo cuántico y el motor gráfico existentes no se usan como sustitutos de una CPU x86, y esta versión no acredita aceleración cuántica.

## Ejecutar la prueba en Android

1. Instalar el APK `COSMIC-X-PC.apk`. La compilación de desarrollo admite Android 8.0 o posterior en ARM64 y x86_64.
2. Pulsar **Arranque de prueba** y después **Ejecutar**.
3. El programa invitado calcula `0x1234 + 0x0102`, escribe y comprueba la RAM, solicita un sector por `INT 13h` y verifica su contenido. Solo entonces imprime `ALU + RAM + lectura de disco: OK`.
4. Escribir una tecla y pulsar **Enviar**. El invitado recibe esa tecla por `INT 16h`, la imprime por `INT 10h` y ejecuta `HLT`.

**Paso** ejecuta un paso de CPU. **Pausa** detiene el bucle entre pasos; **Reiniciar** vuelve a cargar el arranque. La pantalla corresponde a los caracteres de la memoria de vídeo del invitado en `0xB8000`. No es una reproducción de un escritorio de Windows.

La interfaz reserva 16 MiB de RAM invitada para estas pruebas. Esa cantidad y el hardware actual no constituyen una configuración para Windows 10.

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
| `native/x86_cpu.hpp` | Decodificador e intérprete x86 en modo real, registros, ALU, segmentos, A20 y diagnóstico de fallos |
| `native/boot_media.hpp` | Lectura acotada de medios raw e ISO, validación del catálogo y plan de arranque |
| `native/pc_machine.hpp` | Memoria de PC y servicios BIOS de vídeo, teclado, disco y memoria |
| `native/demo_boot.hpp` | Generador del programa x86 propio y su disco de dos sectores |
| `native/pc_cli.cpp` | Ejecución del mismo motor desde terminal |
| `app/src/main/cpp/pc_jni.cpp` | Enlace Android/JNI y acceso al medio mediante `pread` |
| `app/src/main/java/com/cosmicx/pc/` | Actividad Android, ejecución en un hilo de trabajo y terminal |

El intérprete incluye registros de 8/16/32 bits, prefijos `66` y `67`, ModRM16/ModRM32 con SIB, operaciones aritméticas y lógicas, pila, llamadas, saltos, interrupciones de software, cadenas y E/S mediante callbacks. Los grupos exactos y sus casos se encuentran junto al código en `native/tests/x86_cpu_test.cpp`; esto no implica cobertura completa de la ISA x86.

Cada operación REP procesa un elemento por paso para mantener acotada la ejecución. El campo JSON histórico `instructions` cuenta **pasos CPU**, no instrucciones retiradas por hardware. Los fallos revierten registros y escrituras efectuadas por los métodos de la CPU, pero no prometen revertir efectos externos de un callback.

## Límite actual para Windows

El intento de activar `CR0.PE` o la paginación produce un fallo explícito antes de modificar ese estado. Todavía faltan:

1. Modo protegido: descriptores, privilegios, excepciones, interrupciones y cambio de contexto.
2. MMU y paginación, con permisos y fallos de página; ampliación de instrucciones que requiera la versión elegida de Windows. Windows de 64 bits también exige modo largo.
3. Hardware de PC operativo: controladores de interrupciones y temporizador, almacenamiento escribible con controlador, buses y dispositivos que descubra Windows, y vídeo gráfico.
4. Pruebas de arranque e instalación con una ISO oficial concreta y medición en el teléfono real.

Los registros de máscara PIC, algunos registros CMOS, A20 y la salida serie son servicios parciales; no equivalen a implementar esos dispositivos completos. No hay entrega de IRQ, x87/SSE, ACPI, APIC, JIT, red ni audio en esta versión. El indicador `windows_ready` permanece en `false`.

## Compilar y verificar

### Motor nativo

Desde este directorio, con un compilador C++17:

```sh
bash tools/test-native.sh
```

El script compila los tests de CPU, medios, integración de PC e ISO. Después ejecuta el arranque propio y guarda una imagen de prueba en `build/native/cosmic-x86-demo.img`.

También admite CMake:

```sh
cmake -S . -B build/cmake
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

Uso de la CLI:

```sh
build/native/cosmic_pc_cli --demo
build/native/cosmic_pc_cli --image /ruta/al/medio.iso --max-instructions 1000000
```

### Android

Herramientas declaradas: JDK 17, Gradle 8.9, Android Gradle Plugin 8.7.3, SDK 35, NDK 27.2.12479018 y CMake 3.22.1. No hay Gradle Wrapper incluido: utilizar Gradle 8.9 instalado o el workflow del repositorio.

```sh
gradle --no-daemon :app:assembleDebug :app:assembleDebugAndroidTest
```

El workflow `.github/workflows/own-pc-android.yml` compila las dos ABI, verifica la firma y las bibliotecas empaquetadas, y ejecuta ocho pruebas instrumentales con el APK construido. El dispositivo virtual oficial de Android se utiliza como infraestructura de pruebas: el motor que ejecuta el huésped x86 dentro de la app es exclusivamente `cosmic_pc`.

El artefacto de CI contiene APK, SHA-256, commit, logs, resultado instrumental y captura de pantalla. La firma es de desarrollo; el workflow no publica una versión de distribución ni compra licencias de Windows.

## Qué prueban los tests

- La ALU de 8 bits se compara exhaustivamente con fórmulas de resultado y flags independientes para ADD, ADC, SUB, SBB y CMP.
- Se verifican direccionamiento, saltos, pila, cadenas, condiciones de fallo y restauración del estado de CPU.
- El lector rechaza medios truncados, firmas y catálogos inválidos, tamaños fuera de rango y modos no implementados.
- Un invitado de la ISO de prueba consulta EDD, lee un sector distinto al de arranque y comprueba sus extremos. Datos correctos producen `I`; datos alterados producen `F` desde el propio invitado.
- La integración prueba teclado, reinicio, memoria, acceso al archivo después de cerrar el descriptor Java y la actividad operando la CPU real.

El número de comprobaciones no representa compatibilidad con Windows. Los logs de cada commit son la evidencia de su compilación y ejecución; la validación en un Android virtual no mide el rendimiento en el Samsung físico.

## Referencias de arquitectura

- [Intel Software Developer's Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html): formatos y semántica de instrucciones x86.
- [El Torito Bootable CD-ROM Format Specification](https://pdos.csail.mit.edu/6.828/2005/readings/boot-cdrom.pdf): estructuras del arranque desde CD.
- [Microsoft Oscdimg](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/oscdimg-command-line-options): distinción entre entradas de arranque BIOS y UEFI.

Estas referencias describen interfaces. No se incorporó código fuente de otros emuladores.
