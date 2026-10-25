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
del procesador. El compilador del proyecto solo construye las fuentes ya
presentes en Mundo X.

## Próxima etapa de compatibilidad

Probar contra la ISO concreta del usuario (no disponible en CI), registrar
la primera instrucción/dispositivo no implementado y avanzar por esa ruta
sin adulterar el modelo CPUID. Incorporar firmware UEFI y controladores de
almacenamiento con test de sistema invitado antes de afirmar que Windows arranca.
