# Máquina de 8 bits: programas `.mxvm`

`mundo_x_vm` recupera la CLI original de Mundo X VM y usa el mismo
`basis_vm.hpp` que el experimento Android integrado. Su archivo fuente se
conserva sin cambios desde el commit
`9d095a81b3820f4c50fc48fecd9c7ff08bd22b40`.

## Compilar y ejecutar

Desde la raíz del repositorio, con CMake y un compilador C++17:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target mundo_x_vm
./build/mundo_x_vm --self-test
./build/mundo_x_vm --example 0 > suma.mxvm
./build/mundo_x_vm suma.mxvm 1000
```

`--example 0`, `1` y `2` generan los programas de suma, Fibonacci y memoria.
El programa de suma produce `55`. La ejecución devuelve un informe JSON con
estado, salida, registros, memoria y comprobación contra el intérprete de
referencia independiente. `--self-test` ejecuta las comprobaciones del motor.
La CLI también se compila con `-DBUILD_TESTING=OFF`; esa opción solo desactiva
los ejecutables y registros de pruebas.

Con el generador de Visual Studio, utilizar `--config Release` al compilar
y el ejecutable `build\Release\mundo_x_vm.exe`. Para guardar un ejemplo desde
Windows PowerShell, usar codificación ASCII:

```powershell
.\build\Release\mundo_x_vm.exe --example 0 | Set-Content -Encoding ascii suma.mxvm
.\build\Release\mundo_x_vm.exe suma.mxvm 1000
```

## Lenguaje y límites

La máquina tiene cuatro registros `R0`–`R3` de 8 bits y 16 posiciones de RAM.
Las operaciones aritméticas se reducen módulo 256. Admite `MOV`, `ADD`, `SUB`,
`XOR`, `CMP`, `LOAD`, `STORE`, `JMP`, `JZ`, `JNZ`, `OUT` y `HALT`:

```text
MOV R0, 0
MOV R1, 1
bucle:
ADD R0, R1
ADD R1, 1
CMP R1, 11
JNZ bucle
STORE [0], R0
LOAD R2, [0]
OUT R2
HALT
```

Los comentarios empiezan por `#`. El texto debe ser ASCII y puede ocupar
hasta 16 KiB, con un máximo de 512 instrucciones. Las direcciones de memoria
pueden ser literales (`[0]`) o registros (`[R1]`), siempre dentro de 0–15.
El segundo argumento fija un presupuesto entre 1 y 10.000 pasos; el valor
predeterminado es 1.000. La salida admite hasta 256 valores y la traza
conserva hasta 64 pasos.

`PASS` exige llegar a `HALT` y coincidir con la referencia después de las
instrucciones ejecutadas. Agotar el presupuesto produce `LIMIT` y un código
de salida distinto de cero. Los errores de entrada también terminan con
código distinto de cero.

El tableau de `CompactCore` guarda los bits de esta máquina en estados de
base computacional. La ejecución no utiliza superposiciones ni constituye
una demostración de aceleración cuántica. Este lenguaje propio de 8 bits
es un componente distinto del intérprete x86/x64 de `pc-android`.
