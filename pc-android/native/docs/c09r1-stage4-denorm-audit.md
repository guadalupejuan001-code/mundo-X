# C09R-1 etapa 4 — Auditoría de conformidad, primer incremento DAZ/FTZ

Esta rama continúa Mundo X PR #95. **No se ha ejecutado Windows ni su ISO**.

## Hallazgo principal

El estado previo tenía MXCSR.FTZ (bit15) y el campo de máscara que rechazaba DAZ (bit6), aunque un invitado que use SSE2 puede configurar DAZ. También existían conversiones y comparaciones que consumían subnormales sin respetar DAZ. La implementación de esta etapa permite DAZ en MXCSR, lo anuncia en el campo MXCSR_MASK de FXSAVE y aplica el reemplazo de entradas subnormales por cero con signo a operaciones FP64, COMISD/UCOMISD y conversiones a entero. El estado previo FTZ de salida se conserva.

Se añadió `x86_c09r1_stage4_daz_ftz_test.cpp` con instrucciones x64 auténticas: DAZ encendido/apagado, resultado de `0+tiny`, bandera DE, resultado bajo FTZ, LDMXCSR/FXSAVE y rechazo de bits reservados. Se añadió una puerta específica en GitHub Actions y a la suite nativa original.

## Límites de conformidad (no resueltos)

**Este incremento NO completa SSE2 ni x87**. Persisten huecos de opcodes, conversiones y manejo de NaN, combinaciones de excepciones MXCSR, precisión de `long double` en ARM64, redondeo x87 PC/RC, stack faults y la cobertura completa de Ring0/Ring3. No se habilitan bits CPUID SSE2/x87/FXSR. La prueba DAZ/FTZ es limitada y debe ampliarse con oráculos de hardware x86 y, si hay runner ARM64, pruebas de la misma ISA emulada compilada allí.

## Reglas

- CPU canónica original Mundo X; ninguna segunda CPU o FPU.
- No modificar QSSD/PCI ATA/IRQ14, GPU Virtual, Ether/XI ni REgraphics por una mejora SIMD aislada.
- Fijar SHA exacto en REVERSE Windows únicamente tras superar la suite y verificar todas las regresiones.
- No declarar instalación de Windows 10 lista, ni abrir o ejecutar ISO.
