# COSMIC X Solar 1.6 — Cine X / presentación

Base de integración: `main` en `c375159fdb17e202e452af7bd0299b16a3ec6fff`.
Reutiliza el renderizador `SolarRenderer`, el shader `solar.frag` de COSMIC,
el puente `QuantumSolarBridge` con su sesión Ether compartida y los
datos astronómicos originales. No sustituye ni duplica estos motores.

## Experiencia para una demostración

1. Abrir **Tierra** o **Saturno** y tocar **Presentar** junto a **Guardar PNG**.
   El botón activa Cine X, prepara un máximo inicial de 1280 px para la vista
   adaptable y oculta los paneles inferiores. La cámara actual se conserva.
2. Mover la cámara, observar iluminación y anillos. El control **☰** recupera
   los paneles sin cerrar la simulación.
3. Con Cine X encendido, **Foto 4K** solicita una imagen real de 3840×2160
   o 2160×3840 sin tener que navegar permanentemente a 4K. El estado del
   visor anterior (manual o Auto fluido) se restaura después de la lectura.
4. Para ver el shader original, desactivar **Cine X** en **Observatorio →
   Calidad de imagen**. Se puede volver a activarlo sin reiniciar.

## Calidad óptica

Los cambios son **opcionales** y solo se aplican cuando
`uCinematic=1` en el shader GLES de Solar:

- Destellos de estrellas con anchura filtrada por la huella angular del píxel.
- Realce tenue de dispersión en el limbo y el terminador de atmósferas.
- Exposición propia para superficies: conserva tonos de Saturno que antes
  tendían a perderse en blancos y deja ver mejor océanos bajo nubes
  procedurales en la Tierra.
- Bandas secundarias en anillos con amortiguación de subpíxel.
- Corona solar con halo y destellos geométricos limitados por oclusión.
- Grado fotográfico de color moderado, tras el tone mapping original.

Son modelos de *apariencia/cámara*, no nuevas mediciones astronómicas ni
pruebas de dispersión atmosférica resuelta físicamente. No alteran
órbitas, tamaños, energía, gravedad, parámetros JPL, mediciones
cuánticas, semilla ni el estado del núcleo Ether.

## Memoria, capturas y fallos

La captura temporal 4K requiere memoria para FBO de color/profundidad y
lectura RGBA además de las texturas del sistema. No se promete que cualquier
teléfono pueda reservarla. Si la GPU rechaza el framebuffer UHD, el flujo
rechaza una captura de resolución inferior; **nunca reescala el visor y lo
presenta falsamente como 4K**. Se restaura el estado anterior y la app
permite seguir navegando.

El modo 4K/8K nativo permanece disponible y no se rebaja silenciosamente.
Cine X por sí solo no garantiza más FPS; un Samsung de gama modesta puede
necesitar Auto fluido o calidad manual menor mientras navega.

## Verificación

- A/B Android con Cine X OFF/ON/OFF sobre escenas controladas de Tierra y
  Saturno; exigir cambios de imagen visibles y restauración exacta en OFF.
- Revisar que la revisión del núcleo y la fecha no se modifican al alternar
  el modo óptico.
- Ejecutar variantes normal, 4K y 8K del workflow, pruebas científicas
  y comparaciones de referencia con Cine X apagado.
- Validar el PNG 4K de presentación en dispositivo objetivo y registrar PSS,
  memoria GPU y tiempo de captura. El emulador Android no prueba la
  fluidez del Samsung físico.

**Alcance**: mejora visual presentable y flujo para fotos UHD bajo demanda.
No se atribuyen a Ether Memory aceleraciones de los shaders sin medición
causal. Tampoco se afirma simulación cuántica de fotones o radiación física.
