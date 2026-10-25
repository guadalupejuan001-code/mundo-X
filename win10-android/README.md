# Mundo X Windows 10 Lab — ISO oficial, núcleo propio

**Windows 10 todavía NO arranca como sistema completo.** Esta es la primera
APK independiente, verificable, que inspecciona una ISO del usuario, selecciona
su arranque BIOS si existe y ejecuta sus bytes mediante el intérprete **propio**
`PcMachine` de Mundo X, sin QEMU, Wine, Limbo ni escritorio simulado falso.

El proyecto es una aplicación de diagnóstico/arranque experimental, no una
alternativa funcional a Windows 10 para ejecutar .exe.

## Lo que ya funciona

- Selector Android SAF para ISO oficial, sin copiar la imagen a RAM.
  El archivo debe estar descargado localmente y ser de acceso aleatorio.
- Inspección exacta del catálogo El Torito: volumen, validación, entrada BIOS
  0x00, entrada UEFI 0xEF, desplazamientos y tamaño. No identifica
  criptográficamente el origen oficial de la ISO.
- Si la entrada predeterminada es BIOS sin emulación y el propio bootloader
  puede cargarla: arrancar y ejecutar instrucciones x86 reales, ver estado,
  video de texto y la primera instrucción/dispositivo pendiente.
- Si solo hay UEFI, informa **UEFI aún no implementado**; no intenta
  ejecutar código UEFI como si fuera BIOS.
- RAM lógica x86 de 2 GiB respaldada por páginas de 4 KiB según demanda,
  presupuesto real del huésped de 256 MiB y métricas verificables.
- MemoryFabric expone una ventana Ether de 256 bytes a la CPU. InfiniteCore
  conserva su sesión y dispositivo x86 original. Las pruebas x64 y
  x86→Ether pueden ejecutarse desde la app sin ISO.
- CPU CPUID/RDTSC con características declaradas conservadoras; **SSE2 NO
  implementado ni anunciado**. Las instrucciones faltantes detienen el núcleo
  con diagnóstico, no generan resultados ficticios.

## Limitaciones críticas para Windows 10

El emulador carece de firmware UEFI, CPU SSE2/x87 y cobertura ISA total, ring 3,
interrupciones/IDT protegidas, dispositivos ACPI/APIC y almacenamiento virtual
escribible con controlador adecuado. Windows 10 requiere RAM suficiente,
almacenamiento y dispositivos emulados; el presupuesto de memoria definido para
esta primera APK protege el teléfono y no permite instalar Windows completo.
**No se afirma un arranque de Windows 10 ni que su escritorio funcione.**

El ISO se monta en **solo lectura**, con permiso otorgado explícitamente.
No se modifica ni se redistribuye la ISO del usuario.

## Compilar desde Mundo X

```sh
bash pc-android/tools/test-native.sh
gradle -p win10-android --no-daemon :app:assembleDebug :app:assembleDebugAndroidTest
```

APK: `win10-android/app/build/outputs/apk/debug/app-debug.apk`.
En Android: seleccionar ISO → ver BIOS/UEFI → pulsar **Arrancar BIOS
(experimental)** → mirar diagnóstico / copiar texto.

La interfaz ejecuta las funciones JNI originales de
`pc-android/app/src/main/cpp/pc_jni.cpp` y
`pc-android/native/pc_machine.hpp`. No mantiene una implementación duplicada
del procesador. El proyecto reutiliza los fuentes C++ canónicos de Mundo X; una copia byte-por-byte de la interfaz Java NativeMachine (que no implementa ninguna instrucción de CPU) es verificada por CI contra el original.

## Próxima etapa de compatibilidad

Probar contra la ISO concreta del usuario (no disponible en CI), registrar
la primera instrucción/dispositivo no implementado y avanzar por esa ruta
sin adulterar el modelo CPUID. Incorporar firmware UEFI y controladores de
almacenamiento con test de sistema invitado antes de afirmar que Windows arranca.


## ISO oficial: diagnóstico del controlador 8042

El 24 de octubre se observó en el teléfono un fallo real de arranque:
\`2000:0ACE  E6 64\` — \`OUT 0x64, AL\`, después de 2.791 pasos CPU y
21 operaciones de lectura BIOS. El puerto **0x64** es el registro de comandos
del controlador PC/AT **8042**.

El emulador ahora conecta el puerto 0x64 y el puerto de datos 0x60 a la
misma máquina nativa y a **la misma señal A20 del procesador**. Soporta un
subconjunto explícito: D1 y D0 (escribir/leer puerto de salida/A20), 20 y 60
(configuración), AD/AE (reloj teclado), A7, AA/AB (autopruebas). Los
comandos desconocidos, solicitudes de reset del sistema, entrada de teclado
no implementada e IRQs siguen bloqueados con diagnóstico, sin inventar
respuestas exitosas.

**Prueba de regresión:** una ISO sintética El Torito carga código que llega
exactamente a \`2000:0ACE\` y ejecuta la secuencia de A20 por el 8042.
Eso demuestra que hemos resuelto **esa clase de parada**, pero **no
demuestra que tu ISO oficial avance**; aún debemos observar la siguiente
instrucción cuando instales esta actualización y repitas la prueba.

El panel ahora presenta comandos 8042, estado A20 y RAM ocupada con
precisión de KiB; \`Arrancar BIOS\` deja de aparecer habilitado durante
las demostraciones internas x64/Ether.

## Actualización 0.1.2 — segundo bloqueo en la ISO

La ejecución **real en el teléfono** llegó desde `2000:0ACE` hasta
`2000:0ADC`:

- Antes: 2.791 pasos CPU, 21 lecturas BIOS, `E6 64` sin controlador.
- Después: 2.817 pasos CPU, 21 lecturas BIOS; comando `0xD1` aceptado,
  un dato del controlador aceptado, A20 habilitada.
- Nueva parada: `2000:0ADC`, `OUT 0x64, AL` con `AL=0xFF`.

Según el contrato del 8042, el intervalo de comandos `0xF0…0xFF`
pulsaría durante aproximadamente 6 ms las líneas de salida cuyos bits
de máscara estén en cero. **0xFF deja todas esas líneas sin pulsar.**
Por tanto, el controlador ahora acepta `0xFF` sin alterar A20,
configuración, estado de respuestas ni provocar reset. **0xFE** y los
demás comandos de pulso que podrían modificar líneas permanecen sin
soporte, con fallo explícito; no se simula un reset físico sin semántica.

Un programa de prueba independiente dentro de una ISO sintética
verifica las dos direcciones exactas `2000:0ACE` y `2000:0ADC`
ejecutando instrucciones reales del intérprete C++. Se repite mediante
JNI en un emulador Android 15.

El equipo aún **no ha probado la ISO oficial con esta versión**;
la siguiente barrera del bootloader debe medirse de nuevo en el teléfono.
Windows 10 completo sigue sin estar implementado.

Referencia técnica:
https://wiki.osdev.org/%228042%22_PS/2_Controller


## v0.1.3 — Native Library Fix (Samsung Android)

La captura más reciente no incluye el texto completo del error de carga,
así que no se presume una biblioteca concreta ausente. Se inspeccionó la APK
0.1.2: contiene para `arm64-v8a` y `x86_64` las bibliotecas
`libcosmic_pc.so`, `libmx_graphics.so`, `libc++_shared.so` y sus
dependencias ELF son coherentes. Todas las entradas ELF y ZIP ARM64
están alineadas a 16 KiB. Falta verificar qué error devuelve el enlace
dinámico de Android en el teléfono.

Se detectó además un problema real de distribución: APKs previas
fueron firmadas con **certificados debug distintos en cada ejecución
de GitHub Actions**. Android no permite actualizar directamente un
paquete cuya firma ha cambiado. No se intenta eludir esa seguridad.

El flavor `rescue` ofrece un paquete distinto
`com.cosmicx.win10.rescue` con el mismo código original; se instala
sin entrar en conflicto con `com.cosmicx.win10` y aparece como
**Mundo X Windows 10 FIX**. El flavor `standard` mantiene el ID antiguo.

Ambos incluyen:

- `jniLibs.useLegacyPackaging=true`, que solicita al gestor Android
  extraer las bibliotecas nativas a `ApplicationInfo.nativeLibraryDir`.
- Carga ordenada: biblioteca de C++, puente Ether/gráficos, intérprete
  x86 propio. Intenta ruta nativa directa únicamente si loadLibrary falla.
- Diagnóstico en la pantalla y en el botón `Copiar diagnóstico`:
  Android SDK, ABI del proceso, ABI disponibles, directorio nativo,
  archivos instalados, tamaño de cada .so y error exacto del linker.
- Ningún nuevo emulador externo ni cambio de instrucciones x86.

La prueba en emulador Android 15 valida **ambos paquetes** y confirma
carga real de las bibliotecas y ejecución de la ISO sintética. La
compatibilidad exacta con el Samsung depende del informe en ese dispositivo.

Si el flavor FIX funciona, vuelve a seleccionar tu ISO original y
compárteme el nuevo estado CPU. **No se afirma Windows 10 funcional.**
