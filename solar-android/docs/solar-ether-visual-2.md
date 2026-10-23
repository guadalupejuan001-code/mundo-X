# Solar Ether Visual 2 · calcular únicamente lo que puede verse

**Base reproducible:** `main` fusionado con Ether Memory PR #54,
`92d0113acec83f5e5a91e906171a821fe3625f16`.

## Idea y conexión real con Ether

Ether Memory evita representar y copiar información que nadie necesita.
Solar aplica **el mismo principio de diseño** a su recorrido gráfico, pero
**no convierte** la memoria visual en qubits ni usa Inverse Ether Memory como
almacenamiento de texturas. Son recursos diferentes y deben medirse por separado.

Antes de esta etapa, el shader `solar.frag` ponía en
`visualCandidates()` un bit activo para todos los cuerpos. Cada fragmento
probaba las 23 esferas, hasta cuatro sistemas de anillos y hasta once
atmósferas incluso cuando estaban lejos de su rayo.

Solar ya incluía `SolarVisualEther`, un planificador geométrico conservador
con pruebas exhaustivas, pero no estaba conectado al shader en producción.
Ahora la cámara y las posiciones actuales producen un **mapa transitorio**
de máscaras de 32 bits por celdas de 128 × 128 píxeles. El motor crea o
reutiliza una textura `GL_R32UI` y la actualiza mediante `glTexSubImage2D`.
El shader consulta esa máscara con `texelFetch` e ignora únicamente los
cuerpos cuyas esferas envolventes no intersectan la región.

## Lo que se conserva

- Misma cámara, proyección, órbitas, escala, modelos físicos, gravedad de
  descenso, mapas NASA, materiales, shaders solares, rastro de campos, tiempo
  y navegación Android.
- La máscara es conservadora: incluye el radio de anillos y la altura de
  atmósfera. Ante cámara dentro del volumen, mantiene el cuerpo como
  candidato en todos los fragmentos.
- No se filtran los cuerpos de `eclipse()` por región de cámara: la
  oclusión por otro planeta hacia el Sol no depende de si ese planeta está
  visible desde la cámara. No volver a integrar aquí el broad-phase de
  eclipses que no acreditó ganancia sostenida anteriormente.
- La corona radial y el cielo lejano siguen evaluándose: no se omiten
  por ausencia del Sol en la máscara esférica.
- Una caída de la ruta de enteros de GLES no invalida todo Solar: se desactiva
  Ether Visual y vuelve al recorrido exhaustivo anterior; el interruptor del
  observatorio permite compararlo manualmente.

## Coste de datos

Por celda: **4 bytes** de máscara. Para 7680 × 4320 y tiles de 128,
se esperan 60 × 34 = 2.040 celdas, **8.160 bytes** de máscara GPU;
más un buffer de subida reutilizable de la misma longitud. No es la RAM
total del proceso, ni incluye mapa de texturas, framebuffer 8K o controlador
gráfico. Con una pantalla 960 × 540 la tabla tiene 8 × 5 = 40 celdas,
160 bytes de máscara GPU.

El planificador examina posiciones, radios y límites conservadores para
la observación presente. Esta versión lo recalcula por cuadro; queda
pendiente su medición en hardware físico y decidir cuándo reutilizar mapas
cuando todas las entradas geométricas son idénticas.

## Interfaz

En **Observatorio → TU MOTOR → Ether Visual · regiones visibles** se alterna
el modo de regiones y el modo completo de referencia. La línea del motor
reporta la proporción aproximada de candidatos a pruebas exhaustivas,
que **no es** un porcentaje de FPS ganados ni una tasa de píxeles ahorrados.
Los formatos de captura y 4K/8K no se rebajan ni se reetiquetan.

## Pruebas y criterio de integración

La suite Android incluye una comparación de imágenes de la GPU en
las mismas escenas estáticas: vista general, Tierra, Saturno con anillos,
Luna y descenso atmosférico. Activa y desactiva Ether Visual y exige
**cero píxeles diferentes**. La campaña host de `SolarVisualEther` ya
comprueba que los límites conservadores no omitan intersecciones de rayos.

Compilar:
```bash
gradle -p solar-android :app:assembleDebug :app:assembleDebugAndroidTest
```

Las mediciones de FPS deben distinguir GPU, shaders, controlador, tiempo
del planificador CPU, transferencia de máscara y sobrecarga de la pantalla.
Una menor cantidad de candidatos no garantiza FPS mayores y no demuestra
aceleración cuántica del hardware.

**Alcance:** esta etapa es una reconstrucción verificable del recorrido
de visibilidad de Solar. No integra ni reemplaza el renderizador CPU
`EtherRender X` para materiales/atmósferas avanzados porque todavía
no existe paridad funcional con `SolarRenderer`.
