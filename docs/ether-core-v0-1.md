# EtherCore v0.1 — dominio infinito por estructura

> Referencia histórica de la versión inicial. La integración actual se describe
> en [Ether: un estado compartido](ether-fusion.md). Las limitaciones de esta
> página corresponden a v0.1.

## Hipótesis

No aumentar un registro finito hasta llamarlo infinito. El dominio lógico es

`Q = {q0, q1, q2, ...}`

y no existe un campo `qubit_count` en EtherCore.

El estado se define por una condición base y reglas globales. v0.1 usa
`|0>^∞` como base y dos capas Clifford exactas:

- `HAll`: H sobre cada sitio.
- `CZNeighbors`: CZ sobre cada arista `(i,i+1)`.

Una sola instancia de cada capa representa una familia infinita de puertas.

## Evaluación

Para un observable de soporte finito `P`:

`<P>_final = <0^∞ | U† P U | 0^∞>`

EtherCore propaga `P` hacia atrás por las reglas. No recorre el dominio ni
materializa los sitios entre q0 y la dirección consultada. El coste depende de:

1. profundidad de reglas no canceladas;
2. tamaño del cono local creado por la consulta;
3. longitud textual de las direcciones concretas.

No depende del valor numérico del índice como una población de qubits.

## Normalización

Dos `HAll` consecutivos se cancelan. Dos `CZNeighbors` consecutivos también,
porque todas las CZ de la capa conmutan y `CZ²=I`. Esto evita guardar historial
redundante.

## Verificación inicial

El test `native/tests/ether_core.cpp`:

- comprueba q0 y una dirección decimal de 1001 dígitos;
- verifica el estabilizador de borde `X0 Z1`;
- verifica `Z(i-1) X(i) Z(i+1)` en `i=10^1000`;
- compara 256 productos Pauli locales contra un statevector independiente de
  seis qubits para la misma cadena cluster;
- ejecuta 100.000 consultas del estabilizador lejano.

En una compilación local C++17 `-O3`, las 100.000 consultas de la dirección de
1001 dígitos tardaron aproximadamente 0.53 s en una ejecución observada. Es una
muestra de host, no un benchmark de Android y no demuestra ventaja frente a
todos los backends.

## Qué NO demuestra

- No son qubits físicos infinitos.
- No ofrece memoria ni cómputo físicos infinitos.
- No representa todavía un estado cuántico arbitrario del dominio infinito.
- No soporta todavía mediciones, T/no-Clifford, reglas periódicas generales ni
  defectos/excepciones locales persistentes.
- No sustituye InfiniteCore en producción.

## Siguiente frontera

La siguiente versión debe evitar una lista creciente de capas para familias más
generales. La dirección propuesta es una forma normal algebraica de reglas
locales/traslacionales (por ejemplo una representación polinómica sobre GF(2))
más un conjunto disperso de defectos. El criterio de aceptación es el mismo:
misma respuesta exacta con menos tiempo/memoria antes de ampliar capacidad.
