# WorldLink Campo: predicción física con memoria Ether

Versión 1.0. Protocolo exploratorio; prueba física en Galaxy Watch4 pendiente.
Base: PR #57, commit `5d6cf0074e207aded03380e41356be4ad6a2cd66`.

## Uso

1. Instala `WorldLink-Campo-Telefono.apk` en el Samsung y
   `WorldLink-Campo-Reloj.apk` en el Galaxy Watch mediante tu depurador inalámbrico.
2. Abre **WorldLink Campo** en ambos dispositivos. En el reloj pulsa
   **INICIAR SENSORES**. En el teléfono espera «Reloj enlazado».
3. Pulsa **INICIAR PRUEBA · 50 s** en el teléfono. Mantén la muñeca aproximadamente
   en el mismo punto y gírala despacio alrededor de varios ejes. Mantén iguales
   las condiciones del entorno durante las tres fases y ambas pantallas abiertas.
4. Lee el resultado y pulsa **EXPORTAR MEDICIONES CSV**. Elige dónde guardarlo.
   El archivo exportado permite revisar y reproducir el análisis.

Esta pareja usa el identificador `com.mundox.worldlink.field` y una firma común.
Se instala junto a WorldLink MIL–Ether anterior; no necesita reemplazarlo ni
eliminar datos. Los sensores se detienen al cerrar la pantalla del reloj.
La sesión se interrumpe si se cierra el teléfono o se pierde la señal.

## Pregunta y modelo

¿Una regla con seis coeficientes predice el campo observado en nuevas orientaciones?

`m_device = transpose(R_game) * B_reference + bias_device`

Se ajustan tres componentes de un campo aproximadamente uniforme y tres sesgos
fijos del sensor. La matriz de orientación viene exclusivamente de
`TYPE_GAME_ROTATION_VECTOR`, sin usar el magnetómetro para definir la orientación.
El marco tiene un norte arbitrario y puede derivar por error del giroscopio.
El campo se obtiene de `TYPE_MAGNETIC_FIELD_UNCALIBRATED`, en microteslas.
No se sustituye silenciosamente por orientación geomagnética ni por datos simulados.
Si falta cualquiera de los sensores, la app declara que no puede ejecutar la prueba.

La fuente Android explica la independencia y la deriva:
https://developer.android.com/develop/sensors-and-location/sensors/sensors_position

## Participación de Ether

El ajuste es regresión clásica por mínimos cuadrados. No se afirma haber
descubierto una ley nueva ni que el ajuste sea una operación cuántica física.
Los seis coeficientes (48 bytes de carga útil, más el coste de su estructura)
se publican atómicamente en el `QuantumRuntime`/`InverseMemory` canónico de Mundo X,
con fondo cero y excepciones en direcciones 2000000–2000047. Cada predicción lee
la regla de esa memoria, verifica que sigue congelada y calcula la proyección
solicitada. No se crean qubits activos. El núcleo MIL no se inicializa en este modo.
Se conservan las observaciones completas en CSV como evidencia independiente.

## Protocolo fijado antes de medir

- 0–20 s: ajuste del campo y sesgo, mínimo 100 muestras.
- 20–30 s: modelo congelado; calibración de un radio de error mediante el
  percentil empírico 95 del error vectorial, mínimo 50 muestras.
- 30–50 s: prueba cronológica reservada, mínimo 100 muestras. No se modifica
  el modelo ni el radio usando estas observaciones.
- Máximo 10 transmisiones/s, 650 registros, 50 ms entre campo y orientación.
- Tiempos monótonos del reloj, secuencia y sesión identificadas. Duplicados y
  muestras inválidas se descartan; un salto de sensores >1 s, una pérdida de
  recepción >2 s, un cambio de reloj o un reinicio de referencia abortan la prueba.
- Se rechaza un ajuste sin orientaciones suficientes mediante rango y condición
  de la matriz normal (máximo 10000). Esto evita inventar parámetros no identificables.

Resultados: RMS vectorial en µT, RMS de una referencia constante (media del
entrenamiento), RMS de persistencia (última lectura de la fase de prueba),
fracción dentro del radio empírico y número de lecturas/descartes.

El mensaje «Predicción reproducida en esta sesión» requiere reducción de error
de al menos 20% frente a la referencia constante y al menos 80% dentro del radio.
Son criterios exploratorios fijados de antemano, **no significancia estadística**.
Las muestras están correlacionadas temporalmente; el radio del percentil 95
no garantiza 95% de cobertura ni es un intervalo de confianza universal.
Persistencia puede ganar en movimientos lentos: se publica su error también.

Una sesión favorable valida únicamente este modelo local, en estas condiciones.
No prueba nuevas interacciones, entrelazamiento, superioridad frente a todos los
métodos de identificación ni conexión a una «lattice universal».

## Datos y límites

No solicita pulso, vibraciones, sonido ni cámara en la pantalla del experimento.
Usa Wear Data Layer entre tus dispositivos. Al finalizar o abortar se guarda un
CSV en el teléfono; cada sesión se archiva y se conserva un acceso a la última.
Los archivos internos desaparecen si se borran los datos o se desinstala la app;
exporta los resultados que quieras conservar. Un cierre forzoso del proceso
durante captura puede perder la sesión aún no guardada.

El CSV incluye los valores crudos, dos tiempos de sensor, orientación, secuencia,
precisión reportada por Android, fases, predicciones, errores, parámetros y estado.
La precisión Android se conserva como metadato; no certifica la exactitud física.
El modelo no corrige todos los efectos de hierro blando, deriva, gradientes,
temperatura, movimiento del brazo o campos de otros dispositivos. Sus errores
deben aparecer en la prueba; no se ocultan mediante reajuste durante el test.

## Verificación reproducible

El workflow `.github/workflows/worldlink-field.yml` ejecuta:

1. Pruebas Java de recuperación de parámetros, convención de rotación,
   separación temporal, congelación, cambio físico sintético, singularidad,
   duplicados, reinicios, tiempos y valores inválidos.
2. C++ sobre la memoria Ether original: bytes exactos, rechazo de NaN,
   conservación del estado anterior, fondo cero y cero qubits activos.
3. JNI real en anfitrión, compilación y lint Android, firmas/IDs coincidentes
   y símbolos de memoria empaquetados en la biblioteca arm64.

Los datos sintéticos solo existen en tests de código. No se muestran como
mediciones del usuario. La disponibilidad y precisión del Watch4, Wear Data
Layer de extremo a extremo y ejecución de 50 segundos requieren sus dispositivos.

Compilación: `gradle -p watch-link -PfieldPrediction=true :app:assembleDebug`
y `gradle -p worldlink-phone -PfieldPrediction=true :app:assembleDebug`.
Sin esa propiedad se conservan las aplicaciones anteriores.
