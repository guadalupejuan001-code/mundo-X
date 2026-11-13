# REVERSE Windows C09R-1 — Estado FPU/x87, XMM y subconjunto SSE2 (sin ejecutar Windows)

## Fuente de autoridad

Los cambios se implementan exclusivamente en `pc-android/native/x86_cpu.hpp`, la CPU canónica de Mundo X. La RAM `MemoryFabric` conserva los bytes, sus transacciones y su rollback. No se usa otro procesador, tensor, Qiskit ni traducción falsa a cómputo cuántico.

Intel SDM, formato de estado:
- https://cdrdv2-public.intel.com/843827/253665-sdm-vol-1-dec-24.pdf
- https://cdrdv2-public.intel.com/868140/253666-089-sdm-vol-2a.pdf
- https://cdrdv2-public.intel.com/874240/325462-090-sdm-vol-1-2abcd-3abcd-4.pdf

## Estado arquitectónico implementado

1. `ExtendedState fx` contiene **XMM0–XMM15, 128 bits exactos**, MXCSR (0x1F80 inicial), FCW (0x037F inicial), FSW, FTW abreviado, FOP y ocho registros x87 de **80 bits crudos exactos**. Se resetea con la CPU y se incluye en los snapshots de transacciones, fallos y entrega de interrupciones. Ninguna función de cómputo host no equivalente sustituye esos bytes.
2. `CR4.OSFXSR` y `CR4.OSXMMEXCPT` son bits reconocidos por el control register del invitado. Las instrucciones XMM exigen OSFXSR; CR0.EM y CR0.TS producen errores arquitectónicos. `#NM` tiene vector 7 en la IDT.
3. `FXSAVE/FXRSTOR` (`0F AE /0`, `/1`) guardan/restauran una región de 512 bytes alineada a 16: campos FCW/FSW/FTW/FOP, MXCSR, MXCSR_MASK, ST0–ST7 y XMM0–XMM7 (y XMM8–XMM15 exclusivamente en modo 64 bits). `FXRSTOR` valida los bits reservados del MXCSR antes de mutar el estado. La región de software/reservada no se modifica arbitrariamente por FXSAVE.
4. `LDMXCSR` / `STMXCSR` leen y escriben el registro de control vectorial con comprobación de bits reservados.
5. x87: `FNINIT`, `FNCLEX`, `FNSTSW AX`, `FLD1`, `FLDZ`, `FCHS`, `FABS`, `FLD ST(i)`, `FXCH ST(i)`, `FLD m80`, `FSTP m80`, `FLDCW` y `FNSTCW` preservan los datos de 80 bits con pila real TOP y tags físicos. **No existe todavía aritmética de coma flotante de 80 bits.**
6. Subconjunto de **SSE2 entero real**: `MOVDQA`, `MOVDQU`, `MOVAPD`, `MOVUPD`, `MOVD/MOVQ`, `MOVSD/MOVSS` para transporte, `PXOR`, `PAND`, `POR`, `PANDN`, `PADDB/W/D/Q`, `PSUBB/W/D/Q`, `PCMPEQB/W/D`, `PSHUFD`, `PSLLDQ/PSRLDQ` con semántica exacta por lane y comprobación de alineación en transferencias alineadas.
7. Las operaciones sobre MMIO se rechazan si no se puede ofrecer la semántica correcta. Las transferencias de memoria FXSAVE/FXRSTOR y XMM se hacen en palabras de 8 bytes y validan una vez cada página, evitando cientos de recorridos MMU por byte.

## Pruebas nuevas

`pc-android/native/tests/x86_c09r1_extended_state_test.cpp` valida:
- x87 FNINIT/FLD1/FSTP m80/FLD m80 y control word; TOP y bits de tag.
- operaciones XMM SIMD con valores concretos, stores y shuffles, y alineación.
- FXSAVE de 512B con XMM0..XMM15, MXCSR, FCW y dato x87 de 80 bits; FXRSTOR recupera exactamente todo; un MXCSR con bits reservados provoca fallo y rollback total.
- carga/almacenamiento de MXCSR y **FXSAVE con validación MMU por páginas**, sin usar 512 recorridos por byte.
- FXSTATE en modo heredado conserva bytes reservados y no carga/guarda XMM8..XMM15.
- CR0.TS provoca #NM; sin OSFXSR el opcode XMM es inválido.
- CPUID **NO** anuncia SSE2, x87 ni soporte completo de Windows 10 todavía.

`pc-android/tools/test-native.sh` incluye el test nuevo además de C04–C09 y demostraciones previas. Workflow `.github/workflows/reverse-windows-c09r1-extstate.yml` prueba todo en GitHub Actions. Se encadena sobre PR #91 de Mundo X y se integrará en REVERSE Windows usando su SHA exacto, nunca una CPU secundaria.

## Límites explícitos / siguientes etapas obligatorias

- **SSE2 NO COMPLETO:** faltan aritmética de dobles (ADDPD/ADDSD, CMP*, conversiones/rounding, excepciones MXCSR, instrucciones adicionales), MMX, y varias instrucciones de contexto/ordenamiento.
- **x87 NO COMPLETO:** faltan aritmética extendida, redondeos, NaN/infinitos, excepciones, memoria m32/m64, campos FIP/FDP con precisión arquitectónica y estado completo de punteros FPU.
- `FXSAVE/FXRSTOR` tienen un formato verificable de campos principales, pero no se declara compatibilidad completa con el contexto x87/MMX en todos los modos.
- Por integridad, CPUID SSE2 y x87 permanecen apagados. No forzar bits a 1 para que un sistema acepte instalarlos; primero implementar y medir las instrucciones que usa.
- El snapshot de FX se copia en cada paso de CPU: esto preserva rollback, pero todavía necesita un benchmark de overhead frente al intérprete anterior. No se atribuyen ganancias de rendimiento sin medición.
- No se montó, descargó ni ejecutó Windows 10; la ISO del usuario permanece exclusivamente en su dispositivo. No hay APK nueva verificada.

Este ciclo es **un incremento probado de la infraestructura SSE2/x87**, no el final de la compatibilidad Windows 10.
