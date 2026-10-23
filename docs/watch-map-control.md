# Mundo X 1.3.0 · reloj como mando del mapa
Esta versión incluye **dos APK distintos**, compilados juntos y firmados con la misma clave de depuración:

- **Mundo-X-GHZ-Reloj.apk**: instalar en Galaxy Watch4 Classic. Conserva GHZ simulado y la prueba de sensores; añade «MAPA 4D · MANDO».
- **Mundo-X-Mapa-Telefono.apk**: instalar en Samsung A16 / Android 8 o posterior. Incluye una copia local del mapa 3D/4D.

## Uso
1. Instala el APK del teléfono en el A16. Instala el APK del reloj en el Watch4 mediante tu instalador de APK para Wear OS.
2. Mantén reloj y teléfono emparejados en Galaxy Wearable. Ambos requieren Google Play services compatible.
3. Abre **Mundo X · Mapa** en el teléfono y espera a ver el mapa.
4. En el reloj abre **Mundo X · GHZ → MAPA 4D · MANDO → CONECTAR TELÉFONO**.
5. Cuando aparezca **APLICADO**, usa «VISTA · CAMBIAR» para alternar entre vista, tiempo y campo. Mantén ambas apps abiertas.

| Modo | Controles |
| --- | --- |
| Vista | Giro horizontal, inclinación y zoom |
| Tiempo | Captura anterior/siguiente, reproducir/pausar y reiniciar vista |
| Campo | Corte de altura Z y factor del dipolo de la demo, entre ×0,1 y ×3,0 |

**Dipolo ± modifica el modelo sintético**, no el campo magnético físico. Conserva la captura seleccionada y la cámara. No modifica los valores de un CSV importado. «Reiniciar vista» restablece cámara, altura y tiempo, y conserva el factor del dipolo.

Puedes editar la demo también desde los botones del teléfono. Exportar datos abre el selector de Android para guardar un CSV. Recargar/cerrar la app restablece la demo: guarda el CSV si quieres conservarla. La exportación PNG del visor web se oculta en esta versión Android.

## Qué se conecta
El mando controla el mapa **dentro del APK del teléfono**. La pestaña del sitio web publicado no recibe estas órdenes y no se sincroniza automáticamente. El mapa del APK parte del mismo código (Site commit 2a5ab59abfa13ee2cccd7f056777850304be3568), con controles y exportación Android añadidos.

El enlace usa Wear OS Data Layer MessageClient. Sólo se intercambian órdenes y confirmaciones pequeñas; no se envían muestras de sensores, GPS o datos de salud. Google Play services exige el mismo identificador de paquete y certificado en ambos dispositivos. Prefiere Bluetooth y puede usar infraestructura de Google cuando esa conexión no está disponible. No se implementa un servidor propio ni sockets entre dispositivos.

La escucha sólo se activa mientras las apps están abiertas. El reloj confirma una orden únicamente cuando recibe la respuesta del código del mapa, no por el mero éxito de sendMessage. Hay un tiempo de espera de 4 segundos y no hay reenvío automático de órdenes. El teléfono detecta números de secuencia repetidos y no vuelve a aplicar la misma orden.

## Instalación de pruebas
La versión 1.3.0 corrige `INSTALL_FAILED_NO_MATCHING_ABIS` (`res=-113`):
el APK del reloj incluye ahora el núcleo compilado para **armeabi-v7a (ARM de 32 bits)**,
además de ARM64 y el emulador. La versión 1.2.0 omitía ARM32. CI abre el APK final,
comprueba los encabezados ELF de las tres arquitecturas y verifica sus símbolos JNI.
Las pruebas de ejecución siguen realizándose en el emulador x86_64; la instalación
y ejecución en el Watch4 físico se comprueban con el dispositivo del usuario.

Este error indica incompatibilidad de arquitectura del APK; no se corrige cambiando
Wi-Fi, puertos o el instalador ADB. Puedes seguir usando tu app de depuración.
Instala los dos APK de esta entrega como pareja para conservar la firma coincidente.

Estos APK no se instalan automáticamente al abrir un enlace. El APK del teléfono no sustituye la instalación del APK del reloj.
Si existe una versión de depuración anterior con otro certificado, Android puede rechazar la actualización. Desinstalarla borra sus datos privados, incluido hardware-latest.json. Conserva cualquier registro que necesites antes de hacerlo. Ambos APK de esta entrega deben instalarse como pareja; sus certificados se comprueban en CI.

## Límites de lo comprobado
Se prueba el modelo numérico y, en un emulador Android, la ruta petición → protocolo → WebView real → modificación → confirmación, incluyendo duplicados. Esto no verifica todavía el transporte Bluetooth ni la instalación en tu Watch4/A16 físicos. El emparejamiento y la respuesta real entre tus dos dispositivos son la comprobación que falta.

No se generan qubits físicos, superposición física ni un mapa medido de tu entorno. Las mediciones espaciales requieren posiciones conocidas y orientación común; el JSON actual del reloj todavía no contiene esos datos.

Fuentes de implementación:
- https://developer.android.com/training/wearables/data/overview
- https://developer.android.com/training/wearables/data/events
- https://developer.android.com/develop/ui/views/layout/webapps/load-local-content



## Campos + objetos (1.3.0)

Debajo del mapa, **Campos + objetos** permite seleccionar Magnético, Gravedad o
Ambos y mover 12 partículas virtuales. El mando tiene páginas OBJETOS y FUERZAS.
B virtual se regula entre −3 y +3 (0 lo apaga y el signo invierte el campo);
la gravedad entre 0 y 3. Reiniciar objetos repone posiciones, velocidades y
reloj simulado sin modificar los CSV ni el factor del dipolo del mapa.

El campo magnético es la predicción del modelo del mapa, en teslas después de
convertir µT, multiplicada por B virtual. Se integra la fuerza q(v × B) con
q/m = 100.000 C/kg usando rotaciones de Boris. Una partícula inmóvil no recibe
fuerza magnética. La fuente gravitatoria es una esfera permeable de densidad
uniforme centrada en el volumen: g = −μ r/max(|r|,R)^3. A escala 1, μ =
0,0002 m³/s², equivalente a unos 3 millones de kg **virtuales**. Las paredes
reflejan las partículas. No hay interacción entre partículas, realimentación
sobre las fuentes, fuerzas eléctricas ni relatividad.

El integrador divide gravedad y rotación magnética de forma simétrica con
pasos de 1/480 s, acota a 50 ms simulados por dibujo y busca hasta 30 dibujos/s.
No se afirma ese rendimiento en el A16: el tiempo mostrado es simulado y se
ralentiza cuando el dispositivo no alcanza el ritmo. La reproducción se pausa
al salir de la app. Una muestra magnética del modelo superior a 500 µT antes
del factor detiene el laboratorio; no se recorta silenciosamente.

## Mediciones del espacio físico

En el mando, **MEDIR SENSORES** habilita el magnetómetro calibrado y el sensor
TYPE_GRAVITY que Android exponga. El reloj muestra B en µT y g en m/s². Envía
una observación por segundo al mapa abierto del teléfono, con vector, edad,
precisión, modelo y marca de emulador. La solicitud de muestreo es de 10 Hz;
la tasa efectiva depende del dispositivo. Los sensores se desregistran al
pulsar detener o salir de la pantalla. No hay servicio en segundo plano.

El teléfono acepta observaciones sólo del nodo y sesión que hayan recibido
confirmación reciente del mapa. Rechaza secuencias repetidas, tipos incorrectos,
vectores no finitos y edades excesivas; retira las lecturas cuando caducan.
Una ausencia se muestra como raya, nunca como cero. La precisión Android se
muestra sin convertirla en una garantía de calibración. Las lecturas usan los
ejes del reloj, no incluyen posiciones 3D y no se insertan automáticamente
como puntos TRAIN/HOLDOUT ni gobiernan la gravedad virtual.

**VIBRACIÓN + SENSORES** abre el experimento físico existente de 13 segundos.
Permite comparar una orden del actuador con la respuesta de sensores. Un
cambio magnético durante un pulso no prueba por sí solo un campo externo
controlado: también pueden influir movimiento e interferencias del propio
reloj. No se extrapola a atracción de objetos ni entrelazamiento.

## Generación magnética física: integración pendiente

Esta entrega no contiene un controlador de electroimán. Para producir un campo
externo regulable hace falta un módulo físico identificado: bobina/electroimán,
alimentación apropiada y etapa de corriente con protección inductiva, gobernada
por un controlador con interfaz accesible (por ejemplo BLE). Hace falta conocer
el módulo y sus límites antes de definir su protocolo. No conectar una bobina
directamente a un GPIO ni a los contactos del reloj.

La siguiente integración debe confirmar corriente y estado reales del módulo,
apagar la salida al perder el enlace y validar el campo con un sensor y una
geometría conocidos. El magnetómetro del reloj es una observación local, no una
medición completa del espacio. La app no puede generar gravedad regulable
mediante el hardware del reloj. Esos controles pertenecen sólo a la simulación.

Referencias:
- https://developer.android.com/develop/sensors-and-location/sensors/sensors_position
- https://developer.android.com/develop/sensors-and-location/sensors/sensors_motion
- https://developer.android.com/reference/android/hardware/SensorEvent
- https://arxiv.org/abs/1701.05605 (rotación magnética e integración de partículas)
- https://pwg.gsfc.nasa.gov/Education/wmfield.html (corriente y campo magnético)

Las pruebas numéricas incluyen movimiento inercial, giro ciclotrón analítico,
conservación de rapidez magnética y órbita gravitatoria. Las pruebas Android
usan WebView real y mensajes de sensores sintéticos etiquetados como fixtures.
No sustituyen la prueba de sensores/transporte en el Watch4 y el A16 físicos.
