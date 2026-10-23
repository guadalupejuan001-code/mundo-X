# Solar: Auto fluido conectado a Ether — etapa de renderizado

Referencia: PR #60, basado en `main` tras la fusión del PR #54
(Ether Memory con 256 páginas de copy-on-write).

## Qué se ha corregido

El primer borrador del modo adaptativo nunca se activaba en la aplicación real:
`SolarActivity.onCreate` invocaba `setQuality`, que desactivaba
`adaptivePreview`, y la UI no ofrecía una opción para reactivarlo. Además,
medía solamente el tiempo CPU transcurrido dentro de `onDrawFrame`, que no
incluye necesariamente las esperas de GPU/sincronización.

La implementación corregida incorpora un controlador **separado y verificable
sin Android**, `SolarAdaptiveQuality`, que observa intervalos entre comienzos
de fotogramas. Esto aproxima el ritmo efectivo de cuadros, aunque no es un
contador de tiempo GPU ni acredita una velocidad concreta en Samsung.

- **Auto fluido** aparece en `Calidad de imagen` y `Observatorio`. Está
  activado por defecto en la app normal nueva, salvo preferencia previa;
  las APK UHD 4K/8K mantienen su modo explícito por defecto.
- Previsualización máxima entre **480 y 1280 px**. Tras seis intervalos lentos
  (≥29 ms) reduce 160 px. Dos intervalos muy lentos (≥100 ms) reducen 240 px.
  Noventa intervalos rápidos (≤18 ms) elevan 80 px, incluso a 60 Hz. La histéresis evita un
  rebote continuo.
- Un primer fotograma, reanudación tras suspensión prolongada, pausa,
  captura PNG, entrada a atmósfera, cambio de orientación o modo UHD no
  alimentan las muestras. **Auto no modifica las fechas, fuerzas, órbitas,
  estado Ether, RNG ni las fuentes científicas**.
- Los ajustes `Ahorro`, `Equilibrada`, `Alta`, `4K`, `8K`
  siguen siendo modos manuales. Una selección UHD no se degrada de forma
  silenciosa. El botón superior identifica Auto fluido y la preferencia
  `fluidPreview` se conserva tras recreación.
- Las pruebas de la lógica autónoma (`SolarAdaptiveQualityChecks`) se
  ejecutan en las tres variantes de CI junto a las pruebas astronómicas.
  `SolarInstrumentation.resolutionBudget` activa el modo desde la Activity
  real, comprueba la resolución permitida, su desactivación al seleccionar
  modo manual y la conservación de la revisión del núcleo.

## Separación real de responsabilidades

`QuantumSolarBridge` conserva la autoridad de las observaciones cuánticas;
`SolarData` calcula posiciones, tiempo y datos físicos. El motor GLES procesa
la imagen y es el único que decide qué resolución asignar a los framebuffers.
Una decisión de calidad es únicamente una **vista derivada**.

La optimización de Inverse Ether Memory del PR #54 afecta al runtime cuántico.
No significa que Solar GPU use qubits como hardware ni que la memoria lógica
evite texturas, framebuffer o muestras de sombreado. El puente de Solar sigue
consultando `NativeCore`, por lo que solo una traza de rendimiento de ese
recorrido permitiría atribuirle nuevas mejoras.

## Cómo verificar

1. `javac -d /tmp/solar-tests solar-android/app/src/main/java/com/mundox/solar/SolarAdaptiveQuality.java solar-android/tests/SolarAdaptiveQualityChecks.java`
2. `java -cp /tmp/solar-tests com.mundox.solar.SolarAdaptiveQualityChecks`
3. Compilar la app Android y su paquete de instrumentación con el workflow
   `solar-android.yml`; repetir las variantes 4K/8K.
4. Ejecutar Android 15 instrumentado y comparar estado, imágenes, memoria
   PSS y consumo CPU/GPU en el dispositivo objetivo. No atribuir a la
   política mejoras de FPS antes de medirlas.

**Alcance honesto:** Solar conserva el renderizador, shaders y modelos
originales. Es una primera optimización efectiva del coste de visualización
y de su elección de calidad; **no es una reescritura completa de la escena,
de la física o del hardware gráfico**. Las siguientes mejoras deben medirse
en cuellos de botella por frame, buffers y transferencia CPU→GPU.
