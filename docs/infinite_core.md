# InfiniteCore 0.1: un dominio infinito definido por reglas

Esta línea de COSMIC X Engine implementa un espacio de qubits con direcciones
en los enteros no negativos, un fondo definido por una regla y modificaciones
finitas. La regla no necesita declarar un último qubit. El trabajo concreto se
realiza sobre componentes finitos con el `CompactCore` propio de Mundo X.

Es un desarrollo separado en `codex/infinite-core-rules-20261019`, iniciado desde
`main` `e06ba2a19d2f7287f7f9d8f4f806f521fb7f8fcf` y actualizado a la nueva base
`7fd001c524ca234d98f07ba46fb074f9f4572a97`, que integró el perfil de 1.000 qubits
durante este trabajo. Tiene su ejecutable `mundo_x_infinite` y programas `.xi`.
La sesión, las aplicaciones y la rama de pruebas `codex/core-1000-qubits-20261019`
conservan su funcionamiento independiente de este módulo.

## Qué significa infinito aquí

El fondo establece el estado de cualquier región finita que se consulte:

| Regla | Definición | Ejemplo de consulta sin activar componentes |
|---|---|---|
| `zero` | Cada dirección empieza en $\lvert 0\rangle$ | `probz 1000000000` devuelve `0` |
| `bell` | Cada pareja `(2k, 2k+1)` sigue $(\lvert 00\rangle + \lvert 11\rangle)/\sqrt{2}$ | `expect Y:1000000000 Y:1000000001` devuelve `-1` |

Las direcciones se guardan como texto decimal canónico. `0009` y `9` identifican
el mismo qubit; los valores superiores a 64 bits no se convierten a enteros de
máquina. Una dirección como `10^100` se escribe como un `1` seguido por cien
ceros, no como una expresión con exponente.

El dominio del modelo es ℕ. Cada ejecución impone presupuestos a la entrada y
a sus componentes activos. El límite configurable de dígitos controla qué
direcciones se pueden introducir en esa ejecución; la definición del fondo no
depende de ese límite. No se asignan los qubits intermedios entre dos direcciones.

El fondo Bell describe correlaciones mediante una regla matemática. Consultarlo
no prepara físicamente infinitas parejas ni ejecuta infinitas operaciones. Cada
programa, puerta, medición y observable tiene soporte finito.

## Representación y conservación del estado

Una consulta a una región intacta evalúa la regla sin crear registros de qubits
persistentes. Cuando una puerta necesita esa región, el núcleo materializa su
estado local. En el fondo Bell activa siempre ambos miembros de la pareja,
porque operar o medir uno puede cambiar sus correlaciones con el otro.

Cada componente activo contiene una lista de direcciones y un `CompactCore`.
Si una puerta une dos componentes, `CompactCore::tensor_product` concatena sus
**estados actuales**, conservando los generadores y sus signos; después aplica
la puerta. No reconstruye el estado desde una lista histórica de operaciones.
Los componentes que no participan en la operación conservan su estado.

Las operaciones admitidas son `H`, `S`, `X`, `Y`, `Z`, `CX` y `CZ`, los productos
Pauli finitos y las mediciones en Z. La evolución Clifford y las expectativas
Pauli usan álgebra de estabilizadores exacta. Las expectativas son `-1`, `0` o
`1`; las probabilidades de Z son `0`, `0.5` o `1`. Como el `CompactCore` original,
esta representación no almacena la fase global, que no cambia esos observables.

`compact` comprueba si un componente completo ha vuelto exactamente a su fondo.
Para `zero` verifica todos sus generadores Z; para `bell`, XX y ZZ de cada pareja.
Solo entonces libera su almacenamiento explícito. No descarta cambios por ser
pequeños, no recorta ramas y no realiza una separación aproximada. Un componente
que contiene partes ya independientes puede seguir almacenándose unido; esta
primera versión no incorpora una factorización general automática.

## Compilación y primer recorrido

Con CMake y un compilador C++17:

```sh
cmake -S . -B build-infinite -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-infinite --config Release --parallel 2 --target \
  mundo_x_infinite test_infinite_address test_infinite_core test_infinite_tensor infinite_scale
ctest --test-dir build-infinite -C Release --output-on-failure -R '^infinite_'
```

La prueba del contrato CLI se registra si CMake encuentra Python 3. También se
puede ejecutar explícitamente:

```sh
python3 tools/validate_infinite_cli.py --binary build-infinite/mundo_x_infinite
build-infinite/infinite_scale
```

En Windows con un generador de varias configuraciones, los ejecutables quedan
en `build-infinite/Release/` y terminan en `.exe`. La validación automática tiene
trabajos separados para Linux y Windows y conserva sus binarios y resultados
como artefactos `InfiniteCore-*` del workflow.

Compilación directa del ejecutable, sin dependencias de simuladores externos:

```sh
g++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -I native/include \
  native/src/infinite_runner.cpp -o mundo_x_infinite
./mundo_x_infinite examples/infinite_zero.xi
./mundo_x_infinite examples/infinite_bell.xi
```

Los dos ejemplos incluyen aserciones ejecutables. `infinite_zero.xi` crea una
pareja Bell entre `0` y `10^100`, verifica XX/YY/ZZ y aplica la inversa para volver
al fondo. `infinite_bell.xi` consulta el fondo, cambia y recupera una fase y
comprueba la correlación después de seleccionar un resultado de medición.

## Lenguaje `.xi`

Se acepta un archivo o la entrada estándar: `mundo_x_infinite [opciones]
[programa.xi|-]`. Cada línea contiene una instrucción; `#` inicia un comentario.
Los nombres de comandos aceptan mayúsculas o minúsculas. Las reglas `zero` y
`bell`, y los ejes Pauli `I/X/Y/Z`, usan la escritura indicada.

| Instrucción | Comportamiento |
|---|---|
| `background zero` / `background bell` | Regla inicial; solo como primera instrucción |
| `seed 42` | Inicializa el generador de mediciones con un entero sin signo de 64 bits |
| `h Q`, `s Q`, `x Q`, `y Q`, `z Q` | Puerta de un qubit |
| `cx Q R`, `cz Q R` | Puerta de dos direcciones distintas |
| `expect X:Q Y:R` | Expectativa del producto Pauli indicado |
| `expect - Y:Q Y:R` | Expectativa con signo negativo explícito |
| `expect` | Identidad, expectativa `1` |
| `probz Q` | Probabilidad de obtener `1` al medir Z |
| `measure Q` | Muestrea y confirma una medición Z |
| `postselect Q 0` / `postselect Q 1` | Confirma la rama indicada y devuelve su probabilidad previa; rechaza ramas imposibles |
| `stats` | Estadísticas del estado materializado y presupuestos |
| `compact` | Libera los componentes que coinciden exactamente con el fondo |
| `begin` … `end` | Confirma un lote de puertas como una sola transacción |
| `assert expect -1 Y:Q Y:R` | Termina con error si la expectativa difiere |
| `assert probz Q 0.5` | Termina con error si la probabilidad difiere |

Un observable admite como máximo un término por dirección canónica, incluido
`I`. `X:9 Z:0009` se rechaza: la API pide un producto Hermítico con soporte
explícito y sin direcciones repetidas. Los enteros se escriben con dígitos ASCII;
signos, notación hexadecimal, exponentes y caracteres Unicode no son direcciones.

### Ejemplo breve

```text
background zero
begin
h 0
cx 0 1000000000
end
expect X:0 X:1000000000
expect Y:0 Y:1000000000
probz 1000000000
stats
```

Las tres consultas devuelven respectivamente `1`, `-1` y `0.5`, en líneas JSON.
Las estadísticas muestran dos qubits activos, un componente de dos qubits y
34 bytes de datos del tableau. El texto de las direcciones cuenta aparte.

`measure` informa `outcome_one`, la probabilidad previa de ese resultado y si
la medición fue aleatoria. Una medición determinista no materializa regiones
intactas, no consume aleatoriedad y no incrementa la revisión. `postselect`
tampoco consume aleatoriedad. Los programas con la misma semilla reproducen sus
resultados para esta implementación.

Los archivos `.xi` guardan programas reproducibles. Esta versión no tiene todavía
un formato para guardar y restaurar directamente el estado vivo de InfiniteCore
ni integración con los proyectos `.cosmic` o con la interfaz Android.

## Presupuestos y transacciones

| Opción | Valor predeterminado | Qué limita |
|---|---:|---|
| `--max-active-qubits` | 4.096 | Total de direcciones materializadas, incluidas parejas Bell necesarias |
| `--max-component-qubits` | 1.024 | Tamaño de cada componente materializado |
| `--max-state-bytes` | 67.108.864 | Suma del contenido lógico de los tableaus del estado candidato y confirmado |
| `--max-address-digits` | 4.096 | Longitud de cada dirección de entrada antes de normalizar ceros |
| `--max-query-terms` | 4.096 | Número de términos en un observable Pauli general, incluidos `I` |

El presupuesto de estado se comprueba antes de asignar un tableau nuevo o de
construir una unión. También rige los estados intermedios de un lote. Un lote que
necesite temporalmente un componente mayor se rechaza aunque una inversa al final
lo devolvería al fondo. No hay reducción automática del problema solicitado.

Los límites pueden configurarse al construir la API o al arrancar el ejecutable.
Cero es válido para todos salvo el presupuesto de dígitos. `probz` y la consulta Z
necesaria para medir tienen soporte fijo y no dependen de `max_query_terms`.
Las consultas de fondo pueden funcionar con cero qubits activos permitidos.

**Los contadores de estado no son una medición de RAM total.** Para un componente
de `n` qubits, `tableau_bytes` cuenta `16 × n × ceil(n/64) + n` bytes.
`address_bytes` suma la longitud del texto canónico una vez por dirección activa.
Mapas, copias de cadenas, capacidades reservadas, asignadores, observadores,
generador aleatorio y copias transaccionales necesitan memoria adicional.
Consultar el fondo tampoco significa cero trabajo ni cero memoria temporal.

Las escrituras usan un estado candidato y copian los componentes compartidos
antes de modificarlos. Una excepción por validación, presupuesto o asignación
descarta ese candidato. En las mediciones también se confirma conjuntamente el
generador aleatorio: un rechazo no consume un resultado futuro. `apply({})` no
hace cambios; un lote no vacío confirma una revisión; `seed` no cambia la revisión
cuántica; `compact` solo la incrementa si libera qubits.

El CLI admite hasta 1 MiB por línea, 65.536 puertas por lote y 16 MiB de texto de
direcciones canónicas por lote. No permite lotes anidados ni consultas dentro
de un lote. Un error detiene el programa con código distinto de cero e indica
la línea. Las instrucciones ya confirmadas antes de un lote fallido conservan
sus efectos; la atomicidad corresponde a cada instrucción o lote, no al archivo
entero. La API C++ recibe vectores del llamador, quien debe presupuestar su entrada.

## Alcance comprobado y pendientes

Las [pruebas independientes](../native/tests/infinite_core.cpp) contrastan
circuitos pequeños con `DenseCore64` y con acción explícita de matrices Pauli y
proyecciones. Las [pruebas de tensor](../native/tests/infinite_tensor.cpp)
comprueban estados, signos, rango de generadores y cruces de palabras de 64 bits.
Se inyectan fallos de asignación para comprobar la conservación transaccional.
La [validación del CLI](../tools/validate_infinite_cli.py) comprueba el ejecutable
como proceso independiente, incluidos errores y los ejemplos publicados.

El [registro de esta primera etapa](../results/infinite_core_2026-10-19.md)
contiene resultados medidos y hashes de fuentes. `infinite_scale` reproduce casos
con direcciones cada vez más largas, consultas de fondo, operaciones repetidas y
un componente GHZ de 1.000 qubits. Sus tiempos son observaciones de ese proceso,
no una promesa para todos los circuitos o dispositivos.

La complejidad aumenta con el tamaño de los componentes activos, el trabajo de
los observadores y la longitud de las direcciones. Un componente de `n` qubits
utiliza almacenamiento cuadrático empaquetado. El núcleo no admite T ni otras
rotaciones generales, operaciones sobre infinitos sitios de una vez, reglas de
fondo arbitrarias, separación general de componentes ni procesamiento cuántico
físico. La siguiente integración puede incorporar este módulo a una sesión
persistente y a un visor sin cambiar el significado de sus estados y límites.

## Fuentes del código

- [InfiniteCore](../native/include/mundo_x/infinite_core.hpp): regla, excepciones,
  operaciones, consultas y transacciones.
- [Direcciones](../native/include/mundo_x/infinite_address.hpp): texto decimal y
  parejas Bell.
- [CompactCore](../native/include/mundo_x/compact_core.hpp): núcleo existente;
  esta rama añade `tensor_product` sin alterar las operaciones anteriores.
- [CLI](../native/src/infinite_runner.cpp): programas, entrada acotada y JSON.
- [Workflow](../.github/workflows/infinite-core.yml): compilación y evidencia por plataforma.

No se incorporó un motor cuántico externo. Los algoritmos del nuevo módulo
operan sobre el núcleo y el observador de Pauli del repositorio.
