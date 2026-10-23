# Mundo X · Fusion Lab

**Programa educativo offline de física nuclear integrado en Mundo X Desktop y COSMIC X PC Android.**

Fusion Lab aparece en **Inicio → Fusion Lab** y en la tarjeta **Núcleo cuántico** del PC existente. No abre una web, no necesita red ni controla hardware radiactivo.

## Uso

1. Escoge el **núcleo A** o **B** y toca cualquiera de los **118 elementos** en la tabla periódica horizontal.
2. Introduce el número másico **A** (protones + neutrones) de cada isótopo. Los valores iniciales son ejemplos editables, no afirmaciones de abundancia natural, estabilidad ni disponibilidad del isótopo.
3. Pulsa **Calcular y visualizar**. Se muestra un diagrama ilustrativo de colisión.
4. Observa la suma de protones **Z** y de nucleones **A**, que define la composición de un núcleo compuesto *hipotético*, **no el producto necesariamente producido**.
5. Cuando existe un canal en el catálogo local, se muestran sus productos y la **energía Q** por evento calculada a partir de masas de isótopos tabuladas.
6. Guarda el reporte en un archivo `.txt` mediante el selector de documentos Android.

## Canales documentados en esta primera versión

| Entrada | Productos del canal | Q aproximado |
| --- | --- | ---: |
| ²H + ³H | ⁴He + n | +17,589 MeV |
| ²H + ²H | ³He + n | +3,269 MeV |
| ²H + ²H | ³H + ¹H | +4,033 MeV |
| ²H + ³He | ⁴He + ¹H | +18,353 MeV |
| ²H + ⁶Li | ⁴He + ⁴He | +22,373 MeV |

Las masas isotópicas neutrales están publicadas en NIST. En estos cinco canales se conserva el número de electrones cuando se usan masas atómicas, así como el número de protones y de nucleones entre reactivos y productos. Las cantidades Q son del **canal específico**, no del supuesto núcleo compuesto A/Z.

Para combinaciones sin canal tabulado, **Q no se calcula**: un núcleo compuesto formal no basta para predecir captura, emisión de neutrones, decaimientos, probabilidad de túnel, existencia de núcleos, ni estabilidad.

La escala electrostática indicada es una aproximación de contacto de dos esferas: `E_C ≈ (1.4399764 MeV·fm) Z1 Z2 / [1.2 fm (A1^(1/3)+A2^(1/3))]`. **No es el umbral necesario**, ya que la fusión puede depender del túnel cuántico, potencial nuclear y canales alternativos. No determina secciones eficaces ni tasas de reacción.

## Qué hace Mundo X y qué no hace aquí

- **Implementado:** interfaz operativa de estudio y exploración nuclear, tabla completa, selección isotópica, conservación de protones/nucleones, comparación con cinco canales publicados y exportación de resultados.
- **No implementado:** integración de un Hamiltoniano nuclear validado con MIL Core, simulación de dinámica de colisión de muchos cuerpos, predicción de nuevos elementos o producción física de material.
- **Ruta futura de investigación:** construir un modelo Hamiltoniano nuclear con potenciales declarados y aproximaciones explícitas, discretización, evolución temporal, análisis de errores y validación contra datos experimentales; solamente entonces plantear el backend cuántico como ejecutor de esa dinámica.

### Seguridad

Fusion Lab solo acepta datos de selección de isótopos, utiliza cálculo local y escribe archivos cuando el usuario elige su destino. La actividad Android no es exportada a otras aplicaciones. No utiliza permisos de red propios ni códigos de control de reactor.

## Referencias de datos

- NIST Atomic Weights and Isotopic Compositions: https://physics.nist.gov/cgi-bin/Compositions/stand_alone.pl?all=all&ascii=ascii&ele=&isotype=all
- IAEA fusion portal, reacción deuterio-tritio: https://nucleus-qa.iaea.org/sites/fusionportal/Shared%20Documents/DEMO/2018/1/Schuster.pdf
- Conversión de unidad de masa atómica a energía: 1 u ≈ 931,49410242 MeV/c²; 1 MeV = 1,602176634 × 10⁻¹³ J.

### Verificación automática

`.github/workflows/own-pc-android.yml` compila `FusionPhysics.java` y corre `FusionPhysicsTest.java` con Java normal antes del paso Android. La prueba cubre geometría sin solapamientos de las 118 casillas, conservación, reacciones conocidas, invariancia al orden de reactivos y rechazo de combinaciones no tabuladas.
