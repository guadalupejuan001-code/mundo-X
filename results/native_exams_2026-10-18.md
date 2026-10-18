# Mundo X: nombres y exámenes ampliados del 18 de octubre

> Registro de las mediciones del commit `68816d2`. Después, el titular aclaró
> que «computasis» era una analogía de homeostasis. El controlador añadido se
> eliminó del código activo, incluidos su comando y sus pruebas específicas.
> Ahora hay siete pruebas CTest en Linux. Las mediciones y la comparación
> de archivos que siguen describen aquel commit, anterior a esa corrección.

**Motor** es un término funcional: el código que representa un estado, aplica
operaciones y permite consultar el resultado. El proyecto se llama **Mundo X**;
su núcleo compacto original se llama **CompactCore**, y su extensión original
para Clifford+T, **ReducedExactTRepresentation**. El [mapa de componentes](engine_map_2026-10-18.md)
identifica cada clase y su origen.

Los resultados de cientos de millones de qubits pertenecen a **FactorizedCore**,
el prototipo añadido en `6b2d90b`, solo o envuelto por el controlador **Computasis**.
No son resultados de CompactCore ni de Reduced. No existe un selector automático
entre esas representaciones. Los bloques de amplitudes del prototipo están
implementados en el propio código C++; esta fase no integra otro simulador.

Esta revisión añade exámenes, supervisores, documentación y tres pruebas CTest.
Los siete headers nativos y `native/src/main.cpp` son idénticos byte por byte a
`3125880`. El repositorio original `/workspace/mundo-X` permanece en `f858053`.
Se trabaja en `codex/native-exams-20261018`, dentro de un checkout separado.

## Productos de hasta 200 millones de qubits

Se ejecutaron cinco procesos, uno por vez y fijados a una CPU: HT con 150 millones
como paso previo, HT con 200 millones en ambos modos y productos heterogéneos con
200 millones en ambos modos. La familia heterogénea utiliza ocho patrones locales;
su diversidad adicional se examina por separado más abajo. No se guarda el circuito
completo ni se materializan `2^N` amplitudes.

| Familia y componente | N | Inicio (s) | Evolución (s) | Consultas (s) | Proceso completo (s) |
|---|---:|---:|---:|---:|---:|
| HT, FactorizedCore | 150.000.000 | 5,454 | 2,849 | 6,368 | 18,782 |
| HT, FactorizedCore | 200.000.000 | 6,471 | 3,724 | 8,498 | 24,144 |
| HT, Computasis | 200.000.000 | 5,945 | 4,541 | 8,499 | 24,474 |
| Heterogéneo, FactorizedCore | 200.000.000 | 6,352 | 3,855 | 7,256 | 24,689 |
| Heterogéneo, Computasis | 200.000.000 | 6,076 | 5,699 | 7,280 | 26,302 |

El proceso completo incluye preparación y huellas del circuito y las consultas,
validación y destrucción; no equivale a sumar solamente las tres etapas mostradas.
Son mediciones individuales en esta máquina, no intervalos estadísticos.

A 200 millones, la carga de amplitudes es **6.400.000.000 bytes** y el pico RSS
máximo del proceso, **11.251.965.952 bytes, unos 10,48 GiB**. La estimación de
reserva de estructuras es 11.200.000.400 bytes; no incluye todos los gastos del
proceso. La diferencia evita confundir carga lógica con RAM real.

Los cinco casos pasan el criterio previamente fijado
`min(1e-7, max(3e-11, 4*N*DBL_EPS))`, que en estos tamaños vale `1e-7`.
**Los cinco fallan el criterio anterior de `1e-8`.** El máximo error absoluto en
logmagnitud es `5,421316e-8` para HT y `3,441892e-8` para el heterogéneo.
En HT de 200 millones también falla la fase con el criterio anterior:
`1,341823e-8` radianes. El umbral de escala es una heurística declarada antes de
ejecutar, no una cota matemática demostrada. No se reajustó después de ver los datos.

Las consultas grandes comprueban tres probabilidades locales, tres expectativas Z
y dos amplitudes de cadenas completas en forma logarítmica, con referencias
analíticas. Las dos cadenas cronometradas tienen soporte; las pruebas de cero
van aparte. Esto no verifica individualmente todas las amplitudes del estado.

Ambas ejecuciones con Computasis registran cero entradas en presión y cero
recuperaciones: 6,4 GB están debajo del umbral alto del 80 % de su presupuesto
de amplitudes de 8 GiB. **Su éxito aquí no demuestra homeostasis.**

## Diversidad local con referencia separada

Seis casos: 1.000, 100.000 y 1.000.000 de qubits, cada uno con 32 y 64 puertas
Clifford+T por qubit, generadas a partir de semilla e índice. Una referencia propia
usa matrices genéricas 2×2 de `complex<long double>`; no llama a las rutinas de
puertas de FactorizedCore. Comprueba **las 2.202.000 probabilidades locales** y
**12 consultas globales** de los seis casos. Todos pasan también `1e-8`.

Errores máximos: probabilidad local `8,8843e-16`, logmagnitud `1,5765e-9`, fase
circular `3,9824e-12`. A un millón, la evolución tarda 448 ms con 32 puertas por
qubit y 874 ms con 64. La referencia y validación requieren 1.168 y 2.031 ms,
respectivamente, medidos aparte; las dos consultas, unos 94–98 ms. Pico RSS real
del trabajador: 58.400.768 bytes, **55,70 MiB**.

Se muestrean hasta 4.096 huellas de secuencias, todas distintas en estos casos;
esto no implica estados físicos distintos. Las cadenas consultadas se eligen
con coeficientes de soporte suficiente, para evitar salidas triviales por cero.
Esa elección solo selecciona consultas: no poda amplitudes ni modifica el estado.
Se conserva una referencia por qubit a la vez, sin almacenar todos los circuitos.

## Computasis bajo presión sin candidatos

Se comparan el núcleo directo, el controlador normal y un controlador con el
mismo presupuesto ajustado, con y sin recuperación automática. En el presupuesto
ajustado, el umbral alto es 75 %, el bajo 50 % y el intervalo es ocho puertas.
Todos los factores ya son individuales; no hay cortes recordados para recuperar.

**20 procesos nuevos y 12.440.215 comprobaciones, sin fallos.** Las huellas de
consultas coinciden en los cuatro modos y tres tamaños. A un millón de qubits,
medianas de tres ejecuciones:

| Modo | Evolución (ms) | Pasadas de recuperación | Separaciones exitosas |
|---|---:|---:|---:|
| FactorizedCore directo | 17,772 | 0 | 0 |
| Computasis normal | 20,206 | 0 | 0 |
| Presión, automática desactivada | 21,327 | 0 | 0 |
| Presión, automática activada | 33,280 | 250.000 | 0 |

La automática añade **56,0 %** al tiempo de evolución frente al controlador bajo
la misma presión con recuperación desactivada. Son pasadas vacías, sin intentos
de separación ni trabajo estimado útil. El cronómetro interno registra 5,269 ms
dentro de ellas; no explica por sí solo toda la diferencia. Las consultas y su
error se conservan, y el pico RSS ronda 55,25 MiB.

Esto identifica una oportunidad concreta: evitar o espaciar pasadas cuando no
hay candidatos y reactivarlas cuando cambia la estructura. Haría falta modificar
y volver a medir el controlador para afirmar una mejora. Esta fase **mide el
problema y conserva producción**, sin atribuirle un arreglo todavía inexistente.

## Exámenes de los núcleos originales

El trabajador `native_original_exams` verifica 45 casos pequeños y tres rechazos
de dimensión. Incluye circuitos Clifford seguidos de su inversa, firmas analíticas
y Clifford+T contra `DenseCore64`, con alineación de fase global y errores de
norma, L2 y amplitudes menores que `1e-9`. DenseCore64 es una referencia del propio
repositorio, no una comprobación de un simulador externo.

CompactCore completa GHZ hasta **32.769 qubits** con firma completa y generadores
seleccionados verificados. En ese caso: inicio 0,100 s, evolución 14,301 s y
verificación completa 1,334 s; carga lógica 269.000.721 bytes. En 32.768, la
evolución tarda 21,343 s; ese salto de tiempo queda registrado sin inferir una
causa ni modificar la disposición del tableau. GHZ de **65.536 agota 45 s** y no
alcanza una validación final. También pasan cluster lineal hasta 10.000 y circuitos
con inversa hasta 4.096 qubits.

ReducedExactTRepresentation termina productos HT de **22 qubits con 4.194.304
componentes**, en 1,130 s de evolución. La familia con ocho T por qubit tarda
12,406 s y conserva ese mismo número de componentes, aunque T⁸ es identidad en
aritmética exacta: este examen no demuestra cancelación compacta. La familia
conectada completa 20 qubits en 30,213 s y **agota 45 s en 22**. N=24 no se lanza
porque la guarda conservadora de mapas transitorios supera 2 GiB. Se comprueba
la aceptación de 63 y el rechazo explícito de 64 qubits para esta representación.

**Los casos grandes de Reduced miden evolución terminada, sin reconstrucción
densa ni validación completa del estado.** No se cuentan como pruebas de
corrección equivalentes a los casos pequeños. De las 30 observaciones: 14 PASS
(incluida la batería pequeña), 12 evoluciones completadas, dos timeouts, un
rechazo esperado y una omisión deliberada.

## Recursos, procedencia y reproducción

La máquina tiene cuatro CPU de cuota y un cgroup de 16 GiB. Cada trabajo utiliza
una CPU y un bloqueo común. Los productos grandes usan 12 GiB de espacio virtual,
8 GiB de amplitudes y 60 s. Diversidad usa 2 GiB/45 s; presión, 1 GiB/20 s.
La reserva libre de 2 GiB se supervisa y solo se detiene el hijo del examen si
se alcanza el límite. No se modifican los límites del entorno ni otros procesos.
En los productos grandes, el margen mínimo observado fue 5.718.884.352 bytes;
los contadores OOM permanecieron en cero.

**Procedencia de la escala original:** sus 30 observaciones ya habían terminado
con una versión anterior del supervisor: 2 GiB de espacio virtual, preflight con
512 MiB adicionales y sin vigilancia continua de margen. Se conserva exactamente
ese script y sus hashes; no se le atribuyen controles añadidos después. El margen
previo mínimo registrado fue 16.988.844.032 bytes. El supervisor definitivo se
verificó por separado con la batería pequeña y siete controles, incluidos timeout,
fallo tras imprimir PASS y simulación de poco margen, sin llenar realmente la RAM.
Su RSS original es un límite inferior muestreado cada 5 ms; puede perder casi
todo el pico de procesos muy rápidos y no se presenta como RAM exacta.

El supervisor original permite ahora consultar el commit de forma opcional y usa
un bloqueo temporal por defecto. Se verificó su versión final en una copia sin
`.git`: los 45 casos pequeños y tres rechazos pasan, con commit desconocido y
fuentes identificadas mediante hashes. Así, esa batería también puede ejecutarse
desde el código descomprimido.

Todos los exámenes nuevos usan C++ y Python con sus bibliotecas estándar y los
headers propios. No instalan paquetes, no usan red y no integran otros backends.
Las diez pruebas CTest pasan. Los comandos básicos, desde la raíz, son:

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 1
ctest --test-dir build --output-on-failure -j 1
python3 -B tools/examine_original_cores.py --output-dir /tmp/mundox-original --suite sanity --compute-lock /tmp/mundox-native-diversity-exams/compute.lock
python3 -B tools/examine_factorized_diversity.py --out /tmp/mundox-diversity --sizes 1000 --depths 32,64
```

Usa destinos nuevos: los supervisores preservan evidencia existente. Para ejecutar
varias familias en la misma máquina, proporciona la misma ruta de bloqueo en
`--compute-lock` o `--lock`. La escala de 200 millones requiere una máquina con
margen suficiente; el orquestador exacto va incluido en la evidencia del paquete.

El [resumen JSON](native_exams_summary_2026-10-18.json) conserva valores, límites y
hashes de producción. La evidencia completa está en
`/workspace/scratch/mundox-native-exams-20261018/`, separada por familia, con
comandos, semillas, salidas y fuentes exactas. El ZIP final incluye esa evidencia
sin ejecutables ni entornos instalados, junto al código fuente del commit final.

Para seguir resolviendo el proyecto con rigor: identifica la representación que
ejecutas, comprueba estados pequeños, aumenta una dimensión por vez y mide por
separado evolución, consultas y memoria. Conserva también los errores y límites.
Aquí el avance de escala corresponde a productos independientes; las fronteras
del núcleo original y el costo de regulación siguen siendo problemas medibles.
