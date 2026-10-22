# MIL Core × Fusion 3D — evolución radial por puertas cuánticas (v1.2)

**Alcance verificado:** evolución unitaria de un Hamiltoniano radial tridiagonal de **4 u 8 posiciones**, codificado respectivamente en **2 o 3 qubits lógicos** de \`ReducedExactTRepresentation\` (el motor MIL original de Mundo X). Cálculo ejecutado en CPU clásica por software; **no es una QPU física**.

El Hamiltoniano de referencia de 256 posiciones **no se ha ejecutado completo**. Se usa la **misma definición matemática de \`V_C+V_N\`**, discretizada aparte en una malla gruesa de longitud fija L=14 fm. La diferencia entre ambas discretizaciones es un error de modelo considerable que no se elimina con más pasos Trotter.

## 1. Operador

Para un par de isótopos documentado (D–D, D–T, D–He³, D–Li⁶) y potencial radial \`V_C(r)+V_N(r)\`, el operador para \`N=2^n\` sitios se construye con diferencias finitas:

\`\`\`
r_j = (j+1) Δr,   Δr = L/(N+1),   L = 14 fm
g = (ℏc)^2 / (2 μc² Δr²)

H[j,j] = 2g + V_C(r_j) + V_N(r_j)
H[j,j+1] = H[j+1,j] = -g
H[j,k] = 0 si |j-k|>1
\`\`\`

Es **real, simétrico, tridiagonal** y con fronteras Dirichlet \`u(0)=u(L)=0\`. Es la misma fórmula de \`RadialHamiltonian\`, pero la instancia **N=256, L=96 fm** sigue siendo el archivo CSV independiente, sin evolución.

La condición de contorno y el tamaño reducido **NO son un tratamiento de dispersión entrante/saliente ni un modelo de sección eficaz de fusión**.

## 2. MIL Core no recibe una matriz simulada de antemano

La función nativa \`MilRadialEvolution::execute\` toma los coeficientes de la matriz tridiagonal pequeña y hace:

1. **Descomposición de Pauli:** \`H = Σ_P c_P P\` con \`c_P = Tr(PH)/N\`. Recompone toda la matriz para verificar que no se omitió ningún término distinto de error de punto flotante.
2. **Inicialización:** el estado localizado \`|j₀>\` se prepara directamente en **MIL ReducedExactTRepresentation** usando puertas X.
3. **Evolución mediante puertas:** una fórmula palindrómica Suzuki/Strang de segundo orden aplica las exponentiales \`exp(-i c_P P τ)\`. Las rotaciones de Pauli se sintetizan en **H, S, CNOT y puertas de fase arbitraria** del motor propio. El término identidad sólo provoca fase global y se omite.
4. **Lectura de pequeñas amplitudes:** el sistema lee 4 u 8 amplitudes finales de MIL exclusivamente para mostrar probabilidades y comparar. No utiliza una matriz densa para producir el estado MIL.
5. **Oráculo separado:** una referencia **Runge–Kutta 4** integra de forma independiente la matriz H original pequeña, sin descomposición de Pauli ni puertas. La comparación se hace por fidelidad insensible a fase global y distancia L1 entre probabilidades.

La fórmula de evolución implementada es:

\`\`\`
τ = t / ℏ                       [MeV⁻¹]
U(τ) = exp(-i H τ)

S₂(τ/r) = [ ∏_{P≠I} exp(-i c_P P τ/(2r)) ]
          [ ∏_{P≠I en orden inverso} exp(-i c_P P τ/(2r)) ]

|ψ_MIL(τ)> ≈ [S₂(τ/r)]^r |j₀>,   r = 1..24
\`\`\`

El tiempo físico es \`t [s] = τ × 6.582119569×10⁻²²\`. Un ejemplo \`τ=0.04 MeV⁻¹\` representa \`2.63×10⁻²³ s\`.

En cada aplicación de \`e^{-i c_P P Δτ}\`, MIL transforma los ejes X/Y a Z, crea la paridad de la cadena con CNOT, aplica \`phase(target, 2c_PΔτ)\` y deshace las puertas. **El circuito no es un gráfico ficticio:** utiliza directamente el backend \`ReducedExactTRepresentation\` de Mundo X.

- **Tolerancia de reconstrucción de H:** 1e-9 relativa a escala de matriz.
- **Presupuesto:** 4 u 8 posiciones, ≤24 pasos, ≤15.000 puertas por llamada, máximo 1.024 componentes de MIL, \`τ≤0.12 MeV⁻¹\`.
- **Componentes no truncados:** si el límite se supera, hay error; no se sustituye el resultado por el oráculo RK4.
- **El frontend Android:** ejecuta el cálculo explícitamente al tocar el botón, en un hilo de trabajo, sin bloquear la interfaz.

## 3. Qué significa la precisión

La fidelidad mide la concordancia del resultado MIL con la solución RK4 del **mismo Hamiltoniano discretizado reducido**:

\`\`\`
F = |<ψ_RK4|ψ_MIL>|² / (||ψ_RK4||² ||ψ_MIL||²).
\`\`\`

Una fidelidad alta sólo valida que las puertas MIL aplicaron aproximadamente la evolución deseada, **no que el potencial Woods–Saxon, la discretización o las reacciones predigan correctamente un experimento nuclear**.

Hay por lo menos cuatro errores independientes: potencial nuclear sin ajuste experimental, discretización radial de 4/8 puntos, aproximación Suzuki–Strang de pasos finitos, y redondeo numérico en MIL/RK4. Aumentar los pasos reduce sólo una parte de ellos.

## 4. Cómo usarlo en Fusion 3D

1. Abre \`Mundo X Fusion 3D\`, elige **²H + ³H** y revisa los isótopos.
2. En **Interacción nuclear y túnel**, ajusta \`V₀\` y observa la barrera WKB (esta es otra aproximación).
3. Ve a **Evolución unitaria en MIL Core**.
4. Elige **3 qubits / 8 posiciones** o **2 qubits / 4 posiciones**.
5. Ajusta \`τ=t/ℏ\` y pulsa **Ejecutar evolución MIL Core**.
6. Observa la norma, fidelidad, distancia de probabilidades y las barras de sitio radial (verde MIL; naranja RK4).
7. El **informe científico** incluye los resultados si ya fueron calculados. El botón **Exportar Hamiltoniano radial .CSV** sigue guardando la malla grande de 256 puntos, no la usada por el circuito pequeño.

## 5. Qué falta para la física nuclear avanzada

- Dispersión entrante/saliente o condiciones absorbentes físicamente coherentes, energía dependiente y acoplamiento entre canales.
- Potencial nucleus–nucleus ajustado a secciones eficaces y datos de reacción; correcciones para espín, estructura, polarización y núcleos compuestos.
- Errores de malla frente a refinamientos más allá de 8 posiciones.
- Soporte escalable para \`N=256\` (8 qubits, pero con muchos más términos/puertas de un Hamiltoniano general) y verificación de límites de RAM, crecimiento de componentes y error Trotter.
- Implementación y benchmarking en hardware cuántico real, si fuera relevante.
- Comparación de observables físicos medidos, no solamente de la misma ecuación resuelta de dos maneras.

**Esta fase no fabrica ni descubre elementos nuevos, ni predice probabilidades de fusión.**

## 6. Documentación y tests

Código de MIL: \`native/include/mundo_x/mil_radial_evolution.hpp\`.  
Tests C++ (MIL real y RK4 separado): \`native/tests/mil_radial_evolution.cpp\`.  
JNI Android: \`fusion-android/app/src/main/cpp/mil_radial_jni.cpp\`.  
Contrato Android: \`MilRadialEvolver.java\` y \`MilProbabilityView.java\`.  
Suite: \`.github/workflows/fusion-standalone-android.yml\`.

Referencias para la técnica de evolución cuántica: [IBM Quantum, product formulas/Trotter](https://qiskit.qotlabs.org/docs/tutorials/multi-product-formula). Para potenciales y límites físicos, consulta \`NUCLEAR_INTERACTION.md\`.
