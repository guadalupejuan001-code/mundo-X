# C06 — CPU física virtual: MMIO y entrega externa de IRQ

Fecha: 2026-11-12. Continuidad de Mundo X C05 PR #87, sin fusionar.

## Fuente canónica

- Se conserva `cosmic_pc::Cpu` y `PcMachine` de Mundo X, con Ring3/TSS e IDT de C04/C05.
- No existe una segunda CPU ni se sustituye Ether/XI.
- `pc-android/native/x86_cpu.hpp`: tres hooks explícitos `mmio_claim_hook`, `mmio_read_hook`, `mmio_write_hook` para bytes de bus físico, incluso fuera de la RAM lógica; un registro reclamado sin soporte provoca error y **no** crea memoria ficticia.
- `Cpu::raise_external_irq(vector)`: entrega un evento físico externo, distinto de INT software, con comprobación IF, marco IVT en modo real o IDT en modo protegido/largo, recuperación transaccional si falla la entrega y despertar desde HLT.
- `pc-android/native/pc_machine.hpp`: hooks `device_step_hook` (poll entre instrucciones) y `device_reset_hook` (republicar firmware/dispositivos tras resetear RAM y cargar boot sector). El CPU y las ventanas Ether mantienen la misma autoridad.

## Evidencias (código x86 real)

`pc-android/native/tests/x86_bus_c06_test.cpp`:

1. Ejecuta MOV EAX,[FEE00030], MOV [FEE00080],EAX y MOV ECX,[FEE00080] en modo protegido32, con operaciones MMIO reales 4 bytes LE.
2. Comprueba que los registros MMIO fuera de RAM no materializan gigabytes de RAM, y que los accesos MMIO no soportados y fuera de bus fallan explícitamente.
3. Comprueba que una IRQ enmascarada por IF=0 no cambia registros ni pila.
4. Entrega IRQ externa real a la IVT; instrucciones x86 INC e IRET ejecutan el manejador y restauran IP, IF y SP.

Workflow `.github/workflows/reverse-windows-c06-bus.yml` ejecuta estas pruebas, C05, C04, x64, perfil, grupo6 y toda la suite `pc-android/tools/test-native.sh`; la suite nativa incluye también C04/C05/C06.

## Fronteras

- Los hooks MMIO no incorporan DMA ni son transacciones de dispositivos reversibles. Sólo escrituras de RAM x86 quedan cubiertas por el journal de instrucciones; los dispositivos deben validar sus comandos antes de cambiar estado.
- La cadencia de `device_step_hook` usa pasos del intérprete; no representa 1.193 MHz reales de PIT, TSC calibrado ni latencia de un procesador físico.
- `raise_external_irq` entrega puertas soportadas por los modos del CPU; APIC/x2APIC, SMP, vectores complejos, NMI, reentradas y conformidad completa Intel quedan pendientes.
- La ejecución de Windows 10 x64 sigue sin demostrarse. No hay soporte nuevo de SSE2/x87 ni JIT.
- La placa base PCI/PIC/PIT/RTC/APIC/ACPI vive en REVERSE Windows: su contrato C06 usa estos hooks de la CPU.

Sin merge automático. Para integrar, fijar un SHA real y ejecutar pruebas del huésped + QSSD.
