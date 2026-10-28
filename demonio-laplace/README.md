# DEMONIO DE LAPLACE DIGITAL — Ether/XI v0.1

## Meta única

Construir un **mundo digital que evoluciona por leyes verificables y un observador que calcula su futuro a partir de su estado completo**. Este es un primer prototipo del demonio de Laplace. No contiene trading, no aplica la ecuación diferencial espacial de Laplace, no integra chatbots y no añade interfaces.

Fórmula: S(t+n) = F^n(S(t)).

Alcance: un mundo cerrado, sintético y completamente conocido. No se afirma predecir el universo físico, los mercados o una causa desconocida.

## Arquitectura Ether real

1. **Autoridad del estado:** el QuantumRuntime original de Mundo X, con InfiniteCore, CompactCore e InverseMemory; no se crea otro simulador. Se mantienen REVERSE y las APK sin modificar.
2. **Fondo:** todos los qubits lógicos comienzan en |0>. Solo los cambios se materializan. Dieciséis celdas forman un anillo lógico, con algunas direcciones decimales de más de 100 dígitos que no requieren materializar sus intermedias.
3. **Leyes artificiales:** dos capas reversibles de puertas CNOT: primero 0→1, 2→3, ..., 14→15; después 1→2, 3→4, ..., 15→0. Las reglas se especifican por adelantado.
4. **Observación:** cada probz consulta el estado XI. No se alteran los qubits al observar probabilidades.
5. **Comprobación externa:** un oráculo Booleano implementado con operaciones de bits en JavaScript compara cada instante. JavaScript solo genera instrucciones XI y verifica; el núcleo original ejecuta las puertas y las consultas.
6. **Intervención desconocida:** en t=9 se introduce una puerta X en la celda 3 que el pronosticador no conoce. La predicción anterior debe fallar desde ese momento. Con el evento conocido, vuelve a resultar exacta.
7. **Reversibilidad:** la secuencia de puertas se invierte, se comprueban los 16 bits, se restablece el fondo y se compactan componentes.
8. **Incertidumbre cuántica:** un estado Bell presenta probabilidades locales 0.5 aunque la correlación ZZ sea +1. No se afirma conocer cada resultado de medición.

Todo el estado cuántico es una simulación realizada en una CPU clásica. No son qubits físicos. Los códigos nuevos son un adaptador de órdenes y un auditor Node sin un nuevo motor de estados.

## Reproducción

Desde la raíz del repositorio:

    cmake -S quantum-runtime -B quantum-build -DCMAKE_BUILD_TYPE=Release
    cmake --build quantum-build --target mundo_x_quantum --parallel 2
    node demonio-laplace/experimento.mjs ./quantum-build/mundo_x_quantum ./demon-evidence

El reporte demon-evidence/resultado.json contiene 33 estados observados, mediciones del núcleo, comparación de predicciones y el hash de los programas XI. Los cinco programas ejecutados quedan guardados para auditoría.

La automatización .github/workflows/demonio-laplace-ether.yml ejecuta este experimento con el núcleo real y exige resultados idénticos entre dos sesiones nuevas.

## Siguiente etapa, mismo objetivo

Extender gradualmente el universo digital para modelar partículas, fuerzas, interacciones y estados iniciales incompletos. Para cada nueva ley debe existir una predicción contrastable. El experimento actual demuestra únicamente la estructura elemental del demonio digital: estado + leyes → futuro comprobable, y su límite frente a causas desconocidas.
