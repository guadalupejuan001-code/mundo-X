# MUNDO X · Fusion 3D (APK independiente)

**Nombre de Android:** Mundo X Fusion 3D  
**Paquete:** `com.mundox.fusion3d`  
**Versión:** 1.4.0  
**Sin conexión, sin permisos de red, sin acceso general a archivos.**

Este módulo construye una APK **independiente** de `COSMIC X PC`, `MUNDO X Desktop`, `Solar` y de otras apps. Su interfaz está diseñada para el teléfono: pantalla completa, 118 elementos, búsqueda, selección de isótopos, explicación contextual, cálculo físico y una escena 3D orbitable.

## 1. Qué es real y qué representa una ilustración

| Parte | Implementación efectiva | Alcance |
| --- | --- | --- |
| Tabla periódica | 118 elementos con número atómico y números másicos editables | No afirma estabilidad/realidad de cualquier isótopo elegido |
| Energía Q | Diferencia de masas isotópicas neutrales tabuladas por NIST y balance Z/A, 5 canales concretos | No calcula probabilidades, captura ni productos de reacciones arbitrarias |
| Energía Ecm | Control logarítmico de 0.1 a 10,000 keV | No fija una velocidad de cámara ni una duración física |
| Potencial Coulomb | `V_C(r)=1.4399764 Z1 Z2/r` MeV fuera de contacto (y esfera uniformemente cargada dentro) | Modelo electrostático aproximado |
| Radio de contacto | `R=1.2(A1^(1/3)+A2^(1/3))` fm | Modelo geométrico simplificado, no radio medido específico |
| Radio clásico de retorno | `rmin=1.4399764 Z1 Z2/Ecm` fm | Idealización de aproximación frontal clásica sin potencial nuclear |
| Interacción nuclear fenomenológica | `V_N(r)=-V0/[1+exp((r-R)/a)]`, V0 20–60 MeV | No ajustada a datos D–T/D–D; no implica una fuerza microscópica |
| Penetración sub-barrera WKB | Integral numérica con dos puntos de retorno y comparación analítica Coulomb | Factor semiclasico de barrera; **no es probabilidad de fusión** |
| Hamiltoniano radial 256 sitios | Matriz tridiagonal real de 256 puntos, exportable como CSV; **evolución en MIL con 8 qubits** | El potencial nuclear es fenomenológico, sin tasas de fusión ni dispersión real |
| Evolución MIL Core | **2–8 qubits**, mallas de 4 a 256 sitios; Pauli tridiagonal y evolución unitaria contrastada con RK4 independiente | Simulación cuántica por software; no demuestra ventaja frente a otros simuladores ni predice probabilidades de fusión |
| Vista 3D nuclear | El **código original EtherRenderer** traza rayos contra nucleones ilustrativos, hasta 60 por escena | No refleja posiciones reales de los nucleones |
| **Visor MIL 100Q × Ether Render X** | **100 qubits GHZ lógicos** y **100 marcadores 3D**, compuestos por profundidad en dos pasadas de 50 objetos del renderer original | Correlación global calculada; no 100 qubits de evolución nuclear, ni hardware cuántico, ni estados arbitrarios de 100 qubits |
| Exportación | Informe de texto con Q, isótopos, dimensiones, fuentes, límites y motor gráfico | No asegura que se haya producido una reacción física |

La versión 1.1 incorporó la integral WKB y el Hamiltoniano radial. La 1.2 ejecutó primero 4/8 posiciones (2/3 qubits). La **versión 1.3 sí ejecuta las 256 posiciones radiales con 8 qubits lógicos en MIL Core**, gracias a una descomposición de Pauli especializada y comprobada contra RK4 independiente. **La versión 1.4 añade una vista separada de 100 qubits GHZ realmente entrelazados, con brillo 3D derivado de observables Pauli y dos pasadas Ether Render X.** Los pilotos de 4/8 sitios mantienen su dominio L=14 fm; las escalas 16..256 comparten L=96 fm. El potencial Woods–Saxon sigue sin calibrarse contra datos experimentales y esto no calcula secciones eficaces ni tasas de fusión. Detalles: [MIL_RADIAL_SCALING.md](MIL_RADIAL_SCALING.md), [MIL_RADIAL_EVOLUTION.md](MIL_RADIAL_EVOLUTION.md) y [NUCLEAR_INTERACTION.md](NUCLEAR_INTERACTION.md).

## 2. Reacciones de referencia

Las masas isotópicas usadas en los canales documentados provienen de NIST. Q se calcula con `(Σ masas reactivos − Σ masas productos) × 931.49410242 MeV/u`. Al usar masas **atómicas** y conservar electrones en cada canal tabulado, los términos electrónicos se cancelan en la precisión utilizada.

| Reacción | Q aproximada |
| --- | ---: |
| ²H + ³H → ⁴He + n | +17.589 MeV |
| ²H + ²H → ³He + n | +3.269 MeV |
| ²H + ²H → ³H + ¹H | +4.033 MeV |
| ²H + ³He → ⁴He + ¹H | +18.353 MeV |
| ²H + ⁶Li → ⁴He + ⁴He | +22.373 MeV |

Para D+D puedes elegir entre sus dos productos documentados. Para todos los demás pares se muestra solamente el Z/A agregado formal y «Reacción no determinada». **La suma de masas atómicas no identifica por sí sola la reacción final.**

## 3. Motor gráfico propio, y qué hace exactamente

Esta APK compila `native/include/mundo_x/ether_renderer.hpp` (Ether Render X) en una biblioteca JNI separada `libmxfusionrender.so`. El método JNI construye `EtherRenderer`, configura una cámara 3D con órbita y zoom, añade hasta **60 cuerpos** (protones/neutrones ilustrativos), ejecuta `render()` de Mundo X y envía el framebuffer a `Bitmap` Android.

El resultado **no** procede de otra librería 3D, motor de juegos o imagen estática. Android solamente compone el framebuffer propio en pantalla.

- Resolución interna acotada: **344 × 224**.
- Tracing native en hilo propio, interfaz nunca bloqueada por un render directo.
- Arrastre de un dedo: orbitar la cámara. Dos dedos: zoom.
- Línea temporal visual: acercamiento, contacto conceptual y productos de un canal tabulado; cuando el canal no está tabulado, los núcleos vuelven a separarse.
- Para núcleos de hasta 30 nucleones por grupo se muestran todos; para elementos grandes se usa una **muestra visual de hasta 30 esferas por núcleo**. No es una muestra estadística nuclear.

> Un escenario 3D con nucleones dibujados como bolas es una **visualización educativa**, no una función de onda ni las posiciones medibles de los nucleones.

### Visor MIL 100Q de la versión 1.4

Este nuevo panel utiliza el registro MIL Reduced de **100 qubits**, no amplía la simulación de dinámica nuclear de 8 a 100 qubits. Sus 100 marcadores 3D se componen por profundidad en **dos pasadas Ether de 50**, mientras el registro GHZ persiste entre movimientos de cámara. El slider de fase cambia en tiempo real la correlación global ⟨X⊗100⟩=cos(θ) y, con ello, la iluminación del visor. El usuario puede inspeccionar la norma, los componentes, la memoria lógica y los tiempos de cálculo.

La ampliación permite ver **100 objetos aunque EtherRenderer siga limitado a 64 por instancia**, sin copiar los 2^100 estados de un registro genérico. El estado GHZ es estructurado; no demuestra capacidad barata para circuitos universales arbitrarios de 100 qubits. [Documentación del visor MIL 100Q](MIL100_RENDER.md).

## 4. Seguridad

- App Android con `android:exported=true` solo para su pantalla de inicio; sin actividades internas accesibles.
- Sin permisos INTERNET, cámara, micrófono, geolocalización ni almacenamiento general.
- Exportación solo con selector `ACTION_CREATE_DOCUMENT` tras acción explícita.
- Todos los inputs JNI se validan; el visor nuclear mantiene máximo 60 cuerpos y el visor MIL 100q utiliza dos pasadas de 50 cuerpos con framebuffer acotado. Cada sesión nativa opera en un hilo serializado y se libera de forma segura.
- La APK se firma con una **clave de depuración** para pruebas; no es versión certificada para distribución pública.
- Las fórmulas calculan estados hipotéticos; no ofrece instrucciones operativas para realizar fusiones físicas ni controla equipos peligrosos.

## 5. Validaciones de compilación

Workflow `.github/workflows/fusion-standalone-android.yml`:
1. Compila los modelos matemáticos con Java sin Android.
2. Comprueba 118 elementos, canales, Q, invariancia de reactivos, energía y radios; WKB versus Coulomb analítico y Hamiltoniano tridiagonal simétrico. La 1.3 valida 4..256 sitios con descomposición Pauli especializada y evolución MIL versus RK4, incluidos límites de memoria, tiempo y número de puertas.
3. Compila y ejecuta el renderizador C++ original con un oráculo geométrico exhaustivo. La versión 1.4 también comprueba un **GHZ real de 100 qubits**, fase T, correlaciones Pauli multiword, 100 cuerpos compuestos por profundidad y un oráculo independiente de rayos.
4. Compila y firma APK Android ARM64 y x86_64, revisa clase de lanzador y biblioteca nativa.
5. Inicia la APK en un emulador Android 15, captura pantalla y revisa errores de inicio.

## 6. Fuentes científicas

- [Visor cuántico de 100 qubits en Ether Render X, v1.4](MIL100_RENDER.md)
- [Escalamiento verificado de MIL Core a 256 sitios, v1.3](MIL_RADIAL_SCALING.md)
- [MIL Core radial evolution v1.2: modelos, métodos y límites](MIL_RADIAL_EVOLUTION.md)
- [Modelo nuclear/WKB v1.1, ecuaciones y límites](NUCLEAR_INTERACTION.md)

- [NIST Atomic Weights and Isotopic Compositions](https://physics.nist.gov/cgi-bin/Compositions/stand_alone.pl?all=all&ascii=ascii&ele=&isotype=all)
- [BNL/NNDC QCalc, evaluación AME 2020](https://www.nndc.bnl.gov/qcalc/)
- [DOE Explains Fusion Reactions](https://www.energy.gov/science/doe-explainsfusion-reactions)
- [DOE Deuterium-Tritium Fusion Fuel](https://www.energy.gov/science/doe-explainsdeuterium-tritium-fusion-fuel)
- [IAEA Nuclear Fusion Reaction D-T](https://nucleus-qa.iaea.org/sites/fusionportal/Shared%20Documents/DEMO/2018/1/Schuster.pdf)

**Compilación por línea de comandos:**
```sh
gradle -p fusion-android --no-daemon :app:assembleDebug
```
La aplicación no requiere instalar Mundo X Desktop, COSMIC X PC ni su servicio nativo.
