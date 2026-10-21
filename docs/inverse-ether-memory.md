# Inverse Ether Memory · ruta cuántica pura

**Estado:** implementación candidata del 21 de octubre de 2026.

Esta revisión sustituye como arquitectura principal dos borradores anteriores:

- `codex/ether-memory-20261021`: mapeaba cada byte lógico a ocho qubits
  consecutivos mediante `qload8/qstore8`.
- `codex/ether-memory-fabric-20261021`: conectaba una ventana física x86 de
  256 bytes a ese mapeo.

Esos experimentos se conservan en su historial como antecedentes, pero no se
fusionan en esta ruta. Ambos seguían pensando desde una dirección clásica hacia
qubits. **Inverse Ether Memory invierte la autoridad:** primero existe la regla,
el estado y sus relaciones; los bytes clásicos aparecen solo cuando un
consumidor pide una proyección determinista.

## Modelo implementado

`InverseMemory` no contiene un arreglo de bytes.

Su estado persistente es:

1. **una regla global de fondo**: un solo byte describe todo el dominio lógico
   de direcciones decimales;
2. **excepciones clásicas finitas**: solo las direcciones cuyo byte difiere del
   fondo;
3. **enlaces cuánticos finitos y explícitos**: una dirección lógica puede
   observar ocho direcciones elegidas del `InfiniteCore`.

No existe una fórmula global `byte a -> q(8a)..q(8a+7)`. Ocho qubits se usan
solo cuando el programa crea explícitamente un `mem_bind`.

Una lectura de un enlace cuántico consulta Z sin medir. Si sus ocho bits son
deterministas, produce un byte. Si alguno no lo es, devuelve un identificador
`qbyte:direccion@base`. No se fabrica un valor clásico y no se colapsa el
estado. Una proyección densa con `mem_materialize` falla al encontrar un
handle cuántico.

Una escritura sobre memoria clásica cambia únicamente una excepción. Volver al
valor de fondo elimina esa excepción. Una escritura sobre un enlace cuántico
solo está permitida si el byte ya es determinista; aplica únicamente las X que
cambian sus bits.

## Runtime cuántico

`QuantumRuntime` posee únicamente:

- `InfiniteCore`;
- `InverseMemory`;
- la gramática XI/Infinite necesaria para operar ambos.

Cada programa se ejecuta sobre copias candidatas del núcleo y la memoria. Solo
si **todas** las líneas y la salida terminan correctamente se publican ambos
estados. Un error tardío descarta los cambios de los dos.

Comandos añadidos al lenguaje XI:

```text
mem_reset BYTE
mem_store ADDRESS BYTE
mem_bind ADDRESS QBASE
mem_unbind ADDRESS
mem_read ADDRESS
mem_materialize ADDRESS COUNT
mem_stats
```

Los comandos cuánticos existentes (`h/s/x/y/z/cx/cz/expect/probz/measure`,
reglas Ether, batches, aserciones, etc.) siguen disponibles.

## Componentes desconectados de la simulación cuántica

`quantum-runtime/CMakeLists.txt` define un grafo de compilación separado. No
configura ni enlaza:

- `pc-android` ni el intérprete x86/x64;
- Graphics/COSMIC ni GLES;
- Solar;
- física de escena;
- JNI/Android.

Esos componentes **no se borran** del repositorio. Conservan su historia y sus
aplicaciones, pero dejan de ser dependencias necesarias para compilar y probar
la ruta de simulación cuántica.

El workflow `quantum-only.yml` inspecciona `compile_commands.json` y falla si
alguna unidad de traducción del build aislado procede de esos componentes.

## Límites deliberados

- La RAM física del Galaxy A16 5G sigue siendo clásica.
- La memoria inversa es una representación lógica por reglas, no qRAM física.
- El fondo implementado para bytes es actualmente una regla constante global;
  aún no existen reglas arbitrarias por región.
- Los enlaces cuánticos son finitos y se presupuestan.
- `mem_materialize` crea una proyección clásica temporal y tiene un límite
  separado.
- Una lectura no determinista devuelve un handle; medir requiere un comando
  cuántico explícito.
- InfiniteCore conserva sus límites de qubits activos, componentes, tableau,
  reglas Ether y conos de consulta.

## Procedencia

Los archivos nuevos de esta ruta fueron escritos para Mundo X sobre los
componentes propios ya existentes `EtherCore`, `InfiniteCore`,
`CompactCore` y la gramática XI, usando C++17 y su biblioteca estándar.

La ruta cuántica aislada no incorpora código fuente de QEMU, Bochs, Wine,
Limbo, motores gráficos ni simuladores cuánticos externos. Las referencias a
especificaciones o herramientas externas en otros módulos no convierten esos
módulos en dependencias de esta compilación.

Esto documenta procedencia técnica del cambio; no es una declaración jurídica
de originalidad mundial de cada algoritmo.

## Criterio de aceptación

La revisión se considera integrable cuando:

1. compila desde `quantum-runtime/` sin subsistemas no cuánticos;
2. pasan las regresiones de Inverse Memory y transacción conjunta;
3. siguen pasando las pruebas extremas de Infinite/Ether y algoritmos;
4. un programa XI demuestra:
   - una dirección lógica enorme resuelta desde la regla sin materializar
     qubits;
   - un enlace cuántico determinista proyectado correctamente;
   - un enlace en superposición devuelto como handle, sin medición silenciosa;
5. la verificación de procedencia reconoce exactamente los archivos nuevos.
