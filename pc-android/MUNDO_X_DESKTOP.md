# Mundo X Desktop 0.5.0 — uso cotidiano en Android

Mundo X Desktop es la **pantalla de inicio** de la APK `MUNDO-X-DESKTOP.apk`.
Al instalarla en un dispositivo Android compatible, aparece una única entrada
**Mundo X** en el lanzador del teléfono. La computadora anfitriona continúa
siendo Android; Mundo X es un entorno de trabajo con herramientas propias y una
ruta real hacia el runtime cuántico de software, no un sistema operativo
independiente ni una QPU física.

## Aplicaciones disponibles

| Aplicación | Qué hace realmente | Tecnología |
| --- | --- | --- |
| Inicio | Accesos a las aplicaciones del mismo paquete | Activity Android / UI propia |
| Navegador | Buscar, visitar sitios, cargar JavaScript, subir archivos y guardar descargas hasta 100 MiB en un documento elegido | WebView y red Android |
| Notas | Editar texto sin red, autoguardado en almacenamiento privado, importación/exportación .txt | Almacenamiento interno Android + SAF |
| Documentos | Abrir documentos mediante selector Android, previsualizar textos hasta 256 KiB y delegar formatos no compatibles a otras apps | Android Storage Access Framework |
| Quantum Workstation | Editor, terminal XI con núcleo persistente, circuitos XASM de hasta 1.000 qubits (con puertas T y límites de presupuesto), archivos .xi/.x, sesiones .mqx | C++ propio (QuantumRuntime, InfiniteCore, Inverse Ether Memory, run_xasm) |
| Fusion Lab | Tabla periódica de 118 elementos, selección de isótopos, canales nucleares conocidos, cálculo de energía Q y simulación visual conceptual | Java offline: FusionPhysics + FusionLabActivity |
| COSMIC X PC | Emulador x86/x64 original, vídeo, arranque y puente con InfiniteCore | Motor nativo existente |

## Cómo usarla de verdad

1. Abre **Mundo X** en el lanzador Android.
2. En **Web**, escribe una dirección o una búsqueda. El navegador utiliza la conexión del teléfono. Si una página inicia una descarga, selecciona un archivo de destino; el contenido se copia ahí sin requerir permisos generales de almacenamiento.
3. En **Notas**, escribe sin pulsar guardar. Los cambios se guardan en el almacenamiento privado; puedes usar **Exportar .txt** para sacar tus notas del dispositivo o **Importar .txt** para reemplazarlas.
4. En **Archivos**, elige un documento real. Los archivos de texto se muestran en la app; otros formatos pueden abrirse con las apps de Android instaladas. Un permiso de lectura persistente permite reabrir el último documento compatible.
5. Desde **Inicio**, abre **Quantum Workstation**. Ejecuta XI para acumular cambios persistentes en Ether o XASM para lanzar circuitos nuevos con el perfil Reduced/Compact. La herramienta crea además un autoguardado local del estado XI al salir y trata de restaurarlo al regresar; exporta una sesión .mqx para tener una copia externa.
6. Desde **Inicio**, abre **COSMIC X PC** para acceder al emulador original.

## Seguridad y límites

- Android suministra Wi-Fi/red móvil, almacenamiento, teclado, pantalla, WebView y drivers. No se representan como trabajo ejecutado por qubits.
- El navegador admite páginas HTTP(S), bloquea acceso directo a archivos locales desde WebView, no registra interfaces JavaScript privilegiadas y permite delegar `mailto:` / `tel:` a otras aplicaciones. La navegación usa los servicios de cookies y JS del motor WebView; los sitios visitados pueden recoger información como en otros navegadores.
- Quantum Workstation intenta recuperar automáticamente la última sesión XI desde un archivo privado guardado de forma atómica. La salida abrupta antes de finalizar el checkpoint puede perder los últimos cambios; los resultados XASM no se serializan como estado.
- Las notas se guardan sin sincronización en nube. **Desinstalar la aplicación borra los datos privados**; exporta antes cualquier nota importante. Android puede finalizar el proceso y las escrituras pendientes no están garantizadas ante una interrupción eléctrica o cierre abrupto.
- Las descargas se limitan a **100 MiB** y requieren elegir un destino. Si falla una transferencia, puede quedar un documento incompleto que conviene eliminar.
- La pestaña Archivos no es un administrador de todo el almacenamiento; abre recursos concedidos por el usuario a través del selector.
- Quantum Workstation usa representaciones de estados cuánticos **simulados en hardware clásico**. Sus backends XI y XASM son diferentes y no se convierten automáticamente uno en otro.
- La pantalla de inicio incluye un motivo gráfico orbital dibujado con Canvas Android: **no es Ether Render X**. El trabajo futuro consiste en usar el renderizador propio cuando sus interfaces y rendimiento se integren realmente en el escritorio.

## Paquete y compilación

- **APK:** `MUNDO-X-DESKTOP.apk` (el artefacto mantiene también el alias `COSMIC-X-PC.apk` por compatibilidad)
- **Package:** `com.cosmicx.pc`
- **Versión:** 0.5.0 (versionCode 5)
- **Mínimo:** API 26; Android 8.0
- **ABIs:** ARM64-v8a y x86_64
- **Entry point:** `MundoXDesktopActivity`
- **Código:** `pc-android/app/src/main/java/com/cosmicx/pc/MundoXDesktopActivity.java`
- **Compilación Android e instrumentación:** `.github/workflows/own-pc-android.yml`

No instala Windows, no altera el lanzador del sistema Android y no concede
acceso a aplicaciones Android externas a los qubits simulados. Esta versión
prioriza tareas reales (navegación, archivos, notas) junto con una superficie
de trabajo explícita para el núcleo propio.
