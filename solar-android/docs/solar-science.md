# COSMIC X Solar — datos, modelos y límites físicos

## Qué calcula `SolarData`

El catálogo contiene el Sol, los ocho planetas y catorce lunas seleccionadas. Los identificadores son estables: Sol 0; Mercurio, Venus, Tierra, Marte, Júpiter, Saturno, Urano y Neptuno 1–8; Luna 9; Ío 10; Europa 11; Ganímedes 12; Calisto 13; Titán 14; Encélado 15; Tritón 16; Fobos 17; Deimos 18; Rea 19; Titania 20; Oberón 21; Miranda 22.

El modelo orbital planetario implementa la **tabla 1 de elementos keplerianos y tasas de JPL**, válida entre 1800 y 2050. El radio es el **radio medio volumétrico** en kilómetros. El catálogo utiliza períodos de giro siderales, distintos de la duración del día solar. Los parámetros proceden de [JPL: posiciones aproximadas](https://ssd.jpl.nasa.gov/planets/approx_pos.html), [JPL: parámetros físicos](https://ssd.jpl.nasa.gov/planets/phys_par.html) y las [fichas de NASA](https://nssdc.gsfc.nasa.gov/planetary/factsheet/).

La escala astronómica del cálculo siempre conserva kilómetros y unidades astronómicas. Cualquier ampliación de planetas o compresión de distancias en el renderizado es una transformación de visualización y debe señalarse en la interfaz. No se deben deducir distancias físicas de una imagen ampliada.

## Convenciones y órbitas

- `AU_KM = 149597870.7`; J2000 es JD 2451545.0; un siglo juliano tiene 36525 días. Referencia: [JPL: constantes astrodinámicas](https://ssd.jpl.nasa.gov/astro_par.html).
- `position(id, jd)` devuelve AU heliocéntricas, eclíptica y equinoccio J2000, con ejes para el renderizador `(x,y,z) = (x_ecl,z_ecl,-y_ecl)`. La transformación conserva la orientación dextrógira. El Sol permanece en el origen.
- Para cada elemento `q`, se evalúa `q=q0+qdot*T`, con `T=(jd-2451545.0)/36525`. La anomalía media es `M=L−varpi`; el argumento de perihelio es `omega=varpi−Omega`. La ecuación `E−e*sin(E)=M` se resuelve por Newton, en radianes.
- En el plano orbital, `x=a*(cos(E)−e)` y `y=a*sqrt(1−e²)*sin(E)`. La rotación por nodo, inclinación y perihelio sigue las ecuaciones explícitas de JPL.
- `orbitPosition(id,fraction,jd)` devuelve un punto sobre la elipse instantánea. Cero es periapsis y una vuelta cierra la curva. Para lunas, el centro de la curva es la posición del planeta en `jd`.
- Se limita `jd` al intervalo 1800-01-01–2050-01-01 para evitar extrapolar el ajuste. La definición de la efeméride requiere **TDB**. Una fecha UTC usada directamente por una interfaz conserva una pequeña diferencia de escala temporal; no equivale a realizar la conversión UTC→TDB.

La fila terrestre de JPL corresponde al **baricentro Tierra–Luna**. Se obtiene una Tierra aproximada mediante `rTierra=rEMB−rLunaRel/(1+M_Tierra/M_Luna)`, y luego `rLuna=rTierra+rLunaRel`. El cociente de masas se deriva de los parámetros gravitatorios DE440 de la tabla astrodinámica de JPL. La fase lunar ilustrativa limita también esta corrección.

Los errores nominales publicados por JPL varían según planeta: por ejemplo, la distancia tiene errores nominales de unos 1000 km para Mercurio y 1,5 millones de km para Saturno en este ajuste. Son referencias del método, no límites garantizados en cada fecha. No se incluyen una integración gravitatoria de N cuerpos, retardos de luz, aberración, relatividad, eclipses predictivos ni precisión de navegación espacial. Para efemérides de mayor precisión, JPL remite a [Horizons](https://ssd.jpl.nasa.gov/horizons/).

## Lunas y ejes de giro

Las lunas usan elipses locales con semiejes, excentricidades y períodos publicados, pero **fases, nodos y periapsis ilustrativos**. No son sus posiciones observadas en la fecha mostrada. Se omiten resonancias, precesión, libraciones y perturbaciones; la figura y la orientación de las lunas se simplifican. Fobos y Deimos se aproximan por esferas de radio medio.

Los planos de las lunas regulares se orientan con el polo J2000 del planeta. La Luna usa una inclinación de 5,145° respecto a la eclíptica. Tritón conserva una inclinación de 157,345° respecto al plano ecuatorial de Neptuno y su movimiento retrógrado. Las lunas seleccionadas de Urano acompañan la dirección del giro de Urano. Fuentes: [Luna](https://nssdc.gsfc.nasa.gov/planetary/factsheet/moonfact.html), [Marte y sus lunas](https://nssdc.gsfc.nasa.gov/planetary/factsheet/marsfact.html), [lunas jovianas](https://nssdc.gsfc.nasa.gov/planetary/factsheet/joviansatfact.html), [saturnianas](https://nssdc.gsfc.nasa.gov/planetary/factsheet/saturniansatfact.html), [uranianas](https://nssdc.gsfc.nasa.gov/planetary/factsheet/uraniansatfact.html) y [neptunianas](https://nssdc.gsfc.nasa.gov/planetary/factsheet/neptuniansatfact.html).

Los polos proceden de las fichas NASA y del [kernel de constantes planetarias NAIF/JPL `pck00011.tpc`](https://naif.jpl.nasa.gov/pub/naif/generic_kernels/pck/pck00011.tpc). `spinAxis(id)` devuelve la dirección aproximada del momento angular planetario en el marco del renderizador. Se mantienen fijos en J2000; no se implementan todos los términos de precesión y nutación del kernel.

**Convención de giro:** `rotationHours` conserva el signo del catálogo, mientras `obliquityDeg` contiene la oblicuidad física completa, incluida la inclinación mayor de 90° de Venus y Urano. Si el renderizador usa esa inclinación completa, debe usar el valor absoluto del período; aplicar además su signo produciría una doble inversión. Si usa el polo norte IAU, debe conservar el período con signo. `spinAxis` ya incorpora el signo y debe combinarse con el período absoluto.

## Magnetismo

Las líneas magnéticas son una visualización de un campo invisible. Un trazo luminoso no representa un hilo material. La función exterior de dipolo calcula, en microteslas,

`B = B0/r³ * [3*(mhat·rhat)*rhat − mhat]`.

`r` se expresa en radios de referencia y `mhat` se normaliza. En el ecuador de la esfera `r=1`, la magnitud es `B0`; en los polos es `2*B0`. La intensidad cae como `r⁻³`. Para `r<1` la función devuelve un centinela cero porque el modelo exterior no aplica al interior: **no significa que el campo físico interior sea cero**.

| Cuerpo | Tipo físico representado | Coeficiente de origen | Inclinación del dipolo usada |
|---|---|---|---|
| Mercurio | Interno, dipolo aproximado | 0,2 µT a 2440 km | 0° |
| Venus | Inducido por el viento solar | Sin constante dipolar asignada | No aplica |
| Tierra | Interno, referencia histórica | 30,6 µT a 6378 km | 9,35°; polo WMM2020 |
| Marte | Inducido y corteza magnetizada | Sin constante dipolar global asignada | No aplica |
| Júpiter | Interno, dipolo aproximado | 430 µT a 71398 km | 9,4° |
| Saturno | Interno, dipolo aproximado | 21,5 µT a 60330 km | 0° |
| Urano | Interno, dipolo orientativo | 22,8 µT a 25600 km | 58,6° |
| Neptuno | Interno, dipolo orientativo | 14,2 µT a 24765 km | 46,9° |
| Ganímedes | Interno, dipolo aproximado | 0,719 µT a 2631,2 km | 176° |
| Sol | Campo variable y viento solar conceptual | Sin intensidad fija | No aplica |

Las fichas expresan varios coeficientes como gauss multiplicado por el cubo del radio del modelo. Se convierten con `1 G=100 µT`, y `fieldMicroT` se adapta al radio medio del cuerpo mediante `B0_catalogo*(R_modelo/R_medio)^3`. Así, el cálculo por radios y el radio visual de referencia son consistentes. No es una estimación del campo local en cualquier lugar de la superficie.

Fuentes cuantitativas: fichas NASA de [Mercurio](https://nssdc.gsfc.nasa.gov/planetary/factsheet/mercuryfact.html), [Tierra](https://nssdc.gsfc.nasa.gov/planetary/factsheet/earthfact.html), [Júpiter](https://nssdc.gsfc.nasa.gov/planetary/factsheet/jupiterfact.html), [Saturno](https://nssdc.gsfc.nasa.gov/planetary/factsheet/saturnfact.html), [Urano](https://nssdc.gsfc.nasa.gov/planetary/factsheet/uranusfact.html) y [Neptuno](https://nssdc.gsfc.nasa.gov/planetary/factsheet/neptunefact.html). Ganímedes: [Kivelson y colaboradores, momentos permanente e inducido, NASA NTRS](https://ntrs.nasa.gov/api/citations/20020044825/downloads/20020044825.pdf). Alineación de Saturno: [Cao y colaboradores, análisis Cassini Grand Finale](https://arxiv.org/abs/1911.06952), límite menor de 0,007°.

El dipolo centrado omite desplazamientos importantes: Mercurio tiene un desplazamiento norte de aproximadamente un quinto de su radio; Urano y Neptuno tienen campos desplazados y multipolos importantes. No se añaden arbitrariamente esos desplazamientos a la función de dipolo. Fuentes: [NASA MESSENGER](https://astrobiology.nasa.gov/missions/messenger/), [NASA Urano](https://science.nasa.gov/uranus/facts/) y las fichas cuantitativas anteriores.

Venus carece de un dipolo interno global observado y tiene una magnetosfera inducida; Marte combina un campo inducido y magnetismo de la corteza. Fuentes: [NASA Venus](https://science.nasa.gov/venus/venus-facts/), [NASA Marte](https://science.nasa.gov/mars/facts/), [ESA: viento solar y Venus](https://www.esa.int/Science_Exploration/Space_Science/Venus_Express/Caught_in_the_wind_from_the_Sun). `FIELD_NONE` para una luna significa que **esta aplicación no calcula su campo local**, no que todo su entorno carezca de magnetismo. Europa, por ejemplo, presenta una respuesta inducida; no debe etiquetarse como un cuerpo físicamente ajeno a los campos magnéticos.

Una deformación gráfica de las líneas hacia una cola es **conceptual**. No es la solución de magnetohidrodinámica de una magnetosfera ni preserva automáticamente las propiedades de una solución dipolar. La orientación general debe ser comprimida hacia el Sol y extendida en dirección antisolar. No se simulan reconexión, plasmas cinéticos, corrientes, cinturones de radiación, auroras cuantitativas ni clima espacial en directo.

## Viento solar y anillos

NASA describe viento solar ordinario de cientos de kilómetros por segundo; 400 km/s es una referencia ilustrativa razonable. El campo interplanetario desarrolla una espiral por la rotación solar. Las partículas y líneas en pantalla representan un flujo conceptual; su velocidad visual, densidad y tamaño no son mediciones. Referencias: [NASA: viento solar](https://science.nasa.gov/sun/what-is-the-solar-wind/) y [SDO: clima espacial](https://sdo.gsfc.nasa.gov/mission/spaceweather.php).

Los cuatro planetas gigantes poseen anillos. El catálogo conserva intervalos físicos de referencia, expresados como cocientes del radio medio: Júpiter 123000–128940 km (anillo principal); Saturno 66900–139826 km (D hasta F); Urano 41837–51149 km (sistema interior); Neptuno 41900–62933 km. Estos intervalos no incluyen todos los anillos difusos exteriores. Fuentes NASA: [Júpiter](https://nssdc.gsfc.nasa.gov/planetary/factsheet/jupringfact.html), [Saturno](https://nssdc.gsfc.nasa.gov/planetary/factsheet/satringfact.html), [Urano](https://nssdc.gsfc.nasa.gov/planetary/factsheet/uranringfact.html), [Neptuno](https://nssdc.gsfc.nasa.gov/planetary/factsheet/nepringfact.html).

Las anchuras de anillos finos y sus opacidades pueden ampliarse para hacerlos visibles. Si se dibuja la división de Cassini, sus bordes de referencia son aproximadamente 117507 y 122340 km del centro de Saturno. Los anillos están en el plano ecuatorial del planeta; no son discos alineados arbitrariamente con la pantalla.

## Texturas y apariencias

El catálogo reserva `textures/earth.png`, `textures/moon.jpg` y `textures/jupiter.jpg` para mapas locales; los demás cuerpos admiten material procedural. Las bandas, manchas, cráteres y nubes procedurales son aproximaciones visuales, no mapas medidos. Los mapas deben acreditarse en el inventario de recursos de la aplicación según el archivo descargado.

La [CGI Moon Kit de NASA SVS](https://svs.gsfc.nasa.gov/4720/) ofrece mapas lunares para renderizado basados en LRO/LROC y LOLA, con ajustes de color y zonas polares completadas. NASA indica que están optimizados para presentación y que el análisis científico debe usar los datos de origen. Crédito indicado: NASA's Scientific Visualization Studio / Ernie Wright; datos LRO/LROC/LOLA. La [galería de mapas JPL](https://maps.jpl.nasa.gov/tmaps/) también distingue texturas de visualización de datos para análisis.

## Verificación reproducible

Las pruebas de `tests/SolarDataChecks.java` revisan: unidades y rangos; perihelio y afelio contrastados con fichas independientes; conservación aproximada de la velocidad areolar; sentido de las órbitas; período local de lunas; Tritón retrógrado; orientación de las lunas de Urano; `B_polo=2*B_ecuador`; caída `r⁻³`; divergencia numérica exterior nula; límites temporales y rechazo de entradas inválidas. Son comprobaciones del modelo implementado, no una validación de equivalencia con toda la naturaleza.

Desde `solar-android`:

```sh
java com.sun.tools.javac.Main -encoding UTF-8 -d /tmp/cosmic-solar-science-classes app/src/main/java/com/mundox/solar/SolarData.java tests/SolarDataChecks.java
java -cp /tmp/cosmic-solar-science-classes com.mundox.solar.SolarDataChecks
```

En un JDK convencional, `javac` puede sustituir a `java com.sun.tools.javac.Main`. Estas pruebas no necesitan Android, acceso de red ni bibliotecas externas.
