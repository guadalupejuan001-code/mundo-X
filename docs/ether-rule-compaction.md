# Compactación exacta de reglas Ether · 20 de octubre de 2026

## Referencia y alcance

La referencia es `aab0930378796ebfd42f9629e2575108ca94fcff`, cabeza del
PR #25 observada al iniciar este trabajo. Incluye las mejoras de la auditoría;
`main` seguía en `7b028db2344912b2ab86a0affa2a1fe56180922b`. El desarrollo se
realiza en `codex/ether-compact-rules-20261020`. No se sustituyen CompactCore,
InfiniteCore ni la sesión compartida por otro motor.

El cambio reduce la descripción del marco y evita reconstruir sus capas para
ciertas consultas nuevas. No es una caché de respuestas. Las puertas locales,
mediciones, PC y gráficos que consultan InfiniteCore reciben la mejora a través
del mismo EtherCore; las funciones que no consultan este camino no adquieren
una aceleración automáticamente.

## Representación

Las únicas reglas globales actuales son H sobre todos los sitios y CZ sobre
todas las aristas vecinas. Ambas son involuciones: HH y CC son identidad.
Después de cancelar letras iguales adyacentes, la secuencia siempre alterna.
Por tanto, una regla inicial y una longitud determinan toda la palabra:

`H C H C H C ...` con 4096 capas se describe como `(H, 4096)`.

Internamente se almacena la última regla y la longitud, para que añadir una
regla compare directamente con la última. La primera se deriva por paridad.
Esto ocupa 16 bytes, incluido relleno, en el anfitrión de 64 bits probado;
son cero asignaciones dinámicas para la secuencia. No se cuentan aquí las
estadísticas, el estado finito ni la memoria del evaluador.

`rule_count()` conserva la **profundidad lógica**. No pasa a decir «1» para
ocultar 4096 capas. `rule_run()` devuelve primera regla y longitud;
`rules()` ofrece una vista por valor que genera las reglas al iterar.
La vista permite tamaño, consulta por índice, extremos e iteración en ambos
sentidos; no ofrece almacenamiento contiguo ni una referencia a `std::vector`.
Los consumidores del repositorio se compilan con esta interfaz. Código C++
externo que exigía explícitamente `const std::vector<Rule>&` debe adaptarse.
Los contratos C/JNI no cambian.

`append_alternating(first, count, budget)` compone palabras completas en tiempo
constante, cancelando la frontera exactamente. Comprueba el desbordamiento y
el presupuesto del resultado antes de publicar cambios. La longitud usa
`size_t`; no se convierte en un entero sin límite físico.

## Evaluación sin recorrer todas las capas

Sea A la matriz de adyacencia de la semirrecta q0--q1--q2--... . Sobre GF(2):

`F0(A)=0`, `F1(A)=I`, `F(k+1)(A)=A Fk(A)+F(k-1)(A)`.

Para un generador situado en q, el soporte de `F(r+1)(A)e_q` es:

`|q-r|, |q-r|+2, ..., q+r`.

La reflexión en el borde q0 cancela exactamente las contribuciones del lado
negativo. La alternancia H/C determina qué dos órdenes F corresponden a X y Z.
Los soportes X y Z de cada imagen están en paridades opuestas: no se produce
un Y ni un signo negativo intrínseco en la imagen de un generador X o Z.

Las consultas de un solo X/Z con al menos ocho capas usan esos dos intervalos
de paridad directamente. Las direcciones siguen siendo cadenas decimales;
no se convierten a enteros de máquina salvo cuando se ha demostrado que son
menores que un radio que sí cabe en `size_t`. Se comprueban acarreo, borde,
soporte y longitud de la dirección antes de reservar el resultado.

Para consultas dispersas que mezclan X, Y y Z, el operador se descompone como
`i^fase (producto X) (producto Z)`. Se componen las imágenes de los generadores
con el signo exacto `(-1)^(Z_actual · X_entrante)`. Los intervalos se consumen
directamente; no se crea un vector temporal de cada imagen. Esto conserva las
fases de Y y las cancelaciones entre sitios.

La selección actual de esta segunda ruta usa al menos 32 capas, hasta 16
sitios iniciales y una cota conservadora que cabe en el presupuesto del cono.
Son criterios para elegir un algoritmo, no nuevos límites de consultas: si no
se cumplen, se usa el evaluador general por capas. Si una imagen aislada
cruza el límite de dígitos aunque la consulta combinada pueda cancelarla,
también se vuelve a la ruta general. Hay pruebas explícitas de ambos retornos.
El camino general recorre parejas CZ/H sin reconstruir una lista de reglas.

Las estadísticas del pico describen el máximo soporte de un Pauli
intermedio del evaluador elegido. No equivalen al RSS ni suman las copias de
entrada/salida o los contenedores auxiliares. El algoritmo puede reducir ese
pico y admitir consultas que antes gastaban más espacio temporal.

## Persistencia y límites que permanecen

MXI1 y MXI2 conservan sus bytes y su significado. MXI2 todavía exporta una letra
por capa para que todos los lectores existentes sigan funcionando; la vista
las genera sin recrear un vector. La importación valida la secuencia en flujo
y conserva sólo su descripción compacta. **El archivo MXI2 no se ha comprimido
con este cambio.** Un formato de disco compacto requiere versionar y actualizar
todos sus lectores conjuntamente.

Se mantienen los presupuestos de InfiniteCore: 4096 capas lógicas, 4096 sitios
de cono, 4096 qubits materializados, 1024 por componente, límite de direcciones
y transporte de snapshots. La sesión continúa serializando operaciones.
Las consultas generales que no usan la ruta algebraica todavía pagan por las
capas y el soporte que recorren. El tamaño de una salida explícita tampoco
desaparece: hay que producir sus términos.

Las pruebas del EtherCore aislado representan una palabra de longitud
`SIZE_MAX`, consultan un generador junto al borde y cancelan su inversa sin
enumerarla. Esto verifica la representación algebraica y sus límites
numéricos; no significa haber ejecutado físicamente esa cantidad de puertas.

## Pruebas reproducibles

```bash
cmake -S . -B build-rules -DCMAKE_BUILD_TYPE=Release
cmake --build build-rules -j 3
ctest --test-dir build-rules --output-on-failure -j 2
python3 tools/ether_reference_check.py
python3 tools/ether_rule_compaction_check.py --output rule-comparison.json
```

El último programa requiere Linux, Python 3 y g++ y recupera la referencia
inmutable con `git show/archive`. Compila exactamente la misma carga antes y
después con C++17/O3/NDEBUG. Alterna el orden de cinco repeticiones y fija ambos
procesos a la misma CPU permitida. Cada hijo tiene 2 GiB de espacio virtual,
28 segundos de CPU y 30 segundos de pared; alcanzar el presupuesto es un fallo
identificable de la campaña.

Las asignaciones cuentan bytes solicitados a C++ `new`, tanto acumulados como
simultáneos; no representan el total del proceso ni incluyen todo `malloc`.
`process_peak_rss_kib` lee VmHWM del propio binario después de la carga.
`wait4_peak_rss_kib` se conserva aparte: puede incluir el suelo de memoria del
proceso Python antes de `exec` y no debe confundirse con el anterior.
Las latencias son mediciones del anfitrión, no de un Android ni de una GPU.

La evidencia numérica completa, los hashes del código ejecutado, las muestras
individuales y los hashes de las salidas de persistencia están en
[`ether-rule-compaction-2026-10-20.json`](../results/ether-rule-compaction-2026-10-20.json).

## Resultado de la validación

Código verificado: `35a82f88c88e40e9c2fcba7453b33fc91f51ffca`.

- **73/73 CTest** aprobados, incluidos núcleo, sesión, puente C, física de escena y PC.
- **720 792 comprobaciones** en `ether_rules`: palabras reducidas, composición por bloques, signos, borde q0, consultas mixtas, presupuestos, desbordamientos y retorno al evaluador general.
- El mismo ensayo pasa con **AddressSanitizer y UndefinedBehaviorSanitizer**. LeakSanitizer está desactivado; no se afirma una prueba de fugas.
- **4000 comparaciones** de pullbacks firmados contra el código previo: 2000 consultas con direcciones largas, 1000 consultas mixtas y 1000 consultas individuales ordinarias; cero diferencias.
- **2500 comparaciones** adicionales contra la referencia anterior a la auditoría; cero diferencias.
- Las salidas completas de guardado, recuperación y mediciones coinciden byte a byte para profundidades 0, 8, 31, 64 y 4096. Se conserva la continuación del RNG.
- La verificación de procedencia queda fijada a commits inmutables, incluido el nuevo ensayo; no se relajó su comparación de archivos.
- JNI host no pudo repetirse: este entorno carece de las cabeceras del JDK 17. No se generó un APK ni se midió un dispositivo físico. La primera ejecución de `boot_media` encontró un binario local sin permiso de ejecución; corregido el permiso del artefacto compilado, el ensayo y la suite completa pasaron sin cambiar su código.

Medianas de cinco repeticiones alternadas, misma carga, compilador y CPU:

| Trabajo | Antes, ms | Después, ms | Cociente antes/después |
| --- | ---: | ---: | ---: |
| Crear 1000 marcos de 4096 reglas | 5.357249 | 5.336709 | 1.00× |
| Copiar un marco 10 000 veces | 0.683569 | 0.005588 | 122.33× |
| Añadir 4096 reglas a InfiniteCore | 0.853010 | 0.227406 | 3.75× |
| 10 consultas nuevas cerca de q0; 4096 capas | 10.030878 | 0.014091 | 711.86× |
| 3 consultas; 1024 capas y direcciones de 4096 dígitos | 1661.307602 | 18.912428 | 87.84× |
| 10 consultas mixtas de 8 sitios; 128 capas | 7.366027 | 1.995935 | 3.69× |

Estos cocientes describen cada carga medida. La construcción directa de
reglas una a una tiene un tiempo próximo al anterior; no se anuncia una mejora
universal. Durante el desarrollo se descartó el uso de la composición genérica
para cada inserción individual porque añadía trabajo innecesario; las entradas
H/C ahora actualizan directamente el descriptor. Las consultas mixtas también
pasaron de la reproducción de capas a la composición exacta de intervalos.

La descripción de 4096 reglas en EtherCore pasa de 4096 bytes de vector más
24 bytes de contenedor a 16 bytes de descriptor en este anfitrión. Los 32 bytes
de estadísticas son adicionales en ambas versiones. La prueba de copiar el
marco 10 000 veces elimina 40 960 000 bytes de asignaciones acumuladas; esa cifra
no era memoria simultánea ni RAM del proceso.

En la carga de tres consultas con direcciones de 4096 dígitos:

| Memoria medida | Antes | Después |
| --- | ---: | ---: |
| Pico temporal solicitado a C++ `new` | 8 517 811 B | 4 256 949 B |
| Asignaciones acumuladas durante la carga | 4 890 023 148 B | 12 869 118 B |
| Mediana del pico RSS del binario | 10 048 KiB | 5 800 KiB |

La memoria acumulada cuenta muchas reservas que se liberan y reutilizan;
por eso puede superar 2 GiB sin que el proceso supere ese presupuesto.
En las consultas mixtas el pico temporal queda igual, en 15 744 bytes, mientras
las asignaciones acumuladas bajan de 1 701 120 a 347 520 bytes.
En las cargas pequeñas el RSS varía unos KiB por el ejecutable y el asignador;
no se anuncia una reducción de RSS universal.

Se volvieron a consultar las 35 ramas al cerrar: ninguna cabeza cambió desde
el inicio. El registro de referencias y los logs están en
[`ether-rule-validation-2026-10-20`](../results/ether-rule-validation-2026-10-20/).
