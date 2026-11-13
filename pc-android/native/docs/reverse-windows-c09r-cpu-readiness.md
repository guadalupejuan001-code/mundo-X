# C09R — CPU canónica preparada parcialmente para Windows 10 x64, sin Windows

Fecha 2026-11-12. Se continúa sobre C09 PR #90. No se sustituye PcMachine ni se ejecuta ninguna ISO de Windows.

## CPU x86 y pruebas

- Instrucción CMPXCHG8B: comparación de EDX:EAX con m64 y sustitución por ECX:EBX, ZF correcto, bytes exactos.
- Instrucción CMPXCHG16B (REX.W 0F C7 /1): comparación RDX:RAX con m128 alineado a 16 bytes; reemplazo RCX:RBX si coincide; en mismatch se restauran RDX:RAX con el valor de memoria y ZF=0. La operación utiliza el journal de la CPU canónica. No se afirma atomicidad entre múltiples vCPU, porque sólo hay una vCPU.
- LOCK se acepta únicamente para CMPXCHG8B/16B; otros opcodes LOCK continúan rechazados. No se anuncian instrucciones aún no probadas.
- PREFETCHW (0F 0D /1): pista sin lectura real que no crea páginas ni provoca E/S falsa. LAHF/SAHF ya existía y ahora tiene flags de CPUID coherentes.
- CPUID leaf 1: CX8 y CX16 sólo después de pruebas; leaf 0x80000001 anuncia LAHF/SAHF y PREFETCHW. SSE2 y x87 permanecen apagados.
- Código del test: pc-android/native/tests/x86_windows10_c09r_cpu_readiness_test.cpp; además se ejecuta la suite completa de C04-C09 y todas las demos históricas.
- La regresión C09 de opcode no soportado ahora prueba LOCK NOP explícitamente (F0 90); no se sacrifica la precisión de la traza por ampliar la ISA.

## Requisitos pendientes de CPU para Windows 10

Microsoft, Windows 10 Minimum Hardware Requirements, sección 3.1:
https://download.microsoft.com/download/c/1/5/c150e1ca-4a55-4a7e-94c5-bfc8c2e785c5/Windows%2010%20Minimum%20Hardware%20Requirements.pdf

1. Implementar realmente FPU/x87, su estado y excepciones, las operaciones FXSAVE/FXRSTOR, FXSAVE-compatible XMM y MXCSR.
2. Implementar SSE2 y las operaciones requeridas con modos de direccionamiento y privilegio, habilitación correcta de CR4.OSFXSR y estado de contexto.
3. Agregar LOCK a otras operaciones que use el kernel invitado, con rigor y tests, nunca suponiendo SMP/atomicidad sin modelo.
4. Probar syscalls, IDT/TSS y compatibilidad de estado protegido en huéspedes propios. No activar bits CPUID hasta cumplir cada contrato.

C09R mejora funciones verificables, pero **Windows 10 todavía no está preparado para instalarse**, y no se ha cargado la ISO del usuario.
