# Ambientes, descenso y mapa orbital

Este documento describe `SolarEnvironment` y `SolarMapView`. Ambas clases utilizan los identificadores y las posiciones del catálogo original de 23 cuerpos. No cambian `SolarData`, sus órbitas, las constantes magnéticas ni el núcleo de COSMIC X.

## Qué representa el descenso

El botón **Descender** permite colocar la cámara cerca de los ocho planetas y las catorce lunas disponibles, identificadores 1–22, y explorar una aproximación visual de su entorno. El botón está deshabilitado para el Sol, identificador 0. La altitud es la distancia radial de la cámara menos el radio medio del catálogo. No es una elevación obtenida de un modelo digital del terreno. Los cráteres, montañas, escarpes, océanos y formas irregulares no modifican ese radio de referencia.

Los planetas gigantes no tienen una superficie sólida donde aterrizar. En ellos la altitud cero corresponde a la esfera del radio medio usada como referencia aproximada de nubes / nivel de 1 bar. Permitir altitudes negativas muestra una profundidad dentro de la representación nubosa; no demuestra que la posición coincida con una presión calculada. Las convenciones de referencia de los gigantes se explican en las [notas de las fichas planetarias de NASA](https://nssdc.gsfc.nasa.gov/planetary/factsheet/planetfact_notes.html).

El Sol tiene fotosfera, cromosfera, región de transición y corona. `SolarEnvironment` conserva un perfil de su entorno exterior para la API, pero la actividad no ofrece descenso solar. Se puede seleccionar y observar el Sol desde la navegación exterior. No se añade un suelo sólido ni se simulan los efectos de presión, temperatura o radiación sobre una nave. Fuente: [NASA, Sun Facts](https://science.nasa.gov/sun/facts/).

## Clasificación física y tratamiento visual

| Cuerpos | Entorno descrito por las fuentes | Tratamiento en esta versión |
|---|---|---|
| Tierra | Atmósfera dominada por nitrógeno y oxígeno | Cielo y envoltura azulados, con dispersión aproximada |
| Venus | Atmósfera densa dominada por CO₂; nubes de ácido sulfúrico | Bruma densa y tono cálido ilustrativo |
| Marte | Atmósfera tenue dominada por CO₂, con polvo suspendido | Bruma rojiza de opacidad menor |
| Júpiter y Saturno | Gigantes gaseosos, sin superficie sólida accesible | Entrada entre nubes ilustrativas; sin suelo |
| Urano y Neptuno | Gigantes de hielo con atmósfera de hidrógeno, helio y metano | Entrada en envolturas nubosas; sin suelo |
| Titán | Atmósfera densa de nitrógeno y metano, con bruma orgánica | Envoltura brumosa anaranjada |
| Ío | Atmósfera muy tenue cuyo componente principal es SO₂ | Opacidad visual muy pequeña |
| Tritón | Atmósfera tenue de nitrógeno y pequeñas cantidades de metano | Bruma débil, sin meteorología estacional |
| Mercurio y Luna | Exosferas extremadamente tenues | Sin cielo de dispersión global espeso |
| Europa, Ganímedes, Calisto y Rea | Exosferas tenues; composición diferenciada en las fichas | Sin envoltura visual global espesa |
| Encélado | Exosfera y plumas de vapor/partículas localizadas | No se sustituye la pluma por aire denso global; los chorros no se calculan en este modo |
| Fobos, Deimos, Titania, Oberón y Miranda | Sin un modelo atmosférico local asignado por esta app | Superficie de referencia y cielo oscuro; no se afirma una ausencia absoluta de partículas gaseosas |

Los códigos `NONE`, `EXOSPHERE` y `PLUME` tienen `hasAtmosphere=false`. En esta API esa bandera significa **que no se aplica el modelo visual de atmósfera continua**. No afirma que Mercurio o la Luna carezcan de exosfera. El campo `type` y el texto `label` conservan la distinción.

La apariencia de Neptuno se mantiene aproximada. NASA explica que varias imágenes históricas realzaron su azul y que un nuevo procesamiento mostró mayor similitud de color con Urano. Elegir una paleta de la escena no reemplaza una calibración fotométrica. Fuente: [NASA, Neptune Facts](https://science.nasa.gov/neptune/neptune-facts/).

### Presiones de referencia

Estos números sirven como referencias documentales. No son las entradas de un modelo de presión a cualquier altitud, ni observaciones actuales de todos los puntos de un planeta.

| Cuerpo | `referencePressureBar` | Procedencia y significado |
|---|---:|---|
| Tierra | 1,014 bar | 1014 mbar en la [ficha de NASA](https://nssdc.gsfc.nasa.gov/planetary/factsheet/earthfact.html) |
| Venus | 92 bar | Valor superficial de referencia de la [ficha de NASA](https://nssdc.gsfc.nasa.gov/planetary/factsheet/venusfact.html) |
| Marte | 0,00636 bar | 6,36 mbar al radio medio, variable con estación y lugar, en la [ficha de NASA](https://nssdc.gsfc.nasa.gov/planetary/factsheet/marsfact.html) |
| Titán | 1,467 bar | 1467 ± 1 hPa medidos por HASI en el lugar de aterrizaje de Huygens en 2005; [ESA](https://www.esa.int/Science_Exploration/Space_Science/Results_from_Mars_Express_and_Huygens/Titan_s_turbulence_surprises_scientists) |
| Los cuatro gigantes | 1 bar | Convención aproximada del nivel de referencia; **no presión de una superficie sólida** |
| Otros cuerpos | `NaN` | Esta clase no asigna una presión de referencia; no significa cero |

## Ajustes explícitos para la vista y la navegación

`atmosphereTopKm` es el alcance de la envoltura visual. No es un límite físico medido de la atmósfera. En particular, usar 120 km en la vista terrestre no afirma que el aire termine allí: las partículas se vuelven progresivamente más escasas y no existe una frontera abrupta. La [ESA describe esta transición gradual](https://www.esa.int/Enabling_Support/Preparing_for_the_Future/Discovery_and_Preparation/ESA_study_paves_the_way_for_a_better_understanding_of_space_weather).

`scaleHeightKm` controla una caída visual simplificada y constante. No resuelve temperatura, composición, aerosoles ni alturas de escala variables. `color` y `opacity` son ajustes de presentación sin unidades físicas; no se deducen de forma directa de las presiones de la tabla anterior. Estos valores no constituyen un modelo de transferencia radiativa validado.

Todos los límites de la siguiente tabla son elecciones de navegación, en kilómetros. La altura mínima de 0,5 km en los cuerpos sólidos evita atravesar la esfera de referencia. No garantiza separación con una montaña real que no esté representada. En los gigantes se admite −200 km para recorrer la capa nubosa aproximada. La fila solar documenta el perfil exterior de la API; sus controles de descenso no están expuestos en la actividad.

| ID | Cuerpo | Envoltura visual superior | Mínimo | Entrada inicial | Máximo |
|---:|---|---:|---:|---:|---:|
| 0 | Sol (perfil API; sin botón Descender) | 700000 | 1000 | 20000 | 1500000 |
| 1 | Mercurio | 0 | 0,5 | 150 | 2500 |
| 2 | Venus | 180 | 0,5 | 140 | 1200 |
| 3 | Tierra | 120 | 0,5 | 35 | 2000 |
| 4 | Marte | 150 | 0,5 | 25 | 1500 |
| 5 | Júpiter | 1500 | −200 | 600 | 12000 |
| 6 | Saturno | 1800 | −200 | 900 | 12000 |
| 7 | Urano | 1200 | −200 | 500 | 7000 |
| 8 | Neptuno | 1200 | −200 | 500 | 7000 |
| 9 | Luna | 0 | 0,5 | 70 | 1500 |
| 10 | Ío | 100 | 0,5 | 40 | 1500 |
| 11 | Europa | 0 | 0,5 | 70 | 1200 |
| 12 | Ganímedes | 0 | 0,5 | 80 | 2000 |
| 13 | Calisto | 0 | 0,5 | 70 | 1800 |
| 14 | Titán | 600 | 0,5 | 350 | 2500 |
| 15 | Encélado | 0 | 0,5 | 30 | 500 |
| 16 | Tritón | 300 | 0,5 | 40 | 1300 |
| 17 | Fobos | 0 | 0,5 | 8 | 150 |
| 18 | Deimos | 0 | 0,5 | 6 | 120 |
| 19 | Rea | 0 | 0,5 | 45 | 800 |
| 20 | Titania | 0 | 0,5 | 50 | 900 |
| 21 | Oberón | 0 | 0,5 | 50 | 900 |
| 22 | Miranda | 0 | 0,5 | 25 | 450 |

Un valor cero en la columna de envoltura significa que no se dibuja una capa atmosférica global en este modo. No define el tamaño de una exosfera real.

En Venus, los gigantes y Titán, la entrada inicial sitúa la cámara por encima de las capas visuales más opacas para mostrar la aproximación antes de bajar entre las nubes. Es una elección de cámara; no representa una frontera atmosférica física ni modifica los límites de descenso.

## Mapa orbital

El mapa obtiene posiciones de `SolarData.position(id, jd)` y traza las elipses mediante `SolarData.orbitPosition(id, fraction, jd)`. Conserva la fecha elegida y el intervalo del modelo original, 1800–2050. Los elementos planetarios son aproximados y las fases de las lunas siguen siendo ilustrativas, tal como se documenta en [solar-science.md](solar-science.md).

La vista principal proyecta sobre el plano eclíptico de J2000. Con las coordenadas del renderer `(x, y, z) = (x_ecl, z_ecl, −y_ecl)`, emplea `x` horizontal y `z` hacia abajo en pantalla. La inclinación se manifiesta en la proyección de las órbitas; el mapa no muestra la altura por encima del plano como una dimensión independiente.

Para ver a la vez los planetas interiores y exteriores, el radio de la posición proyectada se comprime mediante:

```
radiusMap = log(1 + distanceAU / 0.18) / log(1 + 32 / 0.18)
```

La dirección de cada posición se conserva. Los círculos de guía rotulados 1, 5, 10 y 30 UA indican la distancia que representa cada radio; no forman una regla lineal. Los tamaños de los marcadores se amplían para poder tocarlos.

Al seleccionar un planeta se muestra un panel separado con sus lunas disponibles. Sus posiciones se calculan con respecto al padre y se comprimen radialmente en otra escala logarítmica. Las filas de nombres proporcionan destinos seleccionables aun cuando dos marcadores se superpongan en la proyección. Las órbitas locales no comparten escala con el diagrama heliocéntrico. La interfaz indica expresamente las dos transformaciones.

Los gestos de zoom y desplazamiento solo modifican la vista del mapa. Seleccionar un destino llama al receptor de la actividad; esta clase no prepara, consulta ni modifica un registro cuántico. Las acciones Recentrar, ampliar y reducir tampoco cambian el tiempo ni las órbitas.

## Fuentes de clasificación y composición

Consulta: 19 de octubre de 2026. Se utilizan fuentes primarias de NASA y ESA; no se incorporan servicios en directo ni dependencias de red a la app.

- [NASA: Earth Facts](https://science.nasa.gov/earth/facts/), composición principal del aire terrestre.
- [NASA: Mercury Facts](https://science.nasa.gov/mercury/facts/), exosfera y componentes principales.
- [NASA: Venus Facts](https://science.nasa.gov/venus/venus-facts/), atmósfera de CO₂ y nubes de ácido sulfúrico.
- [NASA: Mars Facts](https://science.nasa.gov/mars/facts/), atmósfera tenue y polvo.
- [NASA: Jupiter Facts](https://science.nasa.gov/jupiter/jupiter-facts/), nubes y ausencia de una superficie sólida donde aterrizar.
- [NASA: Saturn Facts](https://science.nasa.gov/saturn/facts/), nubes, gases y ausencia de una superficie accesible.
- [NASA: Uranus Facts](https://science.nasa.gov/uranus/facts/), composición atmosférica y naturaleza de gigante de hielo.
- [NASA: Neptune Facts](https://science.nasa.gov/neptune/neptune-facts/), atmósfera, ausencia de suelo y precisión sobre el color visible.
- [NASA: Titan Facts](https://science.nasa.gov/saturn/moons/titan/facts/), nitrógeno, metano y bruma; [ESA/HASI](https://www.esa.int/Science_Exploration/Space_Science/Results_from_Mars_Express_and_Huygens/Titan_s_turbulence_surprises_scientists), presión medida por Huygens.
- [NASA: The Moon's Atmosphere](https://science.nasa.gov/moon/lunar-atmosphere/), exosfera lunar de helio, neón y argón.
- [NASA: Io Facts](https://science.nasa.gov/jupiter/jupiter-moons/io/facts/), atmósfera tenue cuyo componente principal es dióxido de azufre.
- [NASA: Europa Facts](https://science.nasa.gov/jupiter/jupiter-moons/europa/europa-facts/), atmósfera de oxígeno extremadamente tenue.
- [NASA: Ganymede Facts](https://science.nasa.gov/jupiter/jupiter-moons/ganymede/facts/), evidencia de atmósfera tenue de oxígeno.
- [NASA: Callisto Facts](https://science.nasa.gov/jupiter/jupiter-moons/callisto/facts/), exosfera de CO₂ detectada por Galileo.
- [NASA: Rhea](https://science.nasa.gov/saturn/moons/rhea/), exosfera de oxígeno y CO₂ detectada por Cassini.
- [NASA/Cassini: Enceladus](https://science.nasa.gov/mission/cassini/science/enceladus/), plumas polares de agua y hielo; no se asigna un cielo denso global.
- [NASA: Triton](https://science.nasa.gov/neptune/moons/triton/), atmósfera tenue de nitrógeno y metano.
- [NASA: Sun Facts](https://science.nasa.gov/sun/facts/), estructura de la atmósfera solar.

Para Fobos, Deimos y las tres lunas de Urano incluidas, la clase usa la categoría **sin atmósfera densa modelada**. No agrega mediciones, presiones ni composiciones atmosféricas que no estén documentadas en este proyecto.
