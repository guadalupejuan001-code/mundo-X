# Mundo X Fusion 3D v1.3 — MIL Core: 256 posiciones radiales

**Resultado verificado en GitHub Actions (Ubuntu x86_64, 2026-10-22):** la implementación original de **MIL ReducedExactTRepresentation** ejecutó una evolución unitaria de un Hamiltoniano radial **tridiagonal de 256 posiciones, codificado en 8 qubits**, mediante descomposición Pauli estructurada y fórmula Suzuki–Strang. Es simulación cuántica por software clásico, no ejecución en hardware cuántico físico.

No se utilizaron 256 qubits; la codificación binaria de 256 posiciones requiere sólo **8 qubits**. MIL conserva soporte para más qubits lógicos, pero eso no elimina el coste general de representar estados arbitrarios ni garantiza velocidad.

## Qué estaba limitando la versión 1.2

- La app estaba *artificialmente* restringida a 2–3 qubits (4–8 sitios) para garantizar un primer cálculo contrastado.
- Su planificador original descomponía el pequeño Hamiltoniano enumerando todas las cadenas de Pauli, lo cual no es una estrategia eficiente para n=8.
- La restricción de malla y de tiempo venía del **software de la aplicación**, no de un límite intrínseco de tres qubits de MIL Core.
- Aún debía controlarse el crecimiento de componentes, puertas, tiempo y precisión en una malla mayor.

## 1. Descomposición exacta especializada para matriz tridiagonal

Para \`N=2^n\` sitios radiales con potencial diagonal arbitrario \`d_j\` y acoplamiento real **UNIFORME** \`g\` entre vecinos:

\`\`\`
H = Σ_j d_j |j><j| + g Σ_{j=0}^{N-2} (|j><j+1| + |j+1><j|)
\`\`\`

El bloque diagonal se transforma en un desarrollo de **como máximo N términos Z** usando una transformada Walsh–Hadamard de coste \`O(N log N)\`. El bloque de acoplamiento vecino produce exactamente

\`\`\`
1 + 2 + 4 + ... + 2^(n-1) = N−1
\`\`\`

cadenas de Pauli del tipo X/Y, mediante los patrones de acarreo de enteros binarios consecutivos. Sólo sobreviven cadenas de paridad Y compatible con un Hamiltoniano real simétrico.

Por tanto, para este operador uniforme:

\`\`\`
Pauli_terms ≤ N + (N−1) = 2N−1
N = 256 -> máximo 511 términos
\`\`\`

La implementación \`fusion-android/native/mil_radial_scaling.hpp\` compara todas las entradas de la matriz H original contra la reconstrucción mediante Pauli. Para n=8 comprobó las **65.536 entradas** con un error máximo ~8×10⁻¹⁵ en el caso de prueba. No es una aproximación que descarte términos significativos.

**No** debe extrapolarse este conteo como garantía para matrices arbitrarias densas, acoplamientos no uniformes o Hamiltonianos nucleares de muchos cuerpos.

## 2. Evidencia del motor cuántico

El programa \`fusion-android/tests/mil_radial_scalability.cpp\` ejecutó H para dimensiones ascendentes y contrastó su estado final MIL con una referencia RK4 independiente. El caso de N=256 usó el potencial **fenomenológico radial** D–T en un dominio L=96 fm, un estado inicial localizado próximo a la escala de contacto y un **paso** de evolución Suzuki–Strang.

| Qubits | Posiciones | Cadenas Pauli | Puertas MIL | Componentes pico | Tiempo de puertas (CI) |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 2 | 4 | 7 | 241 | 4 | 0,29 ms |
| 3 | 8 | 15 | 881 | 8 | 1,77 ms |
| 4 | 16 | 31 | 2.609 | 16 | 9,75 ms |
| 5 | 32 | 63 | 3.481 | 32 | 23,5 ms |
| 6 | 64 | 127 | 4.365 | 64 | 50,2 ms |
| 7 | 128 | 255 | 10.509 | 128 | 236 ms |
| 8 | 256 | 511 | 24.590 | 256 | 1.059 ms |

Los tiempos provienen de **un runner Ubuntu GitHub Actions**; no se pueden asumir en un teléfono Android ni equivalen a una mejora frente a hardware cuántico o bibliotecas especializadas.

Resultado n=8:

\`\`\`
qubits = 8
sites = 256
pauli_terms = 511
Strang steps = 1
MIL gates = 24590
maximum Reduced components = 256
measured gate time = 1059 ms (GitHub runner)
fidelity vs independent sparse RK4 = 0.99999999676
probability L1 = 8.61208e-6
MIL norm = 1
RK4 norm = 1
tau = 0.00048379370083 MeV^-1
\`\`\`

La fidelidad alta demuestra que en **esta duración corta** el circuito de MIL reproduce la evolución de la matriz discretizada. No demuestra fidelidad alta a tiempos largos ni convergencia de la discretización espacial.

## 3. Android 1.3

El mismo backend se conecta al botón **Ejecutar evolución MIL Core**, sin interferir con Ether Render X ni con el resto de la app.

- Selector 2–8 qubits; **8 qubits / 256 posiciones** como valor inicial.
- Las escalas 4/8 sitios preservan el piloto 1.2 de dominio L=14 fm; los casos 16..256 usan el dominio de investigación L=96 fm.
- La app reduce la duración \`tau\` al aumentar el valor de \`|g|\`, limitando la fase cinética \`|g tau|\`. Esto evita usar el mismo tiempo absoluto con mallas cuya energía cinética crece como \`1/(Δr)^2\`.
- Pasos Strang limitados por tamaño: 12 para el piloto (2–3 qubits), 4 para 16 sitios, 2 para 32 y 1 para 64–256.
- Ejecuta en hilo de trabajo, valida presupuesto máximo de 1.024 componentes y 200.000 puertas, y nunca utiliza RK4 como reemplazo silencioso del cálculo MIL.
- En 256 sitios, el gráfico suma barras en **16 bloques de 16 posiciones** para mantener legibilidad y conservar la probabilidad. Los datos completos quedan en el estado exportable; el dibujo no interpola ni inventa valores.
- El informe distingue dominio físico, tiempo, números de puertas, fidelidad y límites del potencial.

## 4. Lo que falta

Este es un logro de **ingeniería de simulación cuántica por software**, no una simulación nuclear completa:

- El potencial Woods–Saxon sigue sin calibrarse con datos experimentales D–T.
- La malla de 256 sitios es una aproximación **radial de una sola coordenada l=0**, no el movimiento relativo de todos los protones y neutrones en un modelo de muchos cuerpos.
- La frontera Dirichlet es reflectante; faltan estados entrantes/salientes, canales de reacción y amplitudes de dispersión.
- No se predicen secciones eficaces, tasas de fusión, nuevos elementos ni procedimientos físicos de fusión.
- Ampliar de 8 a 1000 qubits de ancho **no** implica capacidad ilimitada para Hamiltonianos generales, porque el crecimiento de componentes, la profundidad de circuito y los errores seguirían siendo importantes.
- Un paso Strang prueba un punto corto de evolución; hacen falta barridos de tiempo, refinamiento temporal/espacial y perfiles de RAM/RSS en Android para determinar límites prácticos.

## Referencia externa

Arseniev y colaboradores, *Tridiagonal matrix decomposition for Hamiltonian simulation on a quantum computer*, Physical Review A **109**, 052629 (2024). DOI: https://doi.org/10.1103/PhysRevA.109.052629.

El trabajo estudia estructurar matrices tridiagonales en cadenas de Pauli; el algoritmo de nuestro experimento incorpora por separado una fórmula específica para **acoplamiento uniforme de primeros vecinos**. La referencia no es una afirmación de novedad científica de Mundo X.

**Código:** \`fusion-android/native/mil_radial_scaling.hpp\`.  
**Verificación:** \`fusion-android/tests/mil_radial_scalability.cpp\`; \`.github/workflows/mil-radial-scalability.yml\`.  
**Android:** \`fusion-android/app/src/main/cpp/mil_radial_jni.cpp\` y \`MilRadialEvolver.java\`.
