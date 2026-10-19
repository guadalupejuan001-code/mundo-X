# Formato de proyecto `.cosmic`, versiones 1 y 2

Un archivo es un objeto JSON UTF-8, sin referencias externas ni código que se
ejecute al abrirlo. Su tamaño máximo es 16 MiB (16.777.216 bytes). Se rechazan
claves duplicadas, claves desconocidas, campos faltantes, números no finitos y
versiones desconocidas. El codec Python se encuentra en `project.py` y usa la
biblioteca estándar para leer, validar y escribir el documento.

La versión 1 conserva el registro finito y su historial. La versión 2 incorpora
`InfiniteCore` con un snapshot de su estado actual. La sección siguiente describe
v1; las diferencias de v2 están al final. Una versión y un backend incompatibles
se rechazan.

## Raíz y núcleo

| Campo | Valor o límite |
|---|---|
| `format` | Cadena exacta `cosmic-x-engine-project`. |
| `version` | Entero 1; un booleano no se acepta como entero. |
| `name` | Nombre de 1..128 puntos de código Unicode, sin caracteres de control U+0000..U+001F ni sustitutos Unicode aislados U+D800..U+DFFF. |
| `core` | Objeto con exactamente `backend`, `initialState`, `qubits`, `revision`, `gates`. |
| `graphics` | Objeto con los campos gráficos de la siguiente sección. |
| `core.backend` | Cadena exacta `compact-clifford`. |
| `core.initialState` | Cadena exacta `zero`: estado inicial `|0…0⟩`. |
| `core.qubits` | Entero 1..1000; los proyectos nuevos comienzan en 1000. |
| `core.revision` | Entero 0..9.007.199.254.740.991. Serial guardado de cambios por lote; no equivale al número de puertas. |
| `core.gates` | Lista ordenada de hasta 100.000 puertas; puede estar vacía. |

Puertas unarias: `["H", q]`, `["S", q]`, `["X", q]`, `["Y", q]`, `["Z", q]`.
Puertas binarias: `["CX", control, objetivo]`, `["CZ", a, b]`.
Todos los índices son enteros en `0..qubits-1`; los operandos de una puerta
binaria son distintos. La lista completa se valida antes de aplicarse. No se
aceptan aliases ni operaciones desconocidas dentro del archivo.

El historial se ejecuta en orden desde el estado inicial cero. La sesión nativa
produce este historial; leerlo no altera el estado. La revisión persistente se
restaura y la sesión activa obtiene un nuevo `epoch` para invalidar las cachés.

## Programa gráfico

El objeto `graphics` tiene exactamente estos campos:

| Campo | Valor o límite |
|---|---|
| `program` | Cadena exacta `opaque-grid-v1`. |
| `objects` | Entero 1..1.048.576. |
| `seed` | Entero sin signo 0..4.294.967.295; sus cuatro bytes alimentan la misma regla de Mundo X. |
| `coupling` | Número finito entre −0,25 y 0,25, ambos inclusive. |
| `observables` | Entre 1 y 4.096 cadenas Pauli originales. |
| `edits` | Hasta 100.000 pares `[id, estado]`, ordenados por ID ascendente y sin duplicados. |
| `camera` | Una de las dos cámaras descritas abajo. |
| `time` | Número finito entre −1.000.000 y 1.000.000. |
| `selectedId` | Entero −1 para ninguna selección, o un ID de la población activa. |
| `resolutionLimit` | Entero 64..4.096; límite del lado más largo para el observador Android. La CLI admite dimensiones explícitas de salida. |

Cada observable contiene exactamente `qubits` caracteres del alfabeto `IXYZ`,
con signo `+` o `-` opcional. El primer carácter del cuerpo corresponde al qubit
0. Se conservan los operadores originales y sus signos; no se reemplazan por
los generadores del estado resultante al importar.

Cada ID editado está entre 0 y 1.048.575, aunque la población actual sea menor.
El estado está entre 1 y 7: 1..6 añaden altura según la regla existente y 7
oculta. La restauración de una caja elimina su par de ediciones; no se guarda
un estado 0 redundante. El observador usa las ediciones activas y conserva las
que quedan temporalmente fuera de la población.

## Cámaras

La cámara orbital tiene exactamente `mode`, `overview`, `yaw`, `pitch`, `zoom`:

```json
{"mode":"orbit","overview":true,"yaw":0.67,"pitch":0.65,"zoom":1.0}
```

`overview` es un booleano. `yaw` está entre −1.000.000 y 1.000.000 radianes,
`pitch` entre 0,04 y 1,52 radianes, y `zoom` entre 0,12 y 6. Todos son finitos.
El campo de visión orbital es de 48 grados. Con `side = ceil(sqrt(objects))`,
`extent = side × 2,2` y el aspecto de la imagen, se usa la ley del observador
Android existente:

- Vista global: distancia `extent × max(1,65; 1,4/aspecto) × zoom`, objetivo
  `[0,0,0]`, plano cercano `max(0,05; extent × 0,01)` y lejano
  `max(10000; distancia + extent × 4)`.
- Vista de calle: distancia `25 × zoom`, objetivo `[0,1,0]`, plano cercano 0,1
  y lejano 5000.

La cámara explícita tiene exactamente `mode`, `eye`, `target`, `near`, `far`,
`fov`:

```json
{"mode":"lookAt","eye":[14,8,18],"target":[0,1,0],"near":0.1,"far":5000,"fov":48}
```

Los vectores tienen tres coordenadas finitas entre −1.000.000 y 1.000.000. La
distancia al cuadrado entre posición y objetivo es al menos 0,00000001. Se
requiere `0,001 ≤ near < far ≤ 1.000.000` y campo de visión `1 ≤ fov ≤ 170`
grados. La referencia vertical cambia a Z si la dirección es casi vertical,
como en los observadores originales.

La separación mínima de cámara y el orden `near < far` también se comprueban
después de convertir sus valores a coma flotante de 32 bits. Un proyecto no
puede describir dos posiciones o planos que colapsen en uno al cargarse en el
observador Android/GLES.

## Lectura y escritura

La importación tiene dos pasos: validación completa del JSON y construcción de
un núcleo/escena candidatos. La aplicación conserva su sesión activa hasta que
ambos pasos terminen correctamente. Una entrada inválida no aplica parcialmente
su historial al proyecto activo.

La escritura valida y codifica antes de tocar el destino. El codec de
escritorio escribe en un temporal del mismo directorio, sincroniza sus bytes y
sustituye el archivo de forma atómica. La aplicación Android exporta un
documento mediante su selector de archivos y valida los mismos límites.

Los ejemplos completos son `examples/bell-observer.cosmic` y
`examples/ghz-city.cosmic`. El segundo cubre una semilla mayor que 2³¹−1, para
evitar que el transporte entre Java y Python cambie sus 32 bits.

## Versión 2: InfiniteCore

La raíz conserva exactamente `format`, `version`, `name`, `core`, `graphics`.
`version` es el entero `2`. `core` contiene exactamente:

```json
{
  "backend": "infinite-clifford",
  "snapshot": "MXI1:<estado codificado por el núcleo nativo>"
}
```

El texto entre ángulos ilustra el lugar del contenido; un snapshot válido lo
produce `mxi_snapshot` / `InfiniteCore.snapshot()`. Su contenido es ASCII con
prefijo `MXI1:` y datos hexadecimales. Conserva fondo, revisión, direcciones
canónicas, componentes, bits X/Z, signos de los generadores y el estado completo
del generador aleatorio MT19937-64. No contiene una lista histórica de puertas.
No se duplican esos campos como números JSON.

`graphics` conserva sus mismos campos y la ley `opaque-grid-v1`. La diferencia
es que cada cadena en `observables` expresa un producto Pauli disperso:

```json
["X:1000000000 X:1000000001", "- Y:1000000000 Y:1000000001", "Z:0"]
```

El signo `+` o `-` es un token opcional separado por espacios. La cadena vacía
representa la identidad positiva. Cada término usa `I`, `X`, `Y` o `Z` y una
dirección de 1..4.096 dígitos ASCII. No se repite una dirección después de
normalizar los ceros iniciales. Hay hasta 4.096 términos por observable y hasta
4.096 observables por programa. Se conserva el límite total de archivo de
16 MiB y los límites de geometría/cámara/ediciones de v1.

El códec comprueba la estructura del documento; la importación nativa comprueba
además dimensiones, presupuestos del consumidor, direcciones únicas, parejas
Bell completas, bits válidos, conmutación y rango completo del tableau y estado
RNG válido. Construye un candidato antes de sustituir el estado activo. El
archivo no puede ampliar los presupuestos del consumidor. Un snapshot que no
quepa en el transporte se rechaza al guardar; no reduce ni aproxima el estado.

Restaurar conserva la revisión histórica y avanza la época del handle vivo.
El observador invalida su caché aunque el estado importado tenga la misma
revisión que el anterior. Los operadores gráficos guardados mantienen su papel
después de cada puerta o medición.
