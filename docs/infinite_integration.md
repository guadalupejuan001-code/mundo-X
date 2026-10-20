# InfiniteCore en el emulador y el motor gráfico

La integración usa el `InfiniteCore` propio, sus componentes `CompactCore` y
el observador gráfico existente. Una sesión Infinite admite un fondo `zero` o
`bell`, direcciones decimales y operaciones finitas. El perfil de 1.000 qubits
y los documentos `.cosmic` v1 siguen disponibles.

## Una sesión viva por propietario

`native/include/mundo_x/infinite_session.hpp` centraliza sincronización,
transacciones, consultas, snapshots y épocas de invalidación. La gramática
`.xi` está en `infinite_program.hpp` y es compartida por CLI, emulador y puente.

| Consumidor | Conexión con el estado |
|---|---|
| Emulador x86/x64 | Cada `PcMachine` posee una `shared_ptr<InfiniteSession>`. El dispositivo de puertos y el panel operan esa sesión. |
| Viewport del PC | Su handle gráfico conserva otra referencia a esa misma sesión; no copia ni reinicia el núcleo. |
| Aplicaciones gráficas | `NativeCore` selecciona explícitamente registro finito o Infinite y el renderizador toma prestado ese propietario. |
| Escritorio | `infinite_runtime.InfiniteCore` posee un handle `mxi_*`; `QuantumScene.from_infinite_core` toma prestado ese objeto. |

Cada petición interactiva ejecuta su programa completo en un candidato. Un
error, una aserción fallida o un presupuesto excedido conserva el estado,
la revisión, la época y el RNG anteriores. La respuesta se prepara antes de
publicar el candidato. Las consultas y los snapshots son de lectura.

`query_batch` devuelve coeficientes, revisión y época de la misma lectura bajo
mutex. El renderizador no mezcla coeficientes antiguos con una época nueva si
el invitado o el panel escriben durante la observación.

## Dispositivo del PC

El invitado utiliza las instrucciones `IN` y `OUT` ya implementadas:

| Puerto | `IN AL, DX` | `OUT DX, AL` |
|---|---|---|
| `0x5850` | Lee el siguiente byte de respuesta; devuelve cero si no queda respuesta. | Añade un byte ASCII al programa pendiente. |
| `0x5851` | Lee bits de estado. | `1` vacía el canal; `2` ejecuta la petición. |

Bits de estado: `0x80` dispositivo presente, `0x01` respuesta disponible,
`0x02` petición rechazada y `0x04` petición demasiado larga. El transporte usa
operaciones de un byte y admite hasta 65.536 bytes de entrada. Una entrada
truncada se rechaza íntegra y exige vaciar el canal. Vaciar el canal no reinicia
el núcleo. El panel tiene su propia respuesta y no consume el FIFO del invitado.

Los errores del dispositivo no se convierten en un fallo de CPU. El arranque
cuántico de prueba envía puertas y aserciones reales y toma una rama de éxito o
fallo según lo que responda el núcleo. Hay variantes para modo real y modo largo.
Las instrucciones x86 siguen siendo ejecutadas por el intérprete propio.

## Efecto gráfico

El programa observado es una lista finita y estable de productos Pauli. Por
ejemplo, para una pareja Bell se observan XX, YY, ZZ y operadores locales. Una Z
sobre uno de sus miembros invierte XX y YY. El shader existente toma esos
coeficientes y aplica su ley de altura:

`altura = altura_procedimental × animación × (1 + acoplamiento × coeficiente)`

El núcleo no enumera el dominio para dibujar. El programa gráfico conserva sus
roles después de una puerta; reconstruirlo a partir de los generadores nuevos
podría ocultar el cambio. Cambiar la dirección observada invalida el programa
gráfico sin modificar el estado cuántico.

El PC conserva el vídeo VBE/GLES producido por la memoria del invitado y su
terminal. El visor cuántico adicional muestra la escena del motor conectada al
núcleo que ese mismo programa puede operar. Las resoluciones de vídeo del PC,
incluidos sus perfiles hasta 8K, siguen perteneciendo al dispositivo de vídeo;
los qubits activos se contabilizan por separado.

## Guardado exacto

`.cosmic` v2 contiene un snapshot `MXI1:` sin marco, o `MXI2:` con reglas Ether, del estado actual. Conserva mediciones,
componentes, signos, direcciones y RNG; v1 conserva su historial finito. El
formato exacto de intercambio está en
[PROJECT_FORMAT.md](../graphics-desktop/PROJECT_FORMAT.md).

El snapshot impone un máximo de 16 MiB. Importar verifica los datos y el estado
matemático antes de aceptarlos. Los presupuestos proceden de la sesión receptora.
Restaurar avanza la época viva, incluso al importar la misma revisión histórica,
para que la imagen no conserve coeficientes de otro estado.

## Uso desde escritorio

Crear un proyecto Bell en direcciones grandes y exportar su estado:

```bash
python graphics-desktop/demo.py --infinite bell --address 100000000000000000000 \
  --gate Z:100000000000000000000 --export-project infinito.cosmic --no-render
```

Abrirlo y observar su escena con el renderizador existente:

```bash
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 \
  python graphics-desktop/demo.py --project infinito.cosmic --view street \
  --output infinito.png
```

Desde código:

```python
from engine import build_native, QuantumScene
from infinite_runtime import InfiniteCore, default_observables

build_native()
a = "1" + "0" * 100
with InfiniteCore("bell") as core:
    scene = QuantumScene.from_infinite_core(core, observables=default_observables(a))
    core.apply([("Z", a)])
    print(scene.compile().coefficients)
    saved = core.snapshot()
    core.execute("measure " + a)
    core.restore(saved)
    scene.close()
```

## Límites operativos

El fondo define un dominio sin último índice; cada operación, dirección de
entrada, componente y escena usa recursos finitos. Los presupuestos nativos
predeterminados son 4.096 qubits activos, 1.024 por componente, 64 MiB de payload
de tableau y 4.096 dígitos por dirección. El emulador PC reduce el presupuesto
de tableau a 16 MiB. Los bytes de tableau no son la RAM
total de la aplicación. No hay puertas T ni simulación universal arbitraria en
este backend Clifford. Las capacidades de arranque de sistemas operativos del
emulador continúan dependiendo de su implementación x86 y sus dispositivos.

Las pruebas de integración verifican el estado compartido, la ruta real del
invitado, el cambio de píxeles, el control con acoplamiento cero y el guardado
después de medir. Los informes separan las comprobaciones nativas, GLES de
escritorio y Android de cualquier medición pendiente en un teléfono físico.
