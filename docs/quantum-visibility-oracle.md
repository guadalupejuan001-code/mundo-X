# Quantum Visibility Oracle

**Estado:** experimento cuántico real simulado; no activado en el render de Solar.

## Qué hace

Solar necesita decidir qué cuerpos pueden afectar una región de pantalla. La ruta clásica produce una máscara conservadora de cuerpos. Este experimento prueba la hipótesis de reemplazar la búsqueda de candidatos por un registro cuántico pequeño.

`QuantumVisibilityOracle` usa cinco qubits lógicos, por lo que dispone de 32 estados base. Los ids 0..23 representan el catálogo Solar y 24..31 permanecen sin marcar.

La evolución es:

1. preparar superposición uniforme de los 32 ids;
2. aplicar un oracle de fase `U_f|x> = (-1)^f(x)|x>`;
3. aplicar difusión de Grover;
4. repetir el número de iteraciones calculado para la cantidad de estados marcados;
5. observar la probabilidad acumulada del subespacio candidato.

Esto usa superposición, inversión de fase e interferencia. No es simplemente guardar una máscara en qubits.

## Qué NO demuestra

La ejecución continúa ocurriendo sobre CPU clásica. No existe una QPU física.

Además, un render exacto necesita la **lista completa conservadora** de cuerpos que pueden afectar una región. Una observación de Grover devuelve un candidato probable, no la lista completa en una sola consulta. Leer todas las amplitudes del simulador para reconstruir la lista sería una ventaja del simulador, no una operación disponible en hardware cuántico.

Por esa razón el experimento no sustituye todavía a `SolarVisualEther` ni al shader de producción.

## Criterio para conectarlo a Solar

La ruta cuántica sólo puede convertirse en producción si se demuestra a la vez:

- cero omisiones respecto al oracle geométrico exacto;
- cero diferencias de píxel frente al render completo;
- coste total menor, incluyendo preparación, evolución y lectura;
- comportamiento acotado en 4K y 8K;
- ninguna dependencia de inspeccionar amplitudes internas como si fueran RAM.

Si falla el criterio de rendimiento, el resultado sigue siendo útil: demuestra que el cuello de botella debe atacarse antes o después del oracle, no que “cuántico” implique automáticamente una aceleración.

## Implementación

- `native/include/mundo_x/quantum_visibility.hpp`: registro de cinco qubits, oracle de fase y difusión.
- `native/tests/quantum_visibility.cpp`: normalización, amplificación de uno y varios candidatos, rechazo de ids fuera del catálogo y benchmark pareado contra la selección clásica.
- La suite raíz y `quantum-runtime/` ejecutan el test.
- La procedencia está declarada en `tools/cosmic_source_provenance.py`.

El benchmark informa el cociente `quantum_over_classical` pero no impone una victoria artificial. Primero se mide; después se decide si se conecta a Solar.
