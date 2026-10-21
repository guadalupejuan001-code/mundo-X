# COSMIC X Engine

## Núcleo cuántico y Motor gráfico cuántico integrados

Este directorio incorpora al repositorio el Motor gráfico de Mundo X que antes
se entregaba como paquete de escritorio. El núcleo utilizado por el observador
y por Android se compila ahora desde la misma implementación del repositorio:
`native/src/graphics_core_bridge.cpp` y los encabezados de `native/include/mundo_x`.
COSMIC X Engine es el título del conjunto; se conservan el núcleo, las reglas
visuales, las puertas y los identificadores de Mundo X.

La sesión tiene un propietario del estado. El motor gráfico recibe ese mismo
núcleo, consulta sus observables y produce una imagen desde una cámara. Las
operaciones sobre el núcleo cambian su historial; observar, mover la cámara,
seleccionar y editar una caja no añaden puertas ni miden proyectivamente el
estado. El formato `.cosmic` permite guardar y transportar esta relación.

## Ejecutar

Requisitos de esta implementación de escritorio: Python 3, compilador C++17,
NumPy, Pillow y una implementación EGL/OpenGL ES 3 disponible en el sistema.
El adaptador de escritorio usa una biblioteca compartida Linux `.so`; Android
usa su propio proceso de compilación. Los comandos se ejecutan desde la raíz
del repositorio:

```sh
python -m pip install -r graphics-desktop/requirements.txt
python graphics-desktop/demo.py --project graphics-desktop/examples/bell-observer.cosmic --output bell.png
```

El puente nativo se compila automáticamente cuando faltan sus binarios o cambian
sus fuentes. Los binarios generados no forman parte del código versionado.

Para abrir el segundo mundo se utiliza el mismo motor:

```sh
python graphics-desktop/demo.py --project graphics-desktop/examples/ghz-city.cosmic --output ciudad.png
```

Para aplicar una operación, editar una caja y guardar un proyecto nuevo:

```sh
python graphics-desktop/demo.py --project graphics-desktop/examples/bell-observer.cosmic --gate Z:0 --edit 62:7 --export-project bell-editado.cosmic --output bell-editado.png
```

`--gate` puede repetirse. Las puertas aceptadas son H, S, X, Y, Z, CX y CZ.
`CNOT` es un alias de CX en la línea de comandos; el formato guarda `CX`.
Las ediciones 1..6 elevan la caja, 7 la oculta y 0 restaura su regla original.
Las puertas proporcionadas en una invocación se aplican como un lote validado.

```sh
python graphics-desktop/demo.py --objects 1048576 --qubits 20 --seed 2026 --view overview --export-project mi-mundo.cosmic --no-render
python graphics-desktop/demo.py --project mi-mundo.cosmic --view street --width 640 --height 360 --output mi-mundo.png
```

`--no-render` permite importar, editar y exportar sin crear un contexto EGL.
El núcleo nativo sigue siendo necesario para ejecutar y conservar las puertas.
`--project` carga el estado y los operadores originales guardados; la apertura
no vuelve a preparar el grafo predeterminado. La CLI sólo aplica puertas cuando
se le pasan mediante `--gate`.

## Usarlo desde otra aplicación Python

La aplicación agrega `graphics-desktop` a su ruta de módulos y reutiliza sus
clases. El dueño del núcleo conserva su vida útil hasta cerrar el observador:

```python
from core_runtime import Core
from engine import build_native, QuantumScene, QuantumGraphicsEngine
from project import ProjectSession

build_native()
core = Core(2)
core.apply([("H", 0), ("CX", 0, 1)])
scene = QuantumScene.from_core(core, count=1024, observables=["XX", "-YY", "ZZ"])
project = ProjectSession.from_scene(scene, name="Mi mundo Bell")
engine = QuantumGraphicsEngine(scene, 640, 360)
try:
    core.apply([("Z", 0)])
    image, timing = engine.observe(project.camera_for(640, 360))
    project.save("mi-mundo-bell.cosmic")
finally:
    engine.close()
    project.close()  # No cierra un núcleo prestado.
    scene.close()
    core.close()
```

`ProjectSession.open(path)` crea un candidato nuevo después de validar el
documento completo. Una aplicación puede sustituir su proyecto activo cuando
esa llamada termine correctamente. Si falla, el proyecto anterior sigue bajo
su propietario y no ha recibido ninguna mutación.

La revisión guardada identifica los lotes de puertas. El `epoch` del núcleo
sirve para invalidar el observador incluso cuando se restaura otro estado con
el mismo número de revisión. El historial exportado procede de la sesión
nativa que ejecutó las puertas, incluidas operaciones hechas por otro usuario
del mismo objeto `Core`.

Las aplicaciones deben serializar sus modificaciones del mundo y sus llamadas
al observador en un solo hilo de render. La ABI sincroniza operaciones sobre
el núcleo; su propietario también debe evitar cerrar un núcleo mientras otra
llamada lo está usando.

## Dos proyectos incluidos

| Proyecto | Núcleo y programa | Mundo y cámara |
|---|---|---|
| `examples/bell-observer.cosmic` | Bell de 2 qubits; 7 operadores originales, incluidos signos explícitos. | 1.024 cajas, semilla 2026, tres ediciones, cámara explícita. |
| `examples/ghz-city.cosmic` | GHZ de 3 qubits seguido de S; 11 operadores originales. | 1.000.000 de cajas, semilla 4000000001, acoplamiento −0,125, cámara orbital. |

Se pueden modificar o reemplazar sus datos sin cambiar código del motor.
Los operadores que no se utilizan al reducir la población siguen guardados.
Las ediciones fuera de la población temporalmente activa conservan sus IDs y
reaparecen al ampliar de nuevo el mundo.

## Verificación

```sh
python graphics-desktop/validate_projects.py --output /tmp/cosmic-project-validation.json
python graphics-desktop/validate_contract.py
python graphics-desktop/validate.py
```

La primera comprobación carga los dos proyectos, exporta e importa el estado y
verifica el historial, los observables, la semilla, las ediciones y la imagen en
el mismo controlador. También comprueba el núcleo prestado, la operación Y,
restauración con igual revisión, caché de cámara, control con acoplamiento cero
y rechazo de entradas inválidas sin dañar la sesión o un archivo existente.

Las verificaciones originales conservan las comparaciones con el núcleo denso,
los rayos geométricos independientes, la selección y los extremos del rango de
IDs. Cada informe identifica el controlador gráfico. Los tiempos de un EGL de
software describen esa ejecución, y deben medirse por separado en el teléfono.

## Alcance actual

El perfil portátil v1 usa el `CompactCore` Clifford, observables Pauli y cajas
opacas en una cuadrícula finita de hasta 1.048.576 objetos. Sus reglas calculan
la geometría a partir del ID, semilla, tiempo, ediciones y observables. La
cantidad total de objetos y la cantidad visible desde una cámara son medidas
distintas.

El perfil `.cosmic` documenta límites de intercambio para controlar archivos y
recursos: 1.000 qubits, 100.000 puertas, 4.096 observables, 100.000 ediciones y
16 MiB por archivo. Estos límites no describen toda la capacidad matemática
del repositorio. Las rutas Reduced/Factorized conservan sus contratos propios;
este formato v1 rechaza operaciones o representaciones que no puede restaurar
fielmente en el observador integrado.

La portabilidad exige conservar el estado lógico, los operadores y las reglas.
El redondeo de matrices y shaders puede variar entre Android y escritorio; no
se exige identidad de cada píxel entre GPUs diferentes. La cámara orbital usa
la misma ley de distancia y encuadre de Android, adaptada al aspecto de salida.
El visor de escritorio de esta entrega produce imágenes mediante CLI; no
incluye una ventana interactiva de edición.

El contrato detallado está en [PROJECT_FORMAT.md](PROJECT_FORMAT.md), y el origen
de los encabezados originales se conserva en `native/PROVENANCE.json`.

## Proyectos Infinite

`--infinite zero` o `--infinite bell` crea un proyecto con direcciones decimales
y estado actual guardado en `.cosmic` v2. `--address` fija la región observada;
`--partner` permite elegir el segundo operando. `--gate` conserva los operandos
como texto y `--command` ejecuta una petición `.xi` atómica, incluidas medidas.
La imagen sigue usando el mismo motor, sus objetos, selección, cámara y shaders.

```bash
python graphics-desktop/demo.py --infinite bell --address 100000000000000000000 \
  --gate Z:100000000000000000000 --export-project infinito.cosmic --no-render
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 \
  python graphics-desktop/validate_infinite.py
```

El modelo y la conexión con el invitado PC se explican en
[infinite_integration.md](../docs/infinite_integration.md).

## Reflejos sobre el mismo mundo

El observador admite suelo reflectante y dos espejos enfrentados. Mantiene los
IDs de los objetos vistos por reflexión y compone los recorridos periódicos que
cumplen las condiciones geométricas comprobadas. Para una vista inicial:

```sh
python graphics-desktop/demo.py --infinite zero --objects 64 \
  --view mirror --mirrors pair --cosmic-style --output espejos.png
```

La configuración óptica se pasa al observador, por separado del archivo
`.cosmic`. Arquitectura, límites, pruebas, RAM y procedimiento Android:
[Espejos sobre el mismo mundo de reglas](../docs/ether-mirrors.md).

## Excepciones compactas en GPU

Las ediciones se consultan mediante páginas exactas de 256 IDs. Se mantiene el
mapa disperso del proyecto y se deriva una textura R32UI; los bloques sin cambios
no tienen página de estados. Las consultas requieren una o dos lecturas, sin
búsqueda binaria. Python empaqueta las ediciones grandes en lotes de 4.096 para
acotar los temporales. [Medidas, compatibilidad y reproducción](../docs/ether-render-optimization.md).
