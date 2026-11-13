# C09R-1 etapa 4 — SSE2 DAZ/FTZ, conversiones y control x87 (sin Windows)

Mundo X PR #96 continúa la PR #95 y reúne el primer incremento DAZ/FTZ con las pruebas de SSE2 y x87 de la rama experimental. Las verificaciones usan huéspedes sintéticos y oráculos de hardware x86. Windows y la ISO del usuario quedan fuera de la campaña.

## Arquitectura respetada

El único procesador es `cosmic_pc::Cpu` del `PcMachine` original. Los registros XMM/MXCSR y x87 físico de 80 bits continúan en esa CPU y su `Snapshot` transaccional. El compilador anfitrión ejecuta operaciones IEEE seleccionadas con ambiente de redondeo restaurado después de cada operación, pero **no se traslada la semántica x86 a GPU Virtual**. Ether/XI y QSSD no sustituyen memoria ni estados arquitectónicos. Los dispositivos GPU Virtual, PCI ATA, IRQ14, BIOS, QSSD y REgraphics pertenecen a REVERSE Windows y se protegen mediante regresiones de integración.

## Funcionalidad C09R-1 etapa 4

### SSE2 y MXCSR

- `DAZ` (MXCSR bit 6) trata entradas binary64 subnormales como cero con su signo. `LDMXCSR` lo acepta y `FXSAVE` lo incluye en `MXCSR_MASK`.
- Con underflow enmascarado, `FTZ` sustituye un resultado subnormal por cero con signo y registra UE/PE. Con underflow desenmascarado, los casos de `MULSD` con resultado diminuto exacto o inexacto entregan #XM con UE y preservan XMM.
- Las operaciones empaquetadas acumulan flags de ambos elementos antes de actualizar el destino. `DIVPD` puede registrar IE y ZE en una misma instrucción. En `CVTPD2DQ`, IE enmascarado y PE se acumulan en ambos órdenes, mientras que IE desenmascarado se entrega antes de PE.
- Las conversiones FP64 a entero respetan DAZ y registran PE cuando un subnormal se redondea a otro valor. El hardware no registra DE en estas conversiones. `MINSD/MAXSD` seleccionan el segundo operando cuando interviene un NaN. Los casos de `ADDSD` y `UCOMISD` verifican el tratamiento de NaN junto con un subnormal.
- Nuevas instrucciones SSE2: `CMPPD/CMPSD` y predicados inmediatos 0..7, `SHUFPD`, `MOVMSKPD`, `ANDPD/ANDNPD/ORPD/XORPD`, `MOVLPD/MOVHPD`, `CVTDQ2PD`, `CVTPD2DQ`, `CVTTPD2DQ`. Se prueban NaNs, flags, bits exactos, regiones de memoria y conversión entera a indefinite.
- Las excepciones desenmascaradas siguen la entrega `#XM`/ `#UD` mediante IDT y rollback de los cambios en XMM y RAM; los indicadores de MXCSR se conservan.

### x87 80-bit, PC/RC y pila

- `FCW.PC` de 24, 53 y 64 bits: después de la operación se cuantiza explícitamente la mantisa extendida a la precisión seleccionada, con rounding-to-nearest-even o los modos RC hacia abajo, arriba y cero. Se rechaza PC=01 reservado en lugar de producir un resultado falso.
- La implementación se contrastó con seis estados `fldcw; fld1; flds 2^-25; fadd; fxsave` de una CPU x86 real. Los resultados exactos de FSW y ST0 se incorporan también como *goldens* para correr la misma emulación en un anfitrión ARM64.
- Errores de pila: noveno `FLD1` con pila llena, `FSTP ST0` con pila vacía, tanto enmascarados como desenmascarados. Se modelan `IE`, `SF`, `C1`, `TOP`, `ES/B` y el valor *indefinite* de 80 bits cuando corresponde; comparados con FXSAVE x86 real.
- Se conservan `FWAIT` que entrega `#MF` en la ruta verificada, `FXSAVE/FXRSTOR`, `#NM`, TSS/Ring0/Ring3 e IRETQ de etapas anteriores.

## Evidencia de pruebas

Archivos de prueba en `pc-android/native/tests/`:

| Test | Qué verifica |
|---|---|
| `x86_c09r1_stage4_daz_ftz_test.cpp` | DAZ, FTZ, LDMXCSR, FXSAVE y rechazo de bits reservados |
| `x86_c09r1_stage4_mxcsr_test.cpp` | DAZ y FTZ contra oráculos x86 de bit+status, IE/ZE simultáneos de dos lanes, sNaN y rollback |
| `x86_c09r1_stage4_x87_test.cpp` | PC/RC en seis combinaciones contra hardware; pila llena/vacía con y sin máscara; goldens para ARM64 |
| `x86_c09r1_stage4_sse2_misc_test.cpp` | CMPPD/CMPSD NaNs y predicados, SHUFPD, MOVMSKPD, MOVHPD/MOVLPD, lógica PD |
| `x86_c09r1_stage4_packed_cvt_test.cpp` | CVTDQ2PD/CVTPD2DQ/CVTTPD2DQ, oráculo native x86 y reglas MXCSR DAZ, IE y PE |

### ARM64 nativo

Workflow `.github/workflows/reverse-windows-c09r1-stage4-arm64.yml` usa el runner **`ubuntu-24.04-arm`** de GitHub. Comprueba `uname -m == aarch64`, compila los mismos fuentes C++ y ejecuta instrucciones x86 en el **emulador de Mundo X compilado para ARM64**. Los oráculos hardware inline x86 no se ejecutan en ARM64; sus valores de referencia pertinentes se comparan con goldens incorporados. La prueba no usa QEMU, ningún binario Windows, ni el dispositivo Android del usuario.

El [CI ARM64 anterior](https://github.com/guadalupejuan001-code/mundo-X/actions/runs/43368598615) verificó `242f8369ebed754926e43bd6a07b2a452645de39`. Para verificar la revisión integrada, consulta los checks del SHA correspondiente en la [PR #96](https://github.com/guadalupejuan001-code/mundo-X/pull/96).

El workflow x86 `.github/workflows/reverse-windows-c09r1-stage4-denorm.yml` ejecuta las mismas pruebas con oráculos de hardware y después corre la suite nativa C04–C09R1.

## Bloqueos honestos pendientes

**No se activa CPUID x87/SSE/FXSR/SSE2** porque siguen faltando instrucciones y excepciones de conformidad completa. Esto no equivale a un CPU Windows 10 x64 utilizable todavía.

- Más casos de precisión x87/PC/RC, excepciones múltiples y stack faults en otros opcodes; `FSTP` a memoria sigue teniendo limitaciones de redondeo.
- Aritmética x87 restante usa `long double` de la plataforma; los 6 goldens en ARM64 no prueban todos los valores/intermedios. Falta un oráculo de software 80-bit que no dependa del host para garantizar todas las operaciones.
- SSE2/SSE, MMX, NaNs de todos los opcodes, conversión/DAZ/FTZ en la matriz completa, context switching x86 general, ACPI/temporizadores y la pila gráfica Android siguen incompletos.
- No existe instalador ni Windows 10 arrancado ni benchmark gráfico Android verificado.
- La única mejora evaluada aquí es fidelidad arquitectónica en pruebas concretas; no se atribuyen FPS, compresión ni rendimiento artificial.

Referencias oficiales para MXCSR/DAZ/FTZ y control x87: Intel 64 and IA-32 Architectures Software Developer's Manual (vol. 1 secciones 8, 10 y 11): https://cdrdv2-public.intel.com/789574/253665-sdm-vol-1.pdf

**Estado: etapa 4 implementada como incremento comprobable, NO conformidad x86/Windows completa. PR en borrador, sin merge.**
