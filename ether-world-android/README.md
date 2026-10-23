# ETHER WORLD · Mundo X

Prototipo funcional de universo virtual / cartografía espacial construido
sobre los módulos **originales** del repositorio Mundo X. Esta aplicación
Android independiente no cambia COSMIC, WorldLink, Solar, los juegos ni main.

## Idea central

El espacio virtual es una **regla generadora y un conjunto finito de
modificaciones**, no una lista de un millón de bloques residentes en RAM.
La observación decide qué parte del mundo se reconstruye y representa.

La fuente de verdad por sesión está agrupada en EtherWorld:

| Módulo original | Función real aquí |
| --- | --- |
| VisualScene | Semilla y transformación determinista de 1.048.576 posiciones (1024 × 1024) |
| QuantumRuntime / InverseMemory | Conserva cambios por dirección con 256 páginas copy-on-write de Ether Memory PR #54 |
| InfiniteCore + Ether | Par Bell SIMULADO de q0/q1 y un bit de campo q2, cuyos cambios alteran la escena |
| RulePhysics | Cuerpos activos con velocidad, contactos y evolución por pasos |
| Ether Render X | Planifica regiones conservadoras, limita candidatos y calcula el objeto esférico visible por píxel |
| Capa de representación espacial | Composición del terreno y un mapa local a partir de reglas, no array global de celdas |

La cámara y el mapa son observadores. Cambiar la cámara no reinicia la sesión
Ether ni borra el mundo.

## Interacción con el mundo

- **ESCANEAR**: revela la región cercana a la posición del observador.
- **Mover N/S/E/O**: se desplaza a una celda lógica; las zonas nuevas
  continúan ocultas hasta escanearlas.
- **Tocar mapa**: seleccionar una posición. **Ir a selección** funciona para
  un destino a no más de seis pasos de distancia.
- **Faro / cristal / vacío / barrera**: modifica solo la celda elegida,
  si ya fue observada. Ether guarda únicamente la excepción, sin recorrer
  ni clonar el millón de posiciones.
- **Restaurar regla original**: borra el cambio y reconstruye la celda
  según su semilla inicial.
- **Impulso**: introduce un frente de onda virtual que se propaga
  radialmente, con atenuación. Cambia un qubit simulado del campo Ether
  q2 mediante una puerta X real del software original.
- **Evolucionar 16**: hace avanzar las sondas materiales usando RulePhysics,
  y los impulsos con el tiempo. Las sondas pueden revelar pequeñas zonas.
- **Seguir sonda**: desplaza el observador hasta la primera sonda.
- **Girar / zoom / arrastrar escena**: cambia únicamente la observación.
- **Verificar exactitud**: compara el culling optimizado de Ether Render X
  con la referencia por fuerza bruta en la misma escena.
- **Optimizar regiones**: ejecuta el entrenamiento determinista de tamaño de
  tiles del motor Ether Renderer, conservando la imagen de referencia.
- **Guardar / restaurar**: serializa todos los cambios Ether, estado
  cuántico simulado, sondas, regiones observadas, posición, tiempo y pulsos.
  También se guarda automáticamente tras cambios de mundo.
- **Crear otro universo**: cambia la semilla y empieza una sesión
  independiente, con confirmación.

## Restricciones y eficiencia

- 1.048.576 **celdas lógicas**. No son qubits físicos ni partículas
  reales: cada celda proviene de la regla VisualScene.
- Solo se reconstruye una ventana local de 65 × 65 celdas (4225 consultas
  lógicas) para la vista; el mapa se presenta a 45 × 45 celdas.
- La etapa de objetos 3D conserva un límite de 64 cuerpos explícitos.
- Se permiten 4.096 excepciones de memoria en WorldLink Ether, 256 regiones
  de escaneo, 12 perturbaciones simultáneas y 10 cuerpos físicos activos.
- Mundo X Ether Renderer conserva su propia prueba de exactitud. La capa de
  mapa/procedural es distinta de su algoritmo de esferas; no declaramos
  exactitud geométrica de mallas o paisajes externos.
- Es un mundo **simulado**. No captura tu habitación ni la geometría de
  la lattice universal; un mapa real requeriría datos de cámara/profundidad
  y calibración que esta app no tiene.
- El campo de ondas es una regla analítica radial; no representa por sí solo
  la solución completa de electromagnetismo, gravedad o la ecuación
  de Schrödinger de un sistema físico.
- La memoria y el rendimiento de JNI, buffers de imagen y GPU/CPU del
  dispositivo deben medirse por separado. La semilla compacta no proporciona
  potencia infinita.

## Compilación

GitHub Actions: .github/workflows/ether-world-android.yml

1. Compila pruebas C++ que ejecutan los **mismos encabezados originales**
   y verifica la evolución, guardado/carga, par Ether, imagen y oráculo.
2. Compila el APK Android ARM64 con Gradle 8.9 y NDK 27.2.
3. Verifica el paquete, firma APK, biblioteca JNI y alineación 16 KiB.

APK: Mundo-X-Ether-World.apk. Se instala directamente en Samsung Android.
El mundo se guarda como archivo privado ether_world_v1.world.

Repositorio fuente: https://github.com/guadalupejuan001-code/mundo-X
