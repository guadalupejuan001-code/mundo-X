# Lenguaje Mundo X v0.1 — describir circuitos con bloques

Este prototipo permite componer circuitos sin escribir cada operación una por una. Sus archivos `.mx` se compilan a XASM v0.2 y se ejecutan con los motores C++ de Mundo X. La mejora que busca esta versión es composición, validación y claridad de los programas; no cambia el algoritmo de simulación.

## Uso

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/mundo_x run-mx examples/mx_bell.mx
./build/mundo_x run-mx examples/mx_magic.mx
./build/mundo_x run-mx examples/mx_layers.mx
./build/mundo_x compile examples/mx_bell.mx > bell_generated.x
./build/mundo_x run bell_generated.x
```

En Windows, utiliza `build\Release\mundo_x.exe` y añade `-C Release` a CTest.

`compile` termina las comprobaciones antes de emitir XASM. Su salida estándar contiene únicamente el programa generado; los errores se escriben en la salida de error. Este comando no crea estados cuánticos ni ejecuta las comprobaciones densas. `run-mx` compila el programa completo antes de ejecutar cualquier instrucción.

Los errores indican archivo, línea y columna. Cada línea XASM generada conserva la ubicación del código `.mx` que la originó para traducir los errores del ejecutor. En un circuito reutilizable, la ubicación corresponde a la instrucción de su cuerpo.

## Configuración y tipos

```text
backend reduced;
let N = 3;
qubits N;
let first = q[0];
h(first);
```

Cada instrucción termina en `;`, salvo los bloques. Los comentarios empiezan con `#` o `//`.

- `backend compact;` o `backend reduced;` selecciona el motor. El predeterminado es `compact`. La directiva solo puede aparecer una vez, en la raíz y antes de `qubits`.
- `qubits N;` declara el registro una sola vez, en la raíz. `q[índice]` referencia uno de sus qubits; los índices comienzan en cero.
- `count` es un entero no negativo de 64 bits. Sirve para tamaños, índices y repeticiones. Las operaciones `+`, `-` y `*` comprueban desbordamientos y resultados negativos. Se admiten paréntesis y la multiplicación tiene precedencia sobre suma y resta.
- `qubit` es una referencia tipada a un qubit del registro. No puede utilizarse como un número de repeticiones ni como operando aritmético.
- `let nombre = expresión;` crea una constante inmutable e infiere su tipo. Un nombre no puede declararse dos veces en el mismo ámbito.

## Repetición

```text
backend compact;
let N = 4;
qubits N;

repeat N as i {
    h(q[i]);
}
repeat N - 1 as i {
    cx(q[i], q[i + 1]);
}
show;
```

`repeat cantidad { ... }` repite un bloque. La forma opcional `as i` crea un índice local desde `0` hasta `cantidad - 1`. Cada iteración tiene su propio ámbito; sus constantes no escapan del bloque. La cantidad se conoce durante compilación y puede ser cero.

## Circuitos reutilizables

```text
backend reduced;
qubits 2;

circuit layer(a: qubit, b: qubit, rounds: count) {
    repeat rounds {
        h(a);
        t(a);
        cx(a, b);
    }
}

layer(q[0], q[1], 4);
assert fidelity;
assert norm;
```

`circuit` define un bloque con parámetros tipados y sin valor de retorno. Las definiciones aparecen en la raíz. Las llamadas comprueban nombres, cantidad y tipos de argumentos. Los nombres de las operaciones integradas y del registro se reservan para el lenguaje.

Los parámetros y constantes locales tienen ámbito léxico. Un circuito puede utilizar las constantes globales visibles al declararse; no captura variables locales de quien lo llama. Puede definirse antes de `qubits` si trabaja con parámetros de tipo `qubit`; para referirse directamente a `q[índice]`, el registro debe estar visible en el lugar de definición. Las llamadas que ejecutan operaciones aparecen después de declarar el registro.

Los circuitos pueden componerse mediante llamadas, incluso con definiciones posteriores, pero se rechazan ciclos de llamadas y recursión. Las definiciones sin usar también se comprueban para detectar errores estáticos; las condiciones que dependen del valor concreto de los argumentos, como índices o alias de qubits, se comprueban al expandir una llamada.

## Operaciones y comprobaciones

| Forma `.mx` | Operación |
|---|---|
| `h(a);`, `x(a);`, `z(a);`, `s(a);` | Puerta de un qubit |
| `cx(a, b);`, `cz(a, b);` | Puerta de dos qubits distintos |
| `t(a);` | Puerta T, requiere el motor reducido |
| `show;`, `bytes;` | Inspección de representación y memoria lógica |
| `assert one;`, `assert ghz;` | Comprobaciones del motor compacto |
| `assert fidelity;`, `assert norm;` | Validación densa explícita del motor reducido |

La compilación valida la compatibilidad con el motor, los índices y que control y objetivo sean distintos después de sustituir los argumentos. No cambia el motor automáticamente. Las comprobaciones se conservan en su lugar dentro del programa; durante ejecución, un `FAIL` produce un código de salida distinto de cero. Se mantiene la tolerancia `3e-5` de XASM para fidelidad y norma.

## Límites de esta versión

Las siguientes cotas son protecciones del frontend experimental, no afirmaciones sobre capacidad universal de simulación:

| Recurso | Cota |
|---|---|
| Registro reducido | 1..12 qubits |
| Puertas T reducidas, después de expandir todos los bloques | 128 |
| Registro compacto del frontend `.mx` | 1..10,000 qubits |
| Archivo fuente | 1 MiB |
| Tokens | 100,000 |
| Profundidad sintáctica | 64 |
| Profundidad de llamadas | 32 |
| Trabajo de expansión | 100,000 pasos |
| Líneas XASM generadas | 10,000 |
| Salida XASM | 1 MiB |

El trabajo de expansión incluye visitas e iteraciones aunque no produzcan puertas. Así, los bucles enormes con cuerpos vacíos también se detienen. Las consultas y comprobaciones consumen presupuesto de salida.

Las cotas de simulación existentes siguen siendo relevantes: Compact Core representa la familia estabilizadora; el modo reducido puede crecer hasta `2^n` componentes y la validación densa necesita vectores de estado. La memoria lógica informada por el motor reducido no es la RAM total del proceso.

## Arquitectura y próximos pasos

La ruta actual es `fuente → tokens → árbol de sintaxis → comprobación de tipos y expansión → XASM → motor`. El compilador conserva la ubicación de origen y el orden de las operaciones. No aplica reordenamientos de puertas.

Las constantes, repeticiones y llamadas se resuelven durante compilación. Esta versión no incluye variables mutables, decisiones basadas en mediciones, ejecución paralela ni un lenguaje general de reglas sobre cualquier clase de estado. Su salida de ejecución sigue siendo secuencial, aunque la fuente permite describir y reutilizar estructuras.

Los siguientes avances deben justificarse con casos de uso y pruebas: interoperabilidad con Python/OpenQASM, mejores herramientas de edición, mediciones y control clásico compatibles con los motores, y representaciones de dependencias. Cada incorporación debe preservar los resultados de los programas existentes y medir por separado corrección, tiempo, memoria y facilidad de uso.
