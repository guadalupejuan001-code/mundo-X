# MicroLab: partículas, colonia y estado cuántico

Primera implementación interactiva para Android. Entra desde el botón **MICROMUNDO** en XLab. La compilación `-PmicroPreview` se instala como **Mundo X · Micro**, paquete `com.mundox.micropreview`, junto a la app habitual. No necesita servidor para simular.

## Qué puedes hacer

- Orbitar con un dedo, acercar con dos, pausar, avanzar exactamente un paso y reiniciar.
- Elegir entre 2.048, 4.096, 8.192 y 16.384 trazadores; ajustar su difusión y tocar el plano para aplicar un pulso.
- Observar agentes bacterianos simplificados que consumen nutrientes, se desplazan, crecen, se dividen y mueren si su masa cae por debajo del umbral. Tocar para añadir alimento o sembrar 12 células. Ajustar crecimiento.
- Aplicar H, T, S, X y CX a cuatro qubits de `FactorizedCore`. CX usa el qubit seleccionado como control y el siguiente como destino, con vuelta q3→q0.
- Inspeccionar los cuatro vectores de Bloch, entropía local, las 16 probabilidades y fases. Medir los cuatro qubits en Z, conjuntamente, con colapso del estado.
- Conectar o apagar un campo externo derivado de observables cuánticos. Repetir con la misma semilla y los mismos pasos. Las secuencias aleatorias de partículas, células y mediciones son independientes.

## Modelo explícito

Todas las cantidades son adimensionales. El paso es Δt=1/60, independiente de cuántos fotogramas se dibujen. Ambos modelos clásicos siguen avanzando al cambiar de vista; el estado cuántico cambia únicamente con puertas, preparación o medición. Si el dispositivo se retrasa, se limita el trabajo por fotograma y el tiempo del modelo avanza más lentamente que el reloj.

Los trazadores siguen un paso de Euler de una ecuación de Langevin sobreamortiguada: `r ← reflect(r + F(r) Δt + sqrt(2 D Δt) ξ)`. La fuerza combina confinamiento y un campo suave; no incluye colisiones entre partículas, química, átomos ni interacciones moleculares. El pulso modifica posiciones localmente; aumentar el selector crea nuevos trazadores.

La colonia usa una malla 48×48 de nutrientes con difusión y fronteras reflectantes. Cada agente consume `min(N, Δt g m N/(0.25+N))`, convierte el 80% en masa y pierde masa por mantenimiento. Se divide al alcanzar masa 2; las masas hijas suman la masa materna. Hay movimiento orientado con ruido y sesgo por gradiente de nutrientes. No hay fuerzas estéricas entre células ni calibración para una especie; pueden superponerse. Los dos colores identifican linajes visuales con las mismas reglas.

El campo se define de forma deliberada, no como una ley física descubierta:

```
f0 = 0.65 ⟨X2⟩ + 0.45 ⟨X0 X1⟩
f1 = 0.65 ⟨Y2⟩ + 0.45 ⟨X0 Y1⟩
f2 = 0.40 (⟨Z0⟩ − ⟨Z1⟩) + 0.20 ⟨Z3⟩
```

Estos valores entran en la fuerza de los trazadores y el sesgo direccional celular. Apagar el acoplamiento elimina esa influencia, incluso del color de los trazadores. La interacción no implica que las bacterias se simulen cuánticamente. El laboratorio facilita diseñar y comprobar hipótesis visuales sobre el núcleo; no demuestra ventaja cuántica universal.

La entropía `S(q) = H₂((1+|r_q|)/2)` mide el entrelazamiento del qubit con el resto porque el estado global aquí es puro. Las probabilidades se muestran en orden de bits q3 q2 q1 q0. El color representa la fase de la amplitud en la convención del simulador; una fase global no cambia las probabilidades. La vista 3D usa altura proporcional a la raíz de la probabilidad, mientras que las barras usan probabilidad lineal 0–100%.

## Recursos y ejecución

- Cálculo nativo C++ en CPU; GPU OpenGL ES 3 para dibujar un lote de puntos y una rejilla estática.
- Un búfer directo reutilizado de 589.824 bytes, sin objetos Java por partícula.
- Máximo 16.384 trazadores y 2.048 células. Al llenarse la colonia se detiene la división; no se eliminan agentes para ocultar el límite. Las muertes por falta de masa siguen siendo parte del modelo.
- Estimación visible de memoria propia nativa, que excluye memoria Java, GPU y RSS de la aplicación. No es una medición total de ahorro.
- Solicitudes de dibujo a unos 30 Hz cuando avanza; al pausar solo se redibuja por interacción. FPS y tiempo del cálculo corresponden al dispositivo que ejecuta la app, no a una garantía universal.
- La reproducibilidad exacta se garantiza para la misma compilación, semilla, parámetros y secuencia de pasos. Las distribuciones aleatorias estándar y la coma flotante pueden variar entre bibliotecas/plataformas.
- Cambiar de vista conserva la sesión. Cerrar la actividad termina el experimento; esta versión no guarda sesiones completas ni exporta trayectorias.

## Verificación

`native/tests/micro_lab.cpp` comprueba coherencia X/Y tras H y T, entropía de Bell, medición correlacionada, determinismo, aislamiento de secuencias aleatorias, intervención del campo, consumo, crecimiento, división y límites de búfer/trabajo.

`MicroLabInstrumentation` prueba JNI, límites y cierre de sesiones, dibujo real en Android 15, pausa/paso, 16.384 trazadores, controles de puertas y medición, colonia, semilla y pausa/reanudación. El workflow `MicroLab Android preview` conserva APK, capturas y JSON con resultados. La aprobación de estas pruebas debe comprobarse en la ejecución concreta; su presencia en el repositorio no significa que ya hayan pasado.

## Contexto de los modelos

Los modelos de agentes y nutrientes se usan para estudiar crecimiento e interacción microbiana. Esta implementación tiene reglas propias y alcance exploratorio; no reproduce resultados de estos trabajos:

- Strong inter-population cooperation leads to partner intermixing in microbial communities, eLife, https://elifesciences.org/articles/00230
- Spatiotemporal establishment of dense bacterial colonies growing on hard agar, eLife, https://elifesciences.org/articles/41093

El código no representa una formulación verificada de la «fórmula madre» del autor. La regla de acoplamiento es una primera propuesta de implementación que puede sustituirse por una formulación matemática y pruebas acordadas del proyecto.
