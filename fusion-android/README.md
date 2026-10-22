# MUNDO X · Fusion 3D (APK independiente)

**Nombre de Android:** Mundo X Fusion 3D  
**Paquete:** `com.mundox.fusion3d`  
**Versión:** 1.1.0  
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
| Radio clásico de retorno | `rmin=1.4399764 Z1 Z2/Ecm` fm | Idealización de aproximación frontal clásica sin potencial nuclear |\n| Interacción nuclear fenomenológica | `V_N(r)=-V0/[1+exp((r-R)/a)]`, V0 20–60 MeV | No ajustada a datos D–T/D–D; no implica una fuerza microscópica |\n| Penetración sub-barrera WKB | Integral numérica con dos puntos de retorno y comparación analítica Coulomb | Factor semiclasico de barrera; **no es probabilidad de fusión** |\n| Hamiltoniano radial | Matriz tridiagonal real 256 puntos, exportable como CSV | Operador de estudio, todavía sin evolución en MIL Core |
| Vista 3D | El **código original de EtherRenderer (Mundo X)** traza rayos contra esferas tridimensionales y sombrea el fotograma | Posiciones de nucleones y trayectorias son diagramas, no resultados de un Hamiltoniano nuclear |
| Exportación | Informe de texto con Q, isótopos, dimensiones, fuentes, límites y motor gráfico | No asegura que se haya producido una reacción física |

La versión 1.1 añade un Hamiltoniano **discretizado** y un modelo WKB; no ejecuta ni propaga ese Hamiltoniano. La dinámica nuclear cuántica completa requiere fuerzas y amplitudes de transición verificables, condiciones de contorno para dispersión y comparaciones con secciones eficaces experimentales. No se atribuye ese trabajo a MIL Core. Detalles y derivaciones: [NUCLEAR_INTERACTION.md](NUCLEAR_INTERACTION.md).

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

## 4. Seguridad

- App Android con `android:exported=true` solo para su pantalla de inicio; sin actividades internas accesibles.
- Sin permisos INTERNET, cámara, micrófono, geolocalización ni almacenamiento general.
- Exportación solo con selector `ACTION_CREATE_DOCUMENT` tras acción explícita.
- Todos los inputs JNI se validan; máximo 60 objetos, framebuffer acotado y por escena un único hilo trabajador.
- La APK se firma con una **clave de depuración** para pruebas; no es versión certificada para distribución pública.
- Las fórmulas calculan estados hipotéticos; no ofrece instrucciones operativas para realizar fusiones físicas ni controla equipos peligrosos.

## 5. Validaciones de compilación

Workflow `.github/workflows/fusion-standalone-android.yml`:
1. Compila los modelos matemáticos con Java sin Android.
2. Comprueba 118 elementos, canales, Q, invariancia de reactivos, energía y radios. La ampliación 1.1 también prueba el modelo Woods–Saxon/WKB frente a Coulomb analítico, su sensibilidad y el Hamiltoniano tridiagonal simétrico.
3. Compila y ejecuta el renderizador C++ original con un oráculo geométrico exhaustivo y comprueba órbita distinta.
4. Compila y firma APK Android ARM64 y x86_64, revisa clase de lanzador y biblioteca nativa.
5. Inicia la APK en un emulador Android 15, captura pantalla y revisa errores de inicio.

## 6. Fuentes científicas\n\n- [Modelo nuclear, ecuaciones, pruebas y límites de v1.1](NUCLEAR_INTERACTION.md)

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
