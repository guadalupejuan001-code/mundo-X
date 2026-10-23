# Mundo X 1.2 · reloj como mando del mapa
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
Estos APK no se instalan automáticamente al abrir un enlace. El APK del teléfono no sustituye la instalación del APK del reloj.
Si existe una versión de depuración anterior con otro certificado, Android puede rechazar la actualización. Desinstalarla borra sus datos privados, incluido hardware-latest.json. Conserva cualquier registro que necesites antes de hacerlo. Ambos APK de esta entrega deben instalarse como pareja; sus certificados se comprueban en CI.

## Límites de lo comprobado
Se prueba el modelo numérico y, en un emulador Android, la ruta petición → protocolo → WebView real → modificación → confirmación, incluyendo duplicados. Esto no verifica todavía el transporte Bluetooth ni la instalación en tu Watch4/A16 físicos. El emparejamiento y la respuesta real entre tus dos dispositivos son la comprobación que falta.

No se generan qubits físicos, superposición física ni un mapa medido de tu entorno. Las mediciones espaciales requieren posiciones conocidas y orientación común; el JSON actual del reloj todavía no contiene esos datos.

Fuentes de implementación:
- https://developer.android.com/training/wearables/data/overview
- https://developer.android.com/training/wearables/data/events
- https://developer.android.com/develop/ui/views/layout/webapps/load-local-content

