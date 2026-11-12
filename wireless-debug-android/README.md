# Mundo X · Depuración inalámbrica para Galaxy Watch4 Classic

APK independiente del teléfono Samsung. Se conecta al reloj con ADB Wireless
Debugging (SPAKE2 para emparejar, TLS para conectarse) y transfiere la APK
mediante ADB sync. Sin computadora y sin cable.

## Cómo usarla

1. Instala Mundo-X-Depuracion-Inalambrica.apk en tu Samsung.
2. Galaxy Watch4 Classic > Ajustes > Acerca del reloj > Información de
   software > toca varias veces Número de compilación para activar el
   menú de desarrollador. Activa Depuración ADB y Depuración inalámbrica.
3. Ambos deben compartir la misma red Wi-Fi, sin aislamiento de clientes.
   Bluetooth por sí solo no sirve para este modo de ADB.
4. En el reloj abre Depuración inalámbrica > Vincular dispositivo con código.
   Verás IP, un puerto de vinculación y un código de seis cifras.
5. Introduce esos tres valores en la APK del Samsung y toca Vincular.
6. Cierra el diálogo del código. En la pantalla principal de Depuración
   inalámbrica del reloj aparece otro puerto de conexión distinto.
7. Introduce el segundo puerto y toca Conectar: se mostrará el modelo del reloj.
8. Toca Instalar Mundo X Enlace y confirma. También puedes elegir otra APK.
9. Desactiva Depuración inalámbrica cuando termines.

El emparejamiento persiste en una identidad RSA privada de la aplicación.
La IP y el puerto de conexión se recuerdan; el puerto puede cambiar al reiniciar
o desactivar la depuración inalámbrica.

## Funcionalidad

- SPAKE2 con código de seis cifras.
- ADB TLS con libadb-android; conexión sin servidor propio.
- Identidad RSA-2048 persistente y certificado X.509.
- Verificación de modelo tras conectar.
- Instalación de Mundo X Enlace compilado en el mismo workflow.
- Instalación de otras APK individuales elegidas desde selector Android.
- Confirmación previa, progreso de copia y respuesta del Package Manager.
- Limpieza del APK temporal tras intentar instalarlo.

## Seguridad y límites

- Utilízala únicamente con tus propios dispositivos autorizados.
- No proporciona root ni cambia el sistema operativo del reloj.
- No se conecta a servidores de Mundo X.
- Tras usarla, desactiva Depuración inalámbrica en el reloj.
- La APK se firma para depuración; la firma puede diferir de otras builds.
- No admite Split APK, APKM ni XAPK: solamente APK individuales.
- Android puede rechazar actualizaciones por incompatibilidad de firma.
- Compilar correctamente no valida el emparejamiento en un Watch4 real.
  Esa prueba necesita el reloj físico.

## Compilar

El workflow .github/workflows/mundo-x-wireless-debug.yml
construye el proyecto watch-link, coloca el APK del reloj dentro de
wireless-debug-android/app/src/main/assets/worldlink-watch.apk,
construye el instalador del teléfono, verifica la firma y lo adjunta.

Dependencias:
- MuntashirAkon/libadb-android 3.1.1 (Apache-2.0 OR GPL-3.0-or-later).
- Conscrypt 2.5.3 (Apache-2.0).
- Bouncy Castle 1.81 (licencia Bouncy Castle).
- SPAKE2-Java transitivo (LGPL-3.0; respetar sus obligaciones).
