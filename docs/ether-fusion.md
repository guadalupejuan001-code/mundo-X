# Ether: un estado compartido

**Integración del 20 de octubre de 2026.**

Las reglas globales de Ether, las modificaciones locales, las mediciones y las
observaciones ahora pertenecen a **un mismo estado cuántico por sesión**.
`NativeCore` ya no crea un Ether auxiliar independiente. Esta integración
implementa parte del [contrato de arquitectura](ether-architecture.md), con el
alcance y las pruebas descritos aquí.

## Representación y cálculo

El estado físico se representa como

\[
|\Psi\rangle = U_{\mathrm{Ether}} |\varphi\rangle.
\]

`U_Ether` contiene las capas globales H y CZ entre vecinos de la cadena
`q0,q1,q2,...`. `φ` contiene el fondo cero o de pares Bell y sus modificaciones
finitas, conservadas mediante los componentes CompactCore propios del proyecto.
Estas dos partes describen un estado: no se suman resultados de dos mundos
independientes.

- **Consulta:** para observar `P`, Ether obtiene `U† P U`, incluido su signo,
  y lo evalúa sobre `φ`. La consulta no materializa el fondo, aplica puertas,
  proyecta ni consume números aleatorios.
- **Regla global:** aplicar `G` actualiza el marco a `G U`, conservando las
  modificaciones finitas. Por tanto, la regla también actúa sobre los sitios
  previamente modificados.
- **Puerta local:** una puerta física `V` modifica `φ` mediante `U† V U`.
  Su soporte puede abarcar un vecindario mayor que el sitio solicitado; se
  conserva ese efecto exacto en los componentes finitos.
- **Medición:** medir Z en una dirección equivale a medir su Pauli conjugado
  en `φ`. Se conserva el signo, el bit físico, la proyección y la secuencia del
  RNG. La postselección no consume RNG.
- **Compactación:** un componente se libera cuando vuelve exactamente al
  fondo de `φ`; el marco global permanece y conserva sus correlaciones.

No se recorta una porción pura del cluster separándola de su exterior. Ese
recorte perdería entrelazamiento en la frontera. El marco permite conservar
esas relaciones al responder cada consulta finita.

## Propiedad y consumidores

`InfiniteSession` es la autoridad sincronizada. El marco, los componentes,
el RNG y las revisiones participan en sus mismas transacciones. Un error de
sintaxis, presupuesto, asignación o preparación de respuesta descarta el
candidato completo. Las vistas se invalidan mediante revisión y época; un
restore invalida también una vista con la misma revisión anterior.

| Consumidor | Comportamiento integrado |
| --- | --- |
| Graphics y COSMIC Android | Los mundos nuevos usan Ether. La UI y `QuantumRenderer` consultan el mismo `NativeCore`; se conserva la selección explícita de fondos cero/Bell y de proyectos finitos. |
| PC x86/x64 | Los comandos del huésped, el editor y el viewport conservan la misma instancia `InfiniteSession`. Abrir el viewport y leer estadísticas no preparan reglas ni reinician el estado. |
| Solar | Prepara las direcciones 0…7 y aplica las reglas al mismo estado. Sus ocho señales locales y cinco señales de Ether proceden de una única observación con revisión y época comunes. |
| Escritorio | El adaptador Infinite usa el mismo puente nativo, admite las reglas mediante `execute()`, `h_all()` y `cz_neighbors()`, y transporta los snapshots nuevos. |
| Proyectos finitos y otros núcleos | Mantienen sus formatos y contratos explícitos. No reciben un Ether auxiliar oculto ni una sustitución silenciosa de su backend. |

Las aplicaciones separadas no comparten por este cambio una instancia entre
procesos. Cada mundo tiene su propia sesión, salvo una referencia compartida
explícitamente, como ocurre dentro del PC.

Los coeficientes de Solar cambian al cambiar su preparación: ya no proceden de
un cluster auxiliar constante. Las órbitas, fuerzas y magnitudes astronómicas
conservan sus modelos propios. El renderizado GLES, los IDs y ediciones de la
escena, y la CPU/RAM x86 conservan también sus implementaciones. La
representación integral de esos subsistemas en Ether sigue siendo trabajo
adicional; esta revisión no acredita esa migración ni aceleración de una ISO.

## Uso

La gramática `.xi` incorpora dos operaciones sin argumentos:

```text
background zero
h_all
cz_neighbors
expect X:0 Z:1
z 0
expect X:0 Z:1
```

La primera expectativa es `+1`; después de `z 0` es `−1`. Ambos resultados
pertenecen a la misma sesión. [El ejemplo ejecutable](../examples/ether_fusion.xi)
incluye también cancelación de una capa y postselección.

Una petición `execute` se confirma completa o se rechaza completa. Las reglas
globales se escriben fuera de los bloques `begin/end`, que conservan su contrato
de lotes de puertas locales. Es posible intercalar ambos tipos de operación en
una misma petición.

En Java, `NativeCore.etherCluster()` crea explícitamente el estado inicial.
`prepareEtherCluster()`, `etherHAll()` y `etherCzNeighbors()` **modifican** el
estado actual. `prepareEtherCluster()` aplica H y CZ en cada llamada; no es un
reset ni un indicador idempotente. `query()`, `observe()`, `etherQuery()`,
`etherStats()` y `etherVisualProgram()` no preparan el estado.

## Persistencia y compatibilidad

`MXI1` se conserva exactamente como formato de salida cuando el marco está
vacío. `MXI2` incorpora, después de los datos del RNG y antes de los componentes,
un contador `u64` y los bytes del marco: `0 = HAll`, `1 = CZNeighbors`.

El lector valida el presupuesto antes de reservar, los códigos, el marco no
vacío y su forma canónica sin capas iguales adyacentes. Conserva la validación
de signos, conmutación, rango, direcciones, pares Bell y RNG del estado finito.
Los lectores actuales admiten ambos formatos. Un lector antiguo que solo
conozca MXI1 no puede abrir un estado MXI2.

`.cosmic` v2 conserva su sobre portable `infinite-clifford` y transporta el
snapshot opaco MXI1/MXI2; `.mxi` lo transporta para PC y CLI. Los archivos
finitos `.cosmic` v1 mantienen sus qubits y su historial, sin migración
automática. La preparación auxiliar de v0.1 nunca formó parte de esos
snapshots antiguos.

## Recursos y capacidades

| Presupuesto predeterminado de la sesión | Límite |
| --- | ---: |
| Reglas globales no canceladas | 4.096 |
| Sitios simultáneos en el cono de un Pauli conjugado | 4.096 |
| Sitios materializados | 4.096 |
| Sitios por componente finito | 1.024 |
| Payload acumulado de tableaus | 64 MiB; PC utiliza 16 MiB |
| Dígitos por dirección, incluidos vecinos generados | 4.096 |
| Términos de una consulta Pauli general | 4.096 |
| Snapshot transportado | 16 MiB |

La comprobación del cono incluye su pico temporal, no solo el soporte final.
Una puerta local puede unir componentes y superar un presupuesto; en ese caso
se rechaza sin alterar el estado. Cancelar la última capa sigue permitido
cuando el contador de reglas está en el máximo.

`stateBytes`/`tableau_bytes` cuentan buffers lógicos del tableau. Excluyen reglas,
metadatos, direcciones duplicadas, copias transaccionales, temporales, Java y GPU.
Un fondo con dos reglas puede tener cero sitios materializados y cero bytes de
tableau sin que el proceso consuma cero RAM.

El dominio no impone un número total de qubits almacenados. El coste sí depende
de la profundidad, el cono de la operación, los componentes modificados y las
direcciones. La fusión conserva exactitud Clifford; no incorpora T, estados
arbitrarios, memoria física infinita ni una mejora garantizada de velocidad.

## Verificación reproducible

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure \
  -R '^(ether_|infinite_|compact_measurement$|engine_session$|core_1000$|pc_quantum$)'

# Linux, C++17 y JDK 17 con cabeceras JNI:
python tools/validate_ether_jni.py --build-dir /tmp/mundo-x-ether-jni
```

- [`ether_frame.cpp`](../native/tests/ether_frame.cpp): fases firmadas,
  frontera q0, reglas, compactación, snapshots, RNG y presupuestos.
- [`ether_fusion.cpp`](../native/tests/ether_fusion.cpp): oráculo independiente
  de amplitudes complejas y matrices; intercalado de operaciones, proyecciones,
  correlación con sitios implícitos, fallos de asignación y puertos PC.
- [`EtherFusionJni.java`](../tests/java/com/mundox/graphics/EtherFusionJni.java):
  Java y JNI de producción en JVM anfitriona, propietarios compartidos,
  causalidad local/global, guardado, RNG, q0, direcciones grandes y Solar.
- La instrumentación Android comprueba además los consumidores y sus controles
  de imagen. Compilar Java o ejecutar JNI en Linux no equivale a ejecutar esos
  casos en Android/GLES.

Los resultados medidos y su alcance se registran en
[`ether_fusion_2026-10-20.json`](../results/ether_fusion_2026-10-20.json).
