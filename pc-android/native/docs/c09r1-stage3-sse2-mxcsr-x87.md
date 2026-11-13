# C09R-1 · Etapa 3 — SSE2 binary64, MXCSR y precisión x87

Fecha: 2026-11-12/13. Base: PR #94; **esta PR no fusiona nada**.

## Contrato

Toda la ISA se ejecuta dentro de `cosmic_pc::Cpu`, procesador original de Mundo X. Ninguna operación de CPU se sustituye por GPU Virtual, Ether/XI, QSSD, PyTorch o un hipervisor ARM64. El estado XMM/MXCSR/x87 y su reversión ya forman parte de `Cpu::Snapshot`; se mantienen todos los registros y las mismas rutas MMU/segmentación/IDT de C04–C09R etapa 2.

**No se ejecutó Windows 10 ni su ISO.** El código de prueba son opcodes x86 propios y la comparación independiente es hardware x86 del runner GitHub Actions.

## Nuevas funciones, dentro de su alcance de prueba

### SSE2 binary64

- **Escalar F2:** `MOVSD` carga/store y altas de XMM conforme al tipo de fuente; `ADDSD`, `SUBSD`, `MULSD`, `DIVSD`, `SQRTSD`, `MINSD`, `MAXSD`. El resultado cambia solo los 64 bits inferiores y conserva los superiores; cuando corresponden NaNs/ceros negativos, MIN/MAX selecciona la fuente según el contrato del opcode.
- **Empaquetado 66:** ADDPD/SUBPD/MULPD/DIVPD/SQRTPD/MINPD/MAXPD en dos lanes de 64 bits; no se confirma el destino antes de las comprobaciones de excepciones.
- **Conversiones:** `CVTSI2SD` signed32/64, `CVTSD2SI` y `CVTTSD2SI`, redondeo según `MXCSR.RC` o truncado, límites de entero indefinido y bit PE/IE cuando corresponde.
- **Comparaciones:** `COMISD` y `UCOMISD` modifican CF/ZF/PF y limpian OF/SF/AF. Se prueba NaN silencioso en UCOMISD frente a COMISD.
- **MXCSR:** estado sticky IE/DE/ZE/OE/UE/PE a partir de la operación IEEE del anfitrión, `FE*` y controles de enmascaramiento. CR4.OSXMMEXCPT (bit10) permite entregar un fallo SIMD desenmascarado por **#XM vector 19**; si está a cero se entrega **#UD vector 6**; ambos preservan flags MXCSR pese al rollback de la instrucción fallida. Se conservaron `CR4.OSFXSR`, `CR0.TS/EM`, FXSAVE/FXRSTOR y las pruebas anteriores.
- El `HostRound` guarda/restaura el entorno de redondeo de la CPU anfitriona durante cada operación, sin dejar el proceso anfitrión con MXCSR modificado. Los operandos volátiles obligan a ejecutar operaciones en tiempo de ejecución. La compatibilidad exacta ARM64 requiere pruebas propias: no se afirma conformidad completa solo por coincidir con una CPU x86 del runner.

### x87 de 80 bits

- `FLD m32real` y `FLD m64real` ahora convierten **directamente los bits IEEE754 a 80 bits**, sin redondeo intermedio `long double`. Esto preserva exactamente signo, normal/subnormal, infinito y los bits de mantisa finita. Un sNaN cuyo manejo aún no es completo falla expresamente en lugar de inventar resultado.
- Extendidos `FADD/FMUL/FSUB/FDIV ST0,STi` con el estado de control default (precisión extendida/round nearest), banderas de excepción básicas y destino sin modificar ante excepción desenmascarada.
- Las excepciones x87 desenmascaradas permanecen pendientes en FSW (ES/B). `FWAIT` las entrega como **#MF vector 16** cuando CR0.NE está activo; `FNCLEX` limpia el estado y `IRETQ` permite reintentar FWAIT. Si la vía legacy FERR#/IRQ13 no está modelada se informa un error explícito, no un manejador falso.
- Sigue existiendo el trabajo de C09R-1 etapa 2: FX(SAVE/RSTOR) de 512 bytes con TOP y FTW físicos, ST0–ST7 lógicos, FIP/FDP de 64 bits, XMM0–15, MXCSR, prueba de 2 contextos y #NM/CLTS/IRETQ.

## Evidencias reproducibles

- `pc-android/native/tests/x86_c09r1_stage3_fp_test.cpp`: **54 checks**; ADD/SUB/MUL/DIV/SQRTSD, packed FP, conversiones y MXCSR.RC, COMISD/UCOMISD, NaN, comparación con hardware x86 real, #XM→IDT19→IRETQ y #UD6 cuando OSXMMEXCPT=0; FXRSTOR tras un fallo SIMD.
- `pc-android/native/tests/x86_c09r1_stage3_ring3_test.cpp`: **13 checks**; el huésped ejecuta `IRETQ` a Ring3, `ADDSD`, `INT 0x80`, entra a Ring0 con pila TSS, guarda por FXSAVE64, ejecuta PXOR+FNINIT, restaura por FXRSTOR64, retorna por IRETQ a Ring3, ejecuta `MULSD` y termina con un `INT 0x81`/HLT real.
- `pc-android/native/tests/x86_c09r1_stage3_x87_precision_test.cpp`: **231 checks**; diez valores binarios de 64 bits y diez de 32 bits incluyendo mínimos subnormales, signo cero y extremos finitos. Compara los **80 bits de ST0** contra resultados auténticos de `fldl/flds; fxsave` del runner x86. También prueba rechazo sin commit de un sNaN pendiente.
- `pc-android/native/tests/x86_c09r1_stage3_x87_mf_test.cpp`: **12 checks**; estado ZE en FDIV por cero enmascarado y oráculo x87 nativo; cuando se desenmascara, prueba FSW ES/B/ZE, #MF al ejecutar FWAIT y recuperación mediante un manejador real FNCLEX/IRETQ.
- La suite nativa completa `pc-android/tools/test-native.sh` preserva los tests C04–C09R anteriores (196 FPU/FXSAVE etapa 2, 166 SSE2 packed, 56 XMM/MXCSR, 45.274 aserciones x64, etc.).
- La CI `.github/workflows/reverse-windows-c09r1-stage3-fp.yml` ejecuta **todos** los tests de arriba (no solo los compila) y requiere cada marcador PASS; los logs se guardan como artefactos.

## Límites y honestidad arquitectónica

1. **CPUID todavía anuncia x87=false, FXSR=false y SSE2=false.** Esta fase no cumple toda la semántica SSE2 de NaNs, denormales/DAZ/FTZ, todas las conversiones, excepciones combinadas ni todas las instrucciones SIMD. Aún faltan más oráculos y pruebas ARM64 para garantizar el redondeo host-agnóstico.
2. Las operaciones x87 de precisión extendida usan `long double` del anfitrión para cierta aritmética. Se rechazan modos de precisión/redondeo x87 todavía no modelados; el conjunto completo de operaciones x87, cargas/restauraciones legacy, y todas las condiciones de excepción **no** están completas.
3. La prueba Ring3/Ring0 con FXSAVE demuestra solo ese camino: no es prueba de aislamiento multitarea Windows ni conformidad global Intel.
4. Faltan SSE2 complejo, MMX, context switching de kernel más amplio, más MMU/ACPI y el renderizador Android. No existe prueba de Windows 10 con esta CPU.
5. Las funciones existentes de Ether/XI, GPU Virtual, QSSD y PCI ATA no se alteran en Mundo X, y su integración la confirma la regresión de REVERSE Windows.

Referencia primaria: Intel® 64 and IA-32 Architectures Software Developer's Manual, volumen 1 (SSE2 y MXCSR) y volumen 2 (instrucciones):
https://cdrdv2-public.intel.com/874240/325462-090-sdm-vol-1-2abcd-3abcd-4.pdf

**C09R-1 etapa 3: incremento probado en escenarios concretos, no finalización del conjunto completo de compatibilidad Windows. No fusionar automáticamente.**
