# MUNDO X PC V1 — entrega Android 1.0.0

V1 del emulador de PC propio de Mundo X con Ether. No incluye Windows 10 ni finge su escritorio.

## Funciones operativas
- CPU x86/x64 propia C++ con arranque BIOS, modos real/protegido/largo y diagnóstico ante operaciones no soportadas.
- ISO original y discos en solo lectura a través del selector Android, sin copiar el archivo completo a RAM.
- Vídeo de texto VGA y framebuffer VBE, renderizador OpenGL ES y teclado BIOS.
- Pausa, reanudar, paso, reiniciar, copiar diagnóstico y pruebas nativas integradas.
- 2 GiB de RAM lógica por páginas bajo demanda (límite de páginas residentes de 1 GiB, consumo físico real medido aparte).
- Un mismo InfiniteCore/Ether como dispositivo del huésped, ventana de memoria, editor y visor.
- Pruebas de CPU x86, x64, GPU y conexión real del huésped a Ether sin depender de Windows.

## Límites críticos
No arranca todavía Windows 10 como sistema completo. Faltan ring3, SSE2/x87, UEFI, IRQ/IDT, ACPI/APIC, almacenamiento virtual escribible y dispositivos requeridos por Windows. Que una ISO avance en modo protegido no prueba compatibilidad con el kernel o escritorio. Esta V1 entrega una computadora experimental real, no Windows funcional.

## Instalar y probar
Nombre de app: Mundo X PC V1; paquete: com.cosmicx.pc.v1; versionName: 1.0.0; versionCode: 100; ARM64 y x86_64; Android 8 o posterior.
1. Instalar Mundo-X-PC-V1.apk. Tiene paquete distinto para convivir con aplicaciones anteriores.
2. Abrir la aplicación y ejecutar Prueba x64 para observar el texto del huésped real.
3. Ejecutar Prueba núcleo x64 para comprobar el dispositivo Ether de la misma máquina.
4. Elegir Abrir ISO/disco, seleccionar una ISO local oficial, pulsar Ejecutar; copiar el diagnóstico de la primera función aún no admitida.

## Criterios de validación
- Código del emulador original de Mundo X, sin QEMU, Wine, Limbo u otro CPU externo.
- Pruebas nativas C++ de CPU, ISA x64, periféricos, RAM por demanda, ISO, Ether, vídeo.
- Las tres bibliotecas nativas (libcosmic_pc.so, libmx_graphics.so y libc++_shared.so) para ARM64 y x86_64.
- Firma Android verificada, APK/ZIP con checksum SHA256, identidad del instalador y fuente reproducible.
- Instrumentación en Android 15 con 30 pruebas de CPU, GPU, memoria, teclado y Ether; windows_booted=false.

Las llaves temporales de depuración cambian entre compilaciones: una V1 firmada con llave temporal puede necesitar desinstalación antes de actualizar. El pipeline usa la firma privada del proyecto cuando está configurada en GitHub.
