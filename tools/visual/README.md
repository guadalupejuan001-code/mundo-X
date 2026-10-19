# Comprobación del puente visual

`check_scene_bridge.py` compara el `VisualScene` de C++ con el `ObjectSceneModel`
real de Android. No contiene una segunda implementación de la mezcla de bits
ni de las transformaciones.

```sh
python3 tools/visual/check_scene_bridge.py --output /tmp/visual-bridge.json
```

Requiere Python 3.10 o posterior, un compilador C++17 (`g++` por defecto) y Java.
Si no está disponible `javac`, se puede usar un compilador Eclipse existente:

```sh
python3 tools/visual/check_scene_bridge.py --ecj /ruta/ecj.jar \
  --output /tmp/visual-bridge.json
```

Los programas de prueba importan el formato `MUNDOX_OBJECTS_V1`, ejecutan cambios
y exportan otra vez. Se verifica el texto canónico, los resultados esperados de
cambios y rechazos y **cada bit de los cuatro floats de cada objeto**. Un caso
recorre los 1.048.576 IDs; los demás incluyen semillas negativas y extremas,
cuentas alrededor de 65.536, cuadrículas incompletas, objetos ocultos/elevados,
restauraciones, el límite de 4.096 cambios y revisiones agotadas.

Las sondas conservan cuatro floats por vez. El comparador lee bloques de 64 KiB
de archivos temporales, y calcula SHA-256 además de comparar directamente los
bytes. Los archivos temporales se eliminan al terminar. El informe registra
los hashes del código comprobado y de las transformaciones.

Esta comprobación demuestra compatibilidad de representación y de cambios.
No mide velocidad, RAM total del proceso, imágenes de GPU ni un dispositivo
Android físico. Las herramientas y compiladores son dependencias de pruebas;
no añaden dependencias al motor.
