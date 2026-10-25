# ETHER X File Laboratory — Android nativo 0.2.0

Prototipo Android completamente independiente de Python, Termux y servidores. Su interfaz nativa permite **analizar**, **compactar** y **reconstruir** archivos/ISO elegidos por el usuario desde el selector seguro de Android. Los datos no abandonan el dispositivo.

## Características implementadas

- **Inspector de firmas:** ISO9660, ISO9660/Joliet, firma UDF, EXF, ZIP/APK, PNG, JPEG, PDF, EXE/MZ, WIM, 7z, GZIP, XZ, Zstandard y MP4.
- **ISO:** lee metadatos ISO9660/Joliet de los descriptores y enumera entradas de directorio raíz cuando caen en los primeros 512 KiB; no extrae el árbol UDF, no monta ni arranca imágenes.
- **Compactación reversible:** archivo `.exf` versión 1 en bloques de 1 MiB, codificador **regla uniforme / regla de fondo con excepciones / referencia a bloque anterior / zlib / literal**, elige la menor carga; SHA-256 global y CRC32 por bloque.
- **Descompresión:** decodifica `.exf` con clases `RAW`, `ZLIB`, `FILL`, `SPARSE` y `REFERENCE`, incluyendo archivos escritos por el prototipo Python v0.1 (rápido/equilibrado). Para restaurar primero crea un temporal privado, verifica SHA-256 y solo entonces copia al destino seleccionado.
- **Q-Lab:** experimento educativo de clasificación con dos qubits simulados mediante amplitudes complejas/rotaciones y una búsqueda de parámetros; compara contra clasificador clásico. Trabaja sobre ejemplos sintéticos, no entrena con el archivo del usuario ni decide la compresión real.

**Compatibilidad incompleta:** el decodificador actual no admite bloques LZMA (tipo 2) de los `.exf` creados por la versión Python en modo `maximo`. El modo “Máximo” nuevo usa zlib 9 en su lugar. Los metadatos y bloques se validan; un método no soportado produce un error claro y no toca el origen.

## Ejecutar las pruebas sin instalar Android

Requiere JDK 17+:

```sh
bash tests/test-host.sh
```

Para comprobar interoperabilidad con el programa anterior, genera un EXF con el Python original y ejecuta:

```sh
bash tests/test-host.sh tests/demo.iso ruta/archivo_python.exf
```

La suite comprueba archivos vacíos, uniformes, dispersos, aleatorios, referencias, CRC/SHA, truncamientos y resultados Q-Lab con semilla fija.

## Compilar APK

Para producir un APK instalable con **Gradle 8.9, Java 17 y Android SDK 35**:

```sh
gradle --no-daemon :app:assembleDebug
```

APK resultante: `app/build/outputs/apk/debug/app-debug.apk`.

Sin ordenador: crea un repositorio GitHub nuevo, sube **el contenido** de esta carpeta, abre la pestaña **Actions** y ejecuta **ETHER X Native Android**. Descarga el artefacto `ETHER-X-debug-apk` cuando la compilación termine. No hace falta Python ni Termux en Android. Este código no incluye un APK precompilado; el archivo APK solo estará disponible después de un build verificado.

La firma `debug` es únicamente para probar. Para distribución pública, crear configuración de firma release y verificar las huellas de certificado.

## Relación con Mundo X

El motor Java aplica al almacenamiento clásico el **principio arquitectónico** de Inverse Ether Memory: una regla global describe valores repetidos, las diferencias se registran como excepciones y los datos se materializan solo al reconstruir. El compresor no incorpora directamente `InfiniteCore`/`InverseMemory` C++ ni afirma que la compresión esté usando qubits físicos.

**Diseño separado:** no se altera Mundo X ni se afirma mejora cuántica sin benchmark. Se puede añadir como un subproyecto `etherx-android` en Mundo X sin modificar su ruta cuántica principal.

## Limitaciones operativas

- Para cualquier ISO grande, la salida requiere espacio libre suficiente. **Reconstruir exige adicionalmente un temporal de tamaño igual al archivo original**, para preservar integridad antes de publicar el resultado. No constituye compresión en sitio.
- La app mantiene unos pocos bloques en memoria (1 MiB), pero la restauración utiliza almacenamiento privado.
- El análisis lee solo la cabecera y hasta 512 KiB, no interpreta la totalidad del sistema de archivos de Windows ni extrae WIM/UDF.
- La reducción depende de la información; archivos cifrados, con mucha entropía o precomprimidos pueden crecer.
- El uso de `.exf` no sustituye el formato ISO; hay que reconstruir `.iso` antes de arrancar Windows desde esa imagen.
- El experimento Q-Lab es una **simulación clásica** de circuito de dos qubits, no entrenamiento de un modelo de IA a gran escala.
- Construido para Android API 26+ con Storage Access Framework, sin permiso de acceso total a archivos ni permiso de Internet.

## Seguridad y privacidad

1. `ACTION_OPEN_DOCUMENT` proporciona acceso de solo lectura al origen.
2. `ACTION_CREATE_DOCUMENT` pide al usuario un archivo de salida diferente.
3. Todos los bloques pasan por verificaciones de tamaño y CRC32, el contenedor exige SHA-256 global.
4. La extracción de archivos EXF no ejecuta su contenido y nunca interpreta comandos internos.
5. Si falla una salida parcial, Android intenta eliminar el documento de destino recién creado.

### Licencias / datos

Código prototipo original basado en ideas de Mundo X; utiliza solo las APIs estándar Java/Android. El archivo ISO de pruebas es sintético y pequeño, no contiene Windows ni material propietario.
