# C09R-1 — Estado SIMD parcial, sin ISO de Windows

**Estado:** primera etapa de implementación, no compatibilidad SSE2 completa.

La CPU original de Mundo X incorpora 16 registros XMM de 128 bits, MXCSR con valor de reset 0x1F80 y máscara de bits reservados, conservación de XMM/MXCSR en rollback transaccional de instrucciones e IRQ, y CR4.OSFXSR (bit 9) en la ruta de escritura de CR4. La RAM del invitado sigue siendo MemoryFabric; no se reemplaza por Ether/XI ni por el GPU.

Instrucciones implementadas en esta etapa:
- MOVDQA (66 0F 6F/7F), carga/almacenamiento alineados a 16 bytes.
- MOVDQU (F3 0F 6F/7F), carga/almacenamiento sin requisito de alineación.
- PXOR (66 0F EF), operación XOR byte a byte sobre 128 bits.
- LDMXCSR/STMXCSR (0F AE /2 y /3), rechazo de bits reservados.
- Gating mediante CR4.OSFXSR, CR0.EM y CR0.TS.

El test `x86_c09r1_xmm_subset_test.cpp` comprueba bytes, estados y fallos sin ejecutar Windows. También se conserva la suite C04-C09R anterior. **CPUID SSE2 y x87 siguen desactivados**: aún faltan numerosas instrucciones SSE2, FPU/x87, FXSAVE/FXRSTOR y las pruebas de cambios de contexto. No se debe iniciar la instalación de Windows con esta CPU.

Siguientes etapas de C09R-1:
1. Completar FPU/x87 y su control/status/tag, operaciones básicas, excepciones, estado de pila de 80 bits.
2. Implementar FXSAVE/FXRSTOR 512 bytes y preservar registros/flags/estado en Ring0/Ring3.
3. Ampliar SSE2 entero y escalar con tests independientes, page faults y alineación, y contrastar resultados con un oráculo x86.
4. Solo tras superar las suites y el contrato de sistema, activar CPUID x87/SSE/SSE2.

No se ha utilizado ISO, Windows PE, Windows 10, ni generado una APK. La ejecución en ARM64 no se ha medido.
