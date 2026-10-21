# Ether Core: contrato de arquitectura de Mundo X

**Decisión de arquitectura · 20 de octubre de 2026**

Ether será la fuente común de estado y cálculo para los componentes de Mundo X.
Este documento recoge la dirección acordada con X, creador y titular del
proyecto, y define cómo comprobar que una integración la cumple. Las
obligaciones siguientes describen la arquitectura objetivo; el alcance del
código integrado se identifica en [Estado de implementación](#5-estado-de-implementación).

## 1. Estado común, operaciones y observaciones

Dentro de una sesión, Ether debe conservar una descripción autoritativa del
estado: su condición base, reglas, relaciones y cambios. Los componentes deben
expresar qué información necesitan, qué transformaciones producen y qué
resultados observan sobre esa descripción.

Las identidades y relaciones pertenecen al estado fuente. Una cámara, una
selección o una vista parcial determinan lo que se muestra; la información
necesaria para reconstruir lo demás debe seguir conservada.

La unidad de propiedad es la sesión. Mundos distintos pueden tener estados
distintos. Compartir el código del núcleo entre aplicaciones no establece por
sí mismo que esas aplicaciones compartan una instancia de estado.

### Significado de «núcleo cuántico infinito»

La propuesta describe un dominio lógico mediante reglas, sin exigir un registro
almacenado por cada dirección. Cada operación admitida necesita una definición
y un procedimiento finito para responder dentro de los recursos disponibles.
En v0.1, el dominio es `q0, q1, q2, ...`.

El alcance representable depende de la familia de reglas implementada.
Describir un dominio infinito no proporciona RAM, energía ni velocidad física
infinitas, y no establece que cualquier estado cuántico admita una descripción
compacta.

## 2. Responsabilidad de cada componente

| Componente | Contrato objetivo con Ether | Evidencia necesaria |
| --- | --- | --- |
| Memoria lógica | Conservar el estado, sus relaciones y cambios con una representación recuperable. Definir qué se guarda y qué puede reconstruirse. | Recuperación equivalente, identidades estables y contabilidad de estado persistente y trabajo temporal. |
| Ejecución, lenguajes y PC | Expresar las transformaciones admitidas sobre esa representación. Definir cómo se componen y cuándo pueden reutilizarse sus efectos. | Resultados equivalentes para operaciones y lecturas posteriores; dependencia causal de la sesión correspondiente. |
| Gráficos | Obtener una vista del estado de la sesión mediante consultas definidas. Conservar la identidad de los objetos y declarar los datos derivados que necesita CPU/GPU. | Relación comprobable entre cambios del estado y cambios de imagen, con controles que aíslen esa relación. |
| Física, Solar y demás sistemas | Definir qué variables y relaciones del modelo residen en Ether y cómo sus operaciones producen la evolución y las observaciones. | Conservación de la semántica del modelo y comparación con una referencia apropiada para cada magnitud. |

La integración de cada componente exige concretar esta correspondencia. Las
funciones de renderizado, entrada/salida y ejecución física siguen teniendo
implementaciones y costes que deben identificarse. Los modelos existentes
sirven como referencia mientras se desarrolla y verifica su representación
sobre Ether.

## 3. Invariantes de integración

1. **Una autoridad de estado por sesión.** Los consumidores reciben la sesión
   correspondiente o una referencia explícita a su revisión. Su conexión no
   crea silenciosamente otro mundo ni reinicia el estado.
2. **Evolución explícita.** Toda operación que modifica el estado tiene una
   semántica definida y permite identificar la revisión resultante. Una
   consulta de solo lectura conserva el estado del modelo; una medición que lo
   modifica se declara como tal. La telemetría de consultas puede actualizarse
   sin alterar ese estado.
3. **Información recuperable.** Las optimizaciones conservan las respuestas y
   relaciones exigidas por el contrato, incluidos signos, fases, direcciones y
   condiciones de borde. Cualquier eliminación o aproximación debe ser
   explícita y tener un alcance declarado.
4. **Resultados derivados coherentes.** Cachés, buffers y vistas tienen un
   criterio de validez asociado al estado. Reutilizar una respuesta requiere
   comprobar que sigue correspondiendo a la revisión consultada.
5. **Propiedad y ciclo de vida definidos.** Está identificado quién crea,
   comparte y destruye cada recurso. Cerrar un consumidor o fallar durante su
   construcción debe respetar a los demás propietarios y liberar los recursos
   que correspondan.
6. **Capacidades y presupuestos explícitos.** Las operaciones no soportadas o
   que exceden un presupuesto devuelven una condición identificable. Si una
   ruta utiliza otro núcleo, su resultado conserva esa procedencia.
7. **Participación demostrada.** Para declarar integrado un componente debe
   comprobarse que una modificación controlada de Ether produce el efecto
   previsto en su salida. La prueba debe aislar Ether de otros motores y
   parámetros que puedan explicar el mismo efecto.

## 4. Diseñar también la forma de calcular

La estructura del estado y el algoritmo que opera sobre ella deben revisarse
juntos. Si un consumidor despliega y duplica sistemáticamente la información en
otra representación, puede reintroducir el gasto que la estructura de Ether
busca evitar.

Cada integración debe responder:

- ¿Qué necesita conocer y modificar el componente?
- ¿Qué parte puede resolverse directamente sobre reglas y relaciones?
- ¿Qué transformación puede componerse o reutilizarse conservando su semántica?
- ¿Qué expansión temporal es necesaria, cuánto ocupa y cuándo se libera?

La composición de reglas y las representaciones algebraicas son líneas de
trabajo. Una descripción más corta del programa debe evaluarse por separado
del trabajo necesario para ejecutarlo o consultarlo. El coste de preparación,
compilación o mantenimiento del estado también cuenta.

### Medición de RAM y tiempo

Las comparaciones deben usar la misma tarea, entradas, precisión, estado
inicial y condiciones de ejecución. Deben registrar revisión del código,
hardware, compilación y resultados correctos antes de atribuir una mejora.

La memoria se informa por separado para estado persistente, trabajo temporal,
cachés, puentes entre componentes y recursos gráficos; también se mide el
proceso completo mediante la métrica disponible, por ejemplo RSS o PSS,
indicando su alcance. El pico simultáneo y las asignaciones acumuladas son
magnitudes diferentes.

El tiempo se desglosa en preparación, actualización, consulta, transferencia y
renderizado cuando corresponda. Una reducción en una etapa no acredita
automáticamente una mejora de extremo a extremo.

## 5. Estado de implementación

### Ruta de simulación cuántica aislada

La simulación cuántica dispone de un propietario independiente,
`QuantumRuntime`, formado únicamente por `InfiniteCore` e
`InverseMemory`. Su build en `quantum-runtime/` excluye PC, gráficos, Solar,
física, Android/JNI y GLES. Esta separación evita que un consumidor no cuántico
se convierta en requisito accidental del núcleo.

`InverseMemory` mantiene una regla de fondo, diferencias clásicas finitas y
enlaces cuánticos explícitos. Los bytes clásicos son una **proyección solicitada**,
no la autoridad primaria. Una lectura no determinista produce un handle y no
mide el estado. El lenguaje XI incorpora `mem_reset`, `mem_store`,
`mem_bind`, `mem_unbind`, `mem_read`, `mem_materialize` y `mem_stats`.

Los dos experimentos previos de Ether Memory (mapeo fijo de ocho qubits por byte
y ventana x86 MemoryFabric) quedan como borradores históricos; no forman parte
de esta ruta. Véase [Inverse Ether Memory](inverse-ether-memory.md).

La [fusión de estado implementada](ether-fusion.md) sustituye la conexión
auxiliar de v0.1. EtherCore define un marco Clifford global `U`, e InfiniteCore
conserva el estado base y sus modificaciones finitas `φ`; juntos representan
**un estado físico `Ψ = Uφ`** dentro de `InfiniteSession`.

- Las puertas locales y las mediciones actúan sobre ese mismo estado mediante
  conjugación exacta; las consultas son de solo lectura.
- `NativeCore` ya no crea un EtherCore auxiliar. PC y su visor comparten la
  instancia de sesión existente. Abrir el visor no prepara ni reinicia Ether.
- Solar prepara ocho direcciones dentro de esa sesión y consulta sus trece
  señales en una sola observación con revisión y época comunes.
- Los snapshots `MXI2` conservan reglas, componentes finitos, fondo y RNG. Los
  snapshots `MXI1` anteriores siguen siendo importables.
- Graphics y COSMIC crean los nuevos mundos Android con Ether. Los proyectos
  finitos conservan su ruta explícita y su formato; otros backends siguen
  identificados por sus contratos.

Esto implementa la autoridad cuántica común para los consumidores conectados.
La física orbital de Solar, la escena y los recursos de GLES, y la CPU/RAM x86
no se han convertido en una representación común de Ether. Su futura migración
requiere definir las correspondencias y demostrar la equivalencia descrita en
este contrato.

La familia implementada sigue siendo Clifford. Las reglas generales, estados
arbitrarios y una reducción garantizada del coste para cualquier tarea no están
implementados. La profundidad del marco, su cono temporal, los componentes
finitos y los buffers de cada aplicación tienen costes y presupuestos finitos.

La [documentación v0.1](ether-core-v0-1.md) se conserva como referencia histórica;
sus carencias describen aquella revisión, anterior a la fusión actual.

## 6. Orden de implementación y aceptación

Para cada componente se debe documentar la correspondencia entre su estado
actual y Ether, implementar las transformaciones y consultas necesarias, y
verificar equivalencia con una referencia apropiada. Después se comprueba su
participación causal en la salida y se miden los costes completos.

Un componente cumple este contrato cuando:

- sus resultados dependen de las operaciones de Ether de la manera prevista;
- conserva la información y la semántica que su función requiere;
- mantiene coherentes las observaciones, revisiones y recursos compartidos;
- registra los costes y límites de la integración en una revisión reproducible.

El criterio para ampliar capacidades es evidencia de resultados correctos y
costes conocidos. La arquitectura permite avanzar hacia el núcleo común con
una obligación verificable para cada componente.
