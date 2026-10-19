# Motor X: integración de escenas en Mundo X

Esta rama abre la línea visual del proyecto. El mismo runtime `mundo_x` puede
ejecutar un cálculo y crear una escena mediante reglas y cambios persistentes.
Los núcleos de cálculo conservan sus algoritmos. La ruta gráfica desarrollada
en la rama anterior se conserva como consumidor de esas escenas.

La idea del titular es incorporar su motor visual a Mundo X. La imagen del
«agujero negro» orienta esa investigación; todavía no especifica un algoritmo
de absorción universal. Este primer paso conecta componentes existentes con
un contrato comprobable: una escena nativa debe reconstruirse igual en XLab.

## Recorrido implementado

```text
Programa .x
  ├─ operaciones de cálculo → CompactCore
  └─ operaciones visuales   → VisualScene: regla + excepciones
                                   ↓ MUNDOX_OBJECTS_V1
                          Restaurar escena en XLab
                                   ↓
                       ObjectSceneModel → renderizador GPU
```

La transferencia actual es una exportación e importación explícita. No hay
sincronización continua, descubrimiento automático de reglas ni un renderizador
nativo de escritorio. La representación visual reside en
`native/include/mundo_x/visual_scene.hpp`; el lenguaje común está en `xasm.hpp`.
No se añade una biblioteca o un motor gráfico externo.

## Ejecutar

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
./build/mundo_x run examples/visual_interference.x
./build/mundo_x run examples/visual_interference.x --scene-stdout
```

En una compilación MSVC, el ejecutable está en `build/Release/mundo_x.exe`.
`--scene-stdout` produce únicamente el texto de la escena final. Copia el bloque
que empieza por `MUNDOX_OBJECTS_V1` y usa **Restaurar escena** en XLab · Objetos.
El código nativo no abre ni reemplaza un archivo de destino. Si un programa falla,
no declara una escena o falla una aserción, esa opción devuelve error sin exportar
una escena parcial. El modo normal sigue mostrando el informe de ejecución.

El ejemplo `visual_scene.x` funciona sin declarar qubits. El ejemplo
`visual_interference.x` registra tres momentos del cálculo en los objetos
496, 497 y 498, y oculta el 499. Esas consultas no miden ni modifican el estado
del cálculo, y no consumen su generador seudoaleatorio.

## Instrucciones nuevas

| Instrucción | Efecto |
|---|---|
| `scene <cantidad> <semilla>` | Declara una escena; la semilla es un entero de 32 bits con signo. |
| `scene_edit <id> <estado>` | Aplica una excepción: 0 restaura, 1–6 elevan, 7 oculta. |
| `scene_clear` | Elimina las excepciones conservando la regla. |
| `scene_info` | Informa cantidad, excepciones, versión y bytes lógicos. |
| `scene_probz <id> <qubit>` | Copia una probabilidad Z actual al estado visual del objeto. |

`scene_probz` usa las probabilidades del núcleo estabilizador: 0, 0,5 y 1 se
traducen respectivamente en estados visuales 0, 3 y 6. Es una convención explícita
de esta primera interfaz. Registra una instantánea, no un enlace permanente.
Una altura no representa el estado cuántico completo ni sus correlaciones. La
consulta tiene el coste del núcleo existente; no se ejecuta por cada objeto ni
en cada cuadro de dibujo.

Sólo se admite una declaración de cada dominio por programa. Las instrucciones
cuánticas conservan su contador `instructions`; las ediciones visuales tienen
`visual_instructions`. Una edición aceptada cuenta como instrucción incluso
si no cambia el estado; la versión de la escena sólo aumenta cuando hay un cambio.

## Compatibilidad y coste

`VisualScene` reproduce la regla, el hash de 32 bits, las operaciones float32,
los IDs y el formato de `ObjectSceneModel`. Conserva los límites existentes de
1.048.576 objetos y 4.096 excepciones. Su memoria persistente crece con las
excepciones, no con una lista de transformaciones de toda la población.

Los `32 + 8 × excepciones` bytes describen el formato lógico. El contenedor C++,
la exportación, las estructuras Java y el dibujo consumen memoria adicional.
La representación por reglas no implica coste cero ni compresión de cualquier
información. Las mediciones gráficas previas pertenecen a su cámara y ensayo;
esta integración no introduce una nueva cifra de FPS.

El modelo admite cualquier cantidad dentro del límite. La interfaz Android
actual sólo restaura sus seis perfiles: 1.024, 4.096, 16.384, 65.536, 262.144 y
1.048.576 objetos. Los ejemplos usan perfiles compatibles. El texto intercambiado
usa saltos de línea LF; el modo de exportación conserva esos bytes también en
Windows.

## Verificar

```sh
ctest --test-dir build --output-on-failure
python3 tools/visual/check_visual_cli.py --binary build/mundo_x
python3 tools/visual/check_scene_bridge.py --output scene-bridge.json
```

La última comprobación compila y ejecuta los modelos C++ y Java reales. Compara
el formato intercambiado y los bits de las transformaciones, incluyendo todos
los IDs de una escena de un millón, semillas extremas y ediciones dispersas.
Consulta `--help` para las herramientas de compilación admitidas.

[Resultados registrados de esta integración](../results/motor_x_visual_2026-10-18.md).

El siguiente diseño puede ampliar las reglas y los tipos de objetos manteniendo
este contrato. Cada ampliación debe definir qué información conserva, qué puede
reconstruir y cómo se verifica su coste y su fidelidad.
