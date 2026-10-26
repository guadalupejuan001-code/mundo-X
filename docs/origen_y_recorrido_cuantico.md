# Origen declarado y recorrido cuántico verificable

> **Nota de mantenimiento (25-10-2026):** Bridge X y Transformer X v0.1 se retiraron de `main`. Este documento conserva su evaluación histórica y el ejemplo Python siguiente solo es ejecutable recuperando esos fuentes desde el commit `7d0e919fc13eb5340d86cb6f7cb2b6cd97172a8a`. Consulte [decisión técnica](retired-transformer-x-2026-10-25.md).

## Relato del titular

El 18 de octubre de 2026, el titular explicó que comenzó preguntándose qué
había antes de las letras en el lenguaje de una computadora. Quiso minimizar
la representación de un símbolo, como `A`, y después resolver cómo organizar
muchas señales para convertirlas en lenguaje útil para una persona. De ahí
pasó a pensar en la experiencia de la computación cuántica y en su interfaz.
Este párrafo resume el relato aportado; no es una transcripción de chats
anteriores. «Caos», «energía» y «catalizar» son términos de ese relato que aún
requieren una definición operativa y mediciones para convertirse en hipótesis
físicas o especificaciones técnicas.

La intención descrita es más amplia que un simulador particular: investigar
cómo representar, transformar y consultar información. El código permite
examinar implementaciones concretas de esa intención.

## Lo que documenta el historial

Se recuperó el historial completo de `main`, con 156 commits hasta `eddc2c4`.
El primer commit no contiene una explicación del origen personal.

| Commit | Etapa documentada |
| --- | --- |
| `4cc75ef` | Modos activos, identidad por posición y estado complejo `z`; bytes → Bridge X → Transformer X. El README ya distingue este modelo de los bits usados por la máquina. |
| `4744607` | Quantum Core como referencia matemática; Transformer e interfaz como etapas diferenciadas. |
| `edfa2f5` | Compact Core con generadores estabilizadores empaquetados. |
| `d3e7d20` | Intérprete XASM. |
| `2da9e17`, `1395e5b` | Proyección Z forzada y protocolo de teletransportación simulada. |
| `47caf6d`, `a621e9e` | Descomposición con T y representación reducida que acumula componentes equivalentes durante la evolución. |
| `0085ae8` | Interfaz cuántica Android dedicada. |
| `f6c5614`, `eddc2c4` | Exámenes ampliados, prototipo Factorized posterior, retirada del controlador y mitigación localizada del test MSVC. |

Estas son etapas registradas en Git; no permiten atribuir cada frase histórica
a una persona ni reconstruyen por sí solas las conversaciones originales.

## Señal, información y estado cuántico

Cambiar el nombre o el soporte de un símbolo no elimina la información que debe
distinguirse. Un símbolo entre 256 posibilidades necesita al menos ocho bits
con un código binario de longitud fija. Una estructura puede ahorrar mediante
regularidades, contexto compartido o evitando materializaciones innecesarias.
Hay que contar también identidad de modos, reglas compartidas, precisión,
memoria temporal y costo de lectura.

`BridgeX` conserva frecuencias de bytes y fases medias de posición. Es un
resumen con pérdida, no un codificador reversible general. Por ejemplo,
`ABBABBBB` y `BAABBBBB` tienen los mismos conteos y las mismas direcciones de
fase media para A y B. El cuerpo histórico de `_state_for_mode`, convertido a
float32, produce payloads idénticos para ambos. Se comprobó ese cuerpo extraído
del fuente con la biblioteca estándar; la pila completa PyTorch no se ejecutó
en esta revisión porque no estaba instalada.

Con los archivos presentes en el commit histórico indicado, la comprobación era (no ejecutable en `main` actual):

```python
import torch
from src.bridge_x import BridgeX

bridge = BridgeX()
ids_a, z_a = bridge.encode(b"ABBABBBB", top_k=256)
ids_b, z_b = bridge.encode(b"BAABBBBB", top_k=256)
assert torch.equal(ids_a, ids_b)
assert torch.equal(z_a, z_b)
```

Este límite corresponde a Bridge X. Compact y Reduced utilizan otras
representaciones y conservan las propiedades cuánticas que comprueban sus
respectivos exámenes. Transformer nunca llegó a integrarse en esos núcleos y se retiró del árbol activo.

Las fases e interferencias también existen en sistemas ondulatorios clásicos.
En Mundo X, el comportamiento cuántico se comprueba con estados normalizados,
operaciones, entrelazamiento y probabilidades de medición definidas. La CPU
evalúa ese modelo matemático. Una realización física requiere un portador
controlable que conserve coherencia y permita preparación, operaciones y
medición; instalar un sistema operativo no aporta ese recurso físico.

## Recorrido programable añadido

Sobre el Compact Core existente se incorporaron:

- `probz q`: consulta de `P(0)` y `P(1)` en Z, sin modificar el estado.
- `measure q`: muestreo según esas probabilidades y proyección del tableau.
- `seed n`: semilla uint64 del generador seudoaleatorio reproducible.

Las probabilidades Z de un estado estabilizador puro son exactamente 0, 1/2 o
1. Las ramas aleatorias usan un bit de `mt19937_64`; las deterministas no
consumen el generador. La semilla inicial es 0. En la práctica interactiva,
`reset` reinicia los qubits y conserva el estado del generador.

La consulta aleatoria inspecciona la columna del tableau. Una consulta
determinista busca primero un generador Z individual; si necesita combinar
generadores, usa eliminación GF(2), hasta O(n³) trabajo escalar y O(n²) bits
temporales. No expande amplitudes densas ni selecciona otro núcleo. La memoria
lógica informada por `bytes` sigue contando el estado persistente, no el pico
temporal de esa consulta.

```powershell
.\mundo_x.exe run examples\measurement_interference.x
.\mundo_x.exe run examples\measurement_bell.x
.\mundo_x.exe practice 2
```

La primera demostración conserva probabilidades 50/50 tras H y Z; la segunda H
convierte la diferencia de fase en `P(1)=1`. En Bell, ambos qubits tienen
marginales 50/50 antes de medir; medir uno determina la siguiente medición del
otro. La semilla permite repetir la ejecución. Son ejemplos de simulación
Clifford en XASM; T continúa disponible en los experimentos del runtime.

## Próxima pregunta de arquitectura

Para conectar el origen del proyecto con una tarea útil, definir primero qué
debe conservarse de entradas como `A`, `AB`, `BA` y secuencias mayores, y qué
operación debe responder sobre ellas. Si se exige reconstrucción, medir
colisiones y recuperación exacta. Si se permiten resúmenes, medir el error y
el resultado de la tarea. Después se puede especificar cómo esos datos
preparan un estado y qué observable responde a la pregunta.

Ese contrato permitirá mejorar la estructura con una medida de éxito propia,
además del número de qubits o de los efectos visuales.
