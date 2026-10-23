# X Optical Lab: ingeniería inversa del receptor del Galaxy Watch4 Classic

## Propósito

Proyecto **Wear OS independiente de Mundo X** para observar cómo recibe señales el Samsung Galaxy Watch4 Classic. Solo lectura de señales disponibles a través de APIs públicas/documentadas: no modifica firmware, voltajes, controladores LED, bootloader ni hardware del usuario. Sin red ni registro oculto: el usuario pulsa para empezar; al salir de la app se detiene, con sesiones de hasta 60 segundos.

No es un experimento de entrelazamiento cuántico físico. Es instrumentación óptica y electrónica clásica. La salida del PPG es un dato **digital procedente de un convertidor analógico-digital (ADC)**, no un registro de fotones o de fase cuántica.

## Cadena física que investigamos

LED verde/rojo/IR (según hardware y modo) -> piel (absorción + dispersión) -> fotodiodo -> corriente -> circuito analógico de recepción/transimpedancia -> filtrado/sustracción ambiental -> ADC -> controlador/firmware -> Samsung Health Sensor Service -> Watch app -> archivo CSV.

iFixit encontró un chip Texas Instruments AFE4500S de biosensado óptico en el desmontaje del Watch4 (ver fuentes). Esto **no demuestra** que podamos cambiar sus registros eléctricos desde nuestra app. Los sensores PPG no son sensores cuánticos de estado coherente.

## ¿Qué compila sin dependencias cerradas?

- Botón **Ver sensores**: enumeración mediante Android SensorManager. Muestra si existen acelerómetro, giroscopio, sensor de luz ambiental frontal y sensor de pulso procesado.
- Botón **Registrar 60 s**: registra acelerómetro, giroscopio y luz ambiental si el firmware permite esos sensores; deja CSV local app-private.
- Botón **PPG óptico bruto**: solicita permiso Body Sensors. **Solo funciona si el desarrollador aporta el archivo oficial AAR de Samsung** y el reloj permite PPG_CONTINUOUS.
- El archivo CSV contiene wall_ms, source, device_time, tres campos numéricos de señal y tres campos de status. Los timestamps de SensorManager son ns desde arranque; los de PPG de Samsung son ms según su documentación. No mezclar sin conversión.
- No tiene permiso INTERNET, grabación de audio ni acceso a datos de otras apps.

## Activar captura óptica oficial (opt-in)

1. Obtén el **Samsung Health Sensor SDK** desde la documentación del portal Samsung Developer, siguiendo sus condiciones y uso de desarrollo.
2. Coloca el archivo oficial sin modificar en app/libs/samsung-health-sensor-api.aar dentro de watch-optics-lab.
3. Ejecuta gradle -p watch-optics-lab :app:assembleDebug. Gradle detecta el AAR y compila también app/sdk-src/com/mundox/watchoptics/SamsungPpgReader.java. Sin el archivo, el APK funciona solo con los sensores Android comunes y muestra explícitamente que PPG bruto no está conectado.
4. Instala el APK en un Watch4 real. Concede Body Sensors solo si deseas usar PPG. Para el prototipo de desarrollo, consulta la documentación oficial del **developer mode del Health Sensor Service**, las reglas de pruebas y el proceso de colaboración para distribución.
5. Abre la app, inicia la sesión y pulsa PPG óptico bruto. La app consulta primero la lista de capacidades del reloj: si PPG_CONTINUOUS no está disponible, lo reporta. Si el servicio deniega el acceso, muestra el error; nunca simula muestras.

No se distribuye el AAR de Samsung, y el CI sin AAR no valida integración específica Samsung. El soporte real de PPG depende del modelo, software, permisos y versión del servicio. No hay conexión a smartphone ni sincronización de datos en v0.1.

## Experimento científico mínimo

Hacer sesiones cortas y etiquetarlas manualmente:
1. Reloj bien ajustado sobre la muñeca, quieto (referencia).
2. Reloj bien ajustado, movimiento suave (artefacto mecánico).
3. Reloj fuera de la muñeca, sin contacto con piel (control).
4. Cambios normales de iluminación alrededor del reloj (artefacto ambiental).

No usar láseres, abrir la carcasa, pegar componentes al sensor ni introducir señales eléctricas en la electrónica. No enviar datos personales a internet. Medir si las lecturas cambian en función de los controles y si el firmware informa error/ausencia de contacto. No inferir coherencia cuántica de las fluctuaciones del ADC.

## Construcción y evidencia

Gradle 8.9, JDK17 y Android SDK platform 35. Comandos:

    gradle -p watch-optics-lab --no-daemon :app:assembleDebug

APK debug: watch-optics-lab/app/build/outputs/apk/debug/app-debug.apk

La acción watch4-optics-lab.yml puede compilar un **APK base sin AAR** y publicarlo como artefacto del workflow. Solo el usuario puede ejecutar la app en su reloj; un emulador no valida el hardware óptico y el SDK Samsung no lo admite.

El CSV privado de la app se recupera con depuración ADB mediante el paquete com.mundox.watchoptics; un primer registro puede inspeccionarse en pantalla sin exportación. No se promete apertura de archivos CSV desde el reloj en v0.1.

## Referencias técnicas

- iFixit Watch4/Classic teardown (AFE4500S): https://www.ifixit.com/Teardown/Samsung+Galaxy+Watch4+and+Watch4+Classic+Teardown/144595
- Samsung Sensor SDK overview: https://developer.samsung.com/health/sensor/guide/introduction.html
- Samsung PpgSet API (PPG_GREEN, PPG_RED, PPG_IR): https://developer.samsung.com/health/sensor/api-reference/com/samsung/android/service/health/tracking/data/ValueKey.PpgSet.html
- Samsung sensor specifications (PPG 25 Hz): https://developer.samsung.com/health/sensor/guide/data-specifications.html
- SDK permission request: https://developer.samsung.com/health/sensor/guide/permission-request.html
- Samsung development policies: https://developer.samsung.com/health/sensor/faq.html
