# C09R-1, etapa 2 — FPU x87 + FXSAVE/FXRSTOR + SSE2 parcial

**Propiedad del estado:** toda la CPU reside en el objeto canónico `cosmic_pc::Cpu` de Mundo X. No se crea otra CPU ni se almacena el estado activo de XMM en GPU, QSSD o Ether/XI. Los 8 registros físicos x87 (80 bits) y los 16 registros XMM (128 bits) se incluyen en el snapshot/rollback de la instrucción original. El estado `MXCSR`, control, status, tags y FIP/FDP también es parte de esa transacción.

## Implementado, bajo pruebas x86 propias

- x87: `FNINIT`, `FNCLEX`, `FNOP`, `FLD1`, `FLDZ`, `FLD ST(i)`, `FXCH ST(i)`, `FSTP ST(i)`, `FLD/FSTP m32/m64`, `FADD ST0, ST(i)`, `FLDCW`, `FNSTCW`, `FNSTSW AX/m16`, `FWAIT` básico. El cálculo de flotantes actualmente pasa por `long double` del anfitrión; **no anuncia redondeo de precisión extendida idéntico en todos los ARM64** ni todas las excepciones FPU.
- FXSAVE y FXRSTOR (0F AE /0 y /1) con área de 512 bytes alineada y validada, MXCSR reservado, FPU x87 con abridged tag físico, **ST0..7 en orden lógico** y XMM0..7 (más XMM8..15 en modo largo).
- `FXSAVE64` y `FXRSTOR64` con REX.W y FIP/FDP de 64 bits; `FXSAVE` sin REX.W usa campos legacy. Se preservan los bytes 464–511 de software.
- CR4.OSFXSR y CR0.TS/EM: fallos explícitos, `#NM` vector 7, procesado por el IDT original. Huésped x64 elimina TS con CLTS y reinicia exactamente la instrucción FXSAVE64 con IRETQ.
- SSE2 de enteros: `MOVDQA/MOVDQU`, `MOVUPD/MOVAPD`, `MOVD/MOVQ`, `PXOR`, `PAND/PANDN/POR`, `PADDB/W/D/Q`, `PSUBB/W/D/Q`, `PCMPEQB/W/D`, `PCMPGTB/W/D`, `PUNPCKL/H*`, `PSLL/PSRL W/D/Q`, `PSLLDQ/PSRLDQ`, `PSHUFD`, `PMOVMSKB`, `LDMXCSR/STMXCSR`.
- Las escrituras SIMD prevalidan el destino y se pueden revertir ante fallo con el mismo journal transaccional de RAM, sin utilizar la GPU física para ejecutar opcodes.

## Pruebas reproducibles

- `x86_c09r1_fxstate_test.cpp`: x87 exacto 1.0/0/1.5/2.0/3.5 en casos finitos, TOP/tag físico, FXSAVE legado y largo (incluyendo XMM15), FXRSTOR, colas de 48 bytes de software, MXCSR ilegal, misalign, CR0.TS (#NM), restauración de dos contextos independientes y #NM→CLTS→IRETQ→FXSAVE64 con código de huésped real.
- **Oráculo independiente del hardware x86** en GitHub Actions: compara FCW, FSW, FTW y datos lógicos ST0/ST1 obtenidos mediante `fninit; fld1; fldz; fxsave` de la CPU física, restaurando después el estado del proceso. Permitió detectar que FXSAVE guarda datos ST en orden lógico mientras FTW es físico.
- `x86_c09r1_sse2_packed_test.cpp`: operaciones de enteros por lane, signos y máscaras, PSHUFD/byte shift, MOVD/MOVQ, memoria/alineación/rollback.
- Se conservan C04–C09R, QSSD, ATA, IRQ14 y GPU Virtual. CI de Mundo X incluye la suite nativa completa.

## Contratos todavía NO implementados

**La CPU no es todavía compatible con Windows 10 completo.** Falta ampliar las operaciones x87 (especialmente excepciones y redondeo), instrucciones SSE/SSE2 escalares y flotantes, control de denormales/estado de excepción de MXCSR, MMX y ciertas operaciones de contexto. Faltan pruebas de context switching real entre dos procesos con Ring3/IDT y gestión completa de fallos `#MF/#XM`, además del hardware restante.

Los bits CPUID x87/FXSR/SSE/SSE2 se mantienen **apagados** mientras estos contratos sigan incompletos. En este ciclo no se abrió ni ejecutó ninguna ISO de Windows, ni se compiló una APK. La integración de REVERSE Windows debe fijar el commit exacto de la CPU y conservar el motor original.
