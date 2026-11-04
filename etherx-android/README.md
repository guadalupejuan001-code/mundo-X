# ETHER X Reverse Lab · Android nativo v0.3.0

Laboratorio local de ingeniería inversa y compactación para Android 8+. Interfaz oscura con identidad visual propia (cian, violeta, azul, coral), icono vectorial, tarjetas interactivas, mapa de entropía y microscopio hexadecimal. No requiere Python, Termux, navegador, cuenta ni Internet.

## Qué es una «cirugía binaria» en esta versión

1. **Elegir archivo/ISO:** Android entrega una URI de solo lectura. ETHER X nunca ejecuta el archivo.
2. **Diseccionar:** clasifica por firmas y decodifica los campos conocidos por sus offsets. Las estructuras aparecen como regiones con título, longitud, explicación y estado verificado/indicio.
3. **Microscopio HEX:** muestra cada byte + su dirección real + interpretación ASCII. Permite saltar a offsets arbitrarios. Fuera de los primeros 3 MiB intenta solicitar solo un rango de 384 bytes al proveedor SAF, sin copiar la imagen entera.
4. **Mapa:** entropía de 64 intervalos, cobertura, porcentaje de ceros y byte dominante. Las estadísticas se calculan sobre una muestra, **no** sobre todo el archivo salvo que quepa íntegramente.
5. **Evidencia:** notas técnicas, límites, indicadores internos y cadenas ASCII con offsets.
6. **Informar:** comparte un informe textual con las regiones interpretadas y sus límites.
7. **Compactar / reconstruir:** conserva los algoritmos EXF v1 de la versión anterior, CRC32 por bloque y SHA-256 global. Bloques de 1 MiB (regla uniforme, excepciones dispersas, referencias, zlib, literal). El EXF es un formato de almacenamiento, no una ISO arrancable: hay que restaurar la ISO antes de usarla como imagen.

## Formatos con disección específica

- **ISO9660 y Joliet:** sector 16, descriptores CD001, volumen, LBAs, registros de directorio, archivos y carpetas anidadas dentro de límites. Puede solicitar por offset directorios lejos del inicio; no es un lector completo de UDF.
- **ZIP y APK:** cabeceras locales, nombres, método, banderas, tamaños. Busca el directorio central en los últimos 128 KiB del archivo, incluso en ZIP grandes (ZIP64 y algunas variantes requieren ampliación).
- **PE / EXE Windows:** firma MZ, e_lfanew, cabecera COFF, RVA de entrada, secciones y direcciones de archivo.
- **ELF:** bits, endianness, arquitectura, punto de entrada y tabla de segmentos.
- **PNG:** secuencia de chunks, dimensiones, tipo de color y CRC32 de los chunks completos disponibles.
- **JPEG:** marcadores, tablas, metadatos y dimensiones si aparecen.
- **PDF:** encabezado, indicadores de objetos, búsqueda de EOF y startxref en la cola.
- **MP4 / ISO BMFF:** cajas ftyp, moov, mdat y contenedores anidados presentes en la muestra.
- **SQLite:** cabecera, tamaño de páginas, codificación y cabecera inicial B-tree.
- **RIFF / WAV / WebP:** estructura de chunks, parámetros básicos de audio WAV.
- **WIM / GZIP / 7z / XZ / Zstandard / EXF:** detección y campos iniciales seleccionados. No se promete descompresión o descifrado de estos formatos externos.
- **Formatos desconocidos:** inspección de bytes, búsqueda de firmas embebidas, cadenas y patrones de distribución sin inventar semántica.

## Alcance real y límites

- La lectura principal está limitada a **3 MiB iniciales**; una lectura aleatoria adicional puede obtener directorios ISO, índice ZIP y cola PDF, más ventanas HEX en otras direcciones. Si el proveedor no soporta seek, la alternativa secuencial está limitada a 128 MiB para evitar lecturas prohibitivas. No se usa RAM proporcional al tamaño de la ISO.
- Sin decodificador específico no es posible prometer explicación exacta de cualquier formato desconocido, cifrado o propietario. Tampoco hay decompilación x86, extracción de WIM/UDF ni descompresión integral de ZIP en esta versión.
- En análisis de ZIP, registros fuera de las muestras se señalan como no verificados; el tamaño de campos y firmas proviene de bytes reales. No se ejecuta contenido.
- La restitución EXF requiere almacenamiento para un temporal íntegro del archivo de salida; se valida SHA-256 antes de escribir el destino. El código no soporta bloques LZMA del EXF Python `maximo` heredado.
- **Q-Lab** conserva el experimento clásico de circuitos de **2 qubits simulados**, semilla fija y comparación con clasificador clásico. No se entrena con archivos del usuario, no acelera el compresor ni utiliza hardware cuántico.
- La representación por regla + excepciones adopta el principio de Inverse Ether Memory de Mundo X, pero no enlaza directamente el núcleo cuántico C++ `InfiniteCore`.

## Pruebas

En JDK 17 o superior:

```sh
bash tests/test-host.sh
```

Pruebas EXF preexistentes: ida/vuelta, corrupción, truncamiento, referencias de bloques y experimento Q-Lab. Pruebas nuevas: **1444 verificaciones** incluyendo formatos sintéticos PE, ELF, ISO, ZIP, PDF, PNG con CRC válido/corrupto, MP4, SQLite, RIFF, lectura ISO lejana, índice ZIP en cola, cola PDF y 700 entradas aleatorias con límites de memoria y regiones. Son pruebas de host; la usabilidad en un dispositivo Android debe probarse aparte.

## Compilar y descargar APK

```sh
gradle -p etherx-android --no-daemon :app:assembleDebug
```

Desde Mundo X en GitHub, el workflow `.github/workflows/etherx-android.yml` instala SDK 35, ejecuta las pruebas y compila el APK, verifica su firma y publica un artefacto descargable. No es necesario que el teléfono tenga Java SDK o Termux instalado. El APK `debug` es para pruebas, no para distribución pública.

## Privacidad

No se solicita permiso de Internet ni acceso general a archivos. Los documentos de entrada permanecen sin cambios. La app utiliza `ACTION_OPEN_DOCUMENT` y `ACTION_CREATE_DOCUMENT` (Storage Access Framework), no sube datos y comparte un informe solo cuando se toca «Exportar informe».