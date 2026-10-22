# Mundo X Fusion 3D — Núcleo de interacción nuclear v1.1

**Estado:** implementación calculable y reproducible de un *modelo fenomenológico*, no una predicción validada de tasas de fusión. El código ejecuta las integrales de forma clásica en el teléfono. No altera el diseño ni los pesos de MIL Core. Este modelo prepara un operador Hamiltoniano discreto para integrar a futuro, cuando exista una evolución cuántica con pruebas comparativas.

## 1. Modelo con unidades físicas

Para dos núcleos con números atómicos \`Z₁, Z₂\` y masas isotópicas \`m₁, m₂\`, la energía cinética relativa es \`Ecm\` (MeV) y la masa reducida se calcula como \`μc² = m₁m₂/(m₁+m₂) × 931.49410242 MeV/u\`. Se usan las masas neutras de los isótopos D-2, T-3, He-3 y Li-6 documentadas en NIST; aproximarlas a A unidades alteraría la masa reducida, por eso el cálculo usa masas isotópicas declaradas.

La coordenada **r** mide separación entre centros de dos núcleos, en **femtómetros** (fm). Adoptamos exclusivamente el canal radial \`l=0\` y el potencial central efectivo:

\`\`\`
V(r) = V_C(r) + V_N(r)

             { k_e Z₁ Z₂ / r,                         r >= R
V_C(r) =     {
             { k_e Z₁ Z₂ (3 - r²/R²) / (2R),          r < R

V_N(r) = -V₀ / [1 + exp((r-R)/a)]

R   = 1.2 fm · (A₁^(1/3) + A₂^(1/3))
a   = 0.65 fm
k_e = 1.4399764 MeV·fm
V₀  = 35 MeV inicialmente; control interactivo 20..60 MeV
ħc  = 197.3269804 MeV·fm (NIST CODATA)
\`\`\`

La **atracción Woods–Saxon** se emplea aquí como una hipótesis *nucleus–nucleus* para explorar matemáticamente la barrera. Es un parámetro **sin calibración** contra datos D–T / D–D / D–He3 / D–Li6, no un potencial microscópico derivado de interacciones nucleón–nucleón. El radio de contacto, la distribución de carga y la profundidad son aproximaciones. No debe extrapolarse a elementos superpesados ni a reacciones no tabuladas.

## 2. Cálculo cuántico aproximado: penetración sub-barrera WKB

Cuando el potencial presenta máximo \`V_peak\` y el estado tiene \`Ecm < V_peak\`, encontramos *dos puntos de retorno clásicos*:

\`\`\`
V(r₁) = Ecm       V(r₂) = Ecm       r₁ < r_peak < r₂
\`\`\`

Luego integramos la acción prohibida mediante cuadratura numérica de Simpson transformada (2.048 / 4.096 paneles y, si la convergencia lo requiere, 8.192):

\`\`\`
S(E) = ∫[r₁,r₂] sqrt(2 μc² · (V(r) − Ecm)) / (ħc) dr
P_WKB = exp(-2S)
log10(P_WKB) = -2S / ln(10)
\`\`\`

La transformación \`r = r₁ + (r₂−r₁) sin²(πt/2)\` distribuye puntos cerca de ambos puntos de retorno. Los signos, límites y dimensiones se comprueban; los resultados no convergentes se rechazan.

**P_WKB es un factor semiclasico de penetración de la barrera del modelo, NO una probabilidad medida de fusión.** No multiplica un S-factor, no resuelve la fuerza tensorial ni absorción nuclear, no determina secciones eficaces, ni representa dinámica nuclear de muchos cuerpos. Para \`Ecm >= V_peak\`, se muestra «WKB sub-barrera no aplicable», **no** «100 % de fusión».

### Oráculo de verificación independiente

Para el potencial de Coulomb puro \`V_C(r) = k_e Z₁ Z₂/r\` desde un radio finito \`R\`, la acción tiene solución analítica:

\`\`\`
η = [k_e Z₁ Z₂ / (ħc)] sqrt(μc² / (2 Ecm))
x = R Ecm / (k_e Z₁ Z₂)

log10(P_Coulomb,R) =
 -2 π η / ln(10) · [1 − (2/π) · (asin(sqrt(x)) + sqrt(x(1−x)))]
\`\`\`

El límite \`R→0\` da el factor Gamow \`exp(−2πη)\`. Una segunda integral numérica de Coulomb puro reproduce el oráculo analítico sobre varios valores de energía. Esta prueba valida la integración y las dimensiones; **no** valida el potencial nuclear fenomenológico.

### Incertidumbre y sensibilidad

El botón «Sensibilidad V0» calcula el mismo par y \`Ecm\` con tres profundidades 25, 35 y 45 MeV. Son **variantes del modelo**, no intervalos de confianza experimentales. Si las diferencias son significativas, queda patente que la respuesta no debe presentarse como predicción absoluta.

## 3. Hamiltoniano radial discreto para investigación posterior

Definimos el operador radial de Schrödinger:

\`\`\`
H u(r) = -(ħc)²/(2 μc²) · d²u/dr² + V(r)u(r)
\`\`\`

Discretizamos \`u(0)=u(L)=0\` (Dirichlet) sobre **N=256** posiciones internas con \`L=96 fm\` y \`Δr=L/(N+1)\`. En cada punto interno \`rᵢ=(i+1) Δr\`:

\`\`\`
t = (ħc)² / (2 μc² Δr²)
Hᵢᵢ = 2t + V_C(rᵢ) + V_N(rᵢ)
Hᵢ,ᵢ₊₁ = Hᵢ₊₁,ᵢ = -t
\`\`\`

Se implementa una **matriz REAL, SIMÉTRICA, TRIDIAGONAL** con almacenamiento O(N), multiplicación vectorial y energía de Rayleigh. Es exportable en CSV (puntos, Coulomb, nuclear, total y coeficientes), sin expandirla a 256×256 elementos densos.

La discretización es **un operador**, no una solución de dispersión. Dirichlet en un dominio finito no incorpora condiciones de onda entrante/saliente, absorción ni sectores de reacción. **MIL Core todavía no ejecuta H ni propaga u(t)**: faltan codificación en qubits, evolución unitaria, control de discretización, pruebas de conservación de norma, estados de frontera y validación frente a referencia clásica. Así se preserva la arquitectura original de MIL Core sin atribuirle funcionalidad inexistente.

## 4. Qué se prueba

- Conservación Z y A y Q de cinco canales de referencia, sin crear energía Q para combinaciones no tabuladas.
- Potencial Woods–Saxon con cola corta, repulsión de Coulomb y potencial total \`V_C+V_N\`.
- Raíces \`r₁<r_peak<r₂\` y signo de \`S(E)\`, convergencia de integración.
- WKB Coulomb puro numérico contra la **expresión analítica** para distintas energías.
- Monotonía sub-barrera cuando aumenta \`Ecm\`; variación al cambiar \`V0\`.
- Rechazo de entradas fuera de presupuesto, parejas no tabuladas y energías sin dominio WKB.
- **Hamiltoniano simétrico**: \`<a,Hb>=<Ha,b>\`. Caso libre con modo seno y autovalor de Laplaciano de diferencias finitas (oráculo analítico), aplicado sin construir matriz densa.
- Verificación de que el panel de Android contiene resultados numéricos y el CSV de H, además de que el mismo Ether Render X genera un fotograma real.

## 5. Qué no se afirma

No existe aquí teletransmutación, síntesis atómica en el teléfono, detección de elementos nuevos, simulación cuántica de las trayectorias reales de nucleones, predicción de reacciones arbitrarias, secciones eficaces, rendimiento de un reactor ni aceleración probada por MIL Core. La animación 3D sigue siendo **ilustrativa**. La estructura nuclear real requiere espín, potenciales efectivos específicos, canales acoplados, propagación y comparación con experimentos.

## Referencias principales

- [NIST CODATA: ħc en MeV·fm](https://physics.nist.gov/cgi-bin/cuu/Value?hbcmevf)
- [NIST Atomic Weights and Isotopic Compositions](https://physics.nist.gov/cgi-bin/Compositions/stand_alone.pl?all=all&ascii=ascii&ele=&isotype=all)
- [NuclearTalent, Woods–Saxon, significado y limitaciones](https://nucleartalent.github.io/NuclearStructure/doc/pub/spdata/html/spdata.html)
- [Phys. Rev. C 69, 024611: potencial empírico nucleus–nucleus y límites de su ajuste](https://journals.aps.org/prc/abstract/10.1103/PhysRevC.69.024611)
- [Coulomb/WKB penetrability en modelos de fusión nuclear](https://hepnp.ihep.ac.cn/article/doi/10.1088/1674-1137/ad1a97)

**Revisión de seguridad:** cálculos locales; rango energía 0.1–10,000 keV, profundidad V0 20–60 MeV, matriz N=256 (máximo 512), sin permisos Internet ni control de material o dispositivos físicos.
