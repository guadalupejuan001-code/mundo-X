# REVERSE Windows C05 — Privilegios CPL 0/3 de x86/x64

Fecha: 2026-11-12. Rama de desarrollo sobre C04, sin fusionar.

## Objetivo y fuente

Se amplía la CPU canónica `pc-android/native/x86_cpu.hpp` de Mundo X. Sigue siendo el intérprete real `cosmic_pc::Cpu` detrás de `PcMachine` y Ether/XI, no un segundo motor ni una representación gráfica falsa.

## Cambios implementados

- La CPU puede ejecutar código invitado en CPL0 y CPL3 (cpl derivado de CS.RPL); CPL1/2 no se anuncian.
- SegmentCache incluye DPL y conforming. Descriptor GDT interpretado con verificación de selector, RPL y privilegio efectivo max(CPL,RPL). Accesos de datos y pila en modo protegido se validan; IRET a un anillo menos privilegiado invalida DS/ES/FS/GS previamente inaccesibles. Bases FS/GS por MSR conservadas.
- IRETD e IRETQ permiten retorno exterior de CPL0 a CPL3, validan CS y SS, sus DPL, RPL, tipo y punteros de pila. No elevan privilegios desde Ring3.
- Los manejadores IDT de modo protegido32 y largo64 pasan de Ring3 a Ring0 mediante SS0:ESP0 del TSS de 32 bits o RSP0 del TSS de 64 bits. La CPU lee GDT/IDT/TSS con permisos internos de supervisor, mientras las instrucciones ordinarias del invitado siguen sujetas a su CPL. El salto a TSS se realiza con LTR x86 real.
- El caminador de páginas IA-32e combina los bits U/S de todas las tablas y exige supervisor vs usuario según el CPL. #PF incluye bit U/S, W/R, P y NX/ID cuando corresponde. El kernel puede continuar tras manejar #PF.
- CPL3 no puede ejecutar HLT, CLI/STI con IOPL insuficiente, MOV CR, RDMSR/WRMSR, LGDT/LIDT, LTR/LLDT, LMSW/CLTS ni I/O de puertos con IOPL insuficiente. Estas rutas emiten #GP hacia la IDT invitada.
- No se desactivaron los mecanismos C04 de rollback, #PF/#GP/#DE/#UD ni la política de fallar ante hardware ausente.

## Pruebas de código invitado

Archivo: `pc-android/native/tests/x86_cpl_c05_test.cpp`

1. Desde kernel x64, un huésped construye el marco y ejecuta IRETQ hacia Ring3.
2. Usuario ejecuta INT 0x80; la CPU recupera RSP0 de una TSS real, apila contexto y ejecuta el manejador en Ring0.
3. El kernel vuelve a Ring3 mediante IRETQ; los datos privilegiados previos no quedan accesibles.
4. Código de usuario intenta leer página supervisor y produce #PF(5); intenta escribirla y produce #PF(7); CR2 apunta a la dirección real.
5. HLT y CLI de usuario generan #GP, como también MOV DS con DPL0 y MOV SS con RPL incompatible. Se prueba que un selector DPL3 con RPL0 sí es válido como segmento de datos desde CPL3.
6. El huésped protegido32 usa una TSS de 32 bits, entra y sale de CPL3 mediante IRETD e INT, valida SS0:ESP0, restaura el contexto y recarga DS desde código de kernel.
7. Al final vuelve al kernel por INT y ejecuta HLT genuino.

GitHub Actions: `.github/workflows/reverse-windows-c05-privileges.yml` compila C05, ejecuta C04, regresiones long64/perfil/grupo6, la suite nativa completa `pc-android/tools/test-native.sh` y demos. No se suben ISOs privadas.

## Restricciones conocidas

- Todavía NO arrancan Windows 10 ni Windows PE; no se ha probado una ISO real en el teléfono.
- Se han probado dos CPL (0 y 3), transición clásica INT/IRET. No se anuncia soporte integral de todos los privilegios, descriptores de LDT, gates de llamada, SYSCALL/SYSRET, SYSENTER/SYSEXIT, task switches, VM86, APIC ni IRQ externas.
- El TSS se prueba para pilas Ring0 (RSP0/SS0:ESP0). No se asegura cobertura completa de IST, mapa de permisos I/O ni reentrada compleja.
- IA-32e soporta solo una parte de la paginación de cuatro niveles, no paginación legacy; faltan partes de la semántica Intel de descriptores y excepciones.
- No se anuncian SSE2, x87, JIT, CPUID compatible con Windows ni rendimiento real en Android ARM64. Las funciones no implementadas siguen dando error explícito.
- C05 es un paso de ejecución privilegiada real, no conformidad total del hardware virtual.
