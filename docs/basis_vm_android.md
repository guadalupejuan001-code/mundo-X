# Mundo X VM: primer prototipo ejecutable en Android

Esta prueba añade una máquina de 8 bits al proyecto. Permite editar un programa,
ejecutarlo y revisar registros, memoria, salida y una traza de instrucciones.
Incluye suma de 1 a 10, Fibonacci y recorrido de memoria.

## Qué se ejecuta

- Cuatro registros de datos: R0, R1, R2 y R3, de 8 bits sin signo.
- RAM: 16 posiciones de un byte, con direcciones de 0 a 15.
- Valores de 0 a 255. Las sumas y restas se envuelven módulo 256.
- Contador de programa, saltos, banderas Z (cero) y C (acarreo).
- Instrucciones MOV, ADD, SUB, XOR, CMP, LOAD, STORE, JMP, JZ, JNZ, OUT y HALT.

Los registros y la RAM de la máquina se almacenan en el tableau de **CompactCore**.
No hay una segunda matriz clásica que conserve los datos de esa máquina. La ALU
usa operaciones de bits X, CX y CCX restringida a estados de base. En esa CCX,
el simulador lee controles deterministas y aplica X al destino; no implementa
una Toffoli general sobre superposiciones. La suma propaga el acarreo entre bits.

El procesador anfitrión ejecuta el simulador y conserva el decodificador, el
contador de programa, las banderas y el control de ejecución. También ejecuta
un **intérprete de referencia separado**, que calcula con aritmética clásica y
se usa para comparar el estado después de cada instrucción. Esa referencia no
es el almacenamiento de la máquina que se está comprobando.

El estado permanece en la base computacional. Esta versión no aprovecha
superposición ni demuestra aceleración cuántica, ahorro de RAM o superioridad
frente a una CPU convencional. El tableau puede ocupar más que los 20 bytes de
registros y RAM visibles. Su utilidad aquí es comprobar una ruta de ejecución
con memoria, aritmética y control de flujo.

## Probar en el teléfono

1. Instalar `Mundo-X-VM-debug.apk` y abrir **Mundo X VM**.
2. Abrir **PROBAR MÁQUINA MUNDO X** en la primera pantalla.
3. Seleccionar **Suma**, pulsar **Ejecutar programa** y comprobar una salida de **55**.
4. Pulsar **Comprobar motor**. Copiar el informe para conservar el resultado real
   del teléfono, incluyendo su memoria PSS y el tiempo de ejecución.
5. Modificar el programa o cambiar a Fibonacci o Memoria y volver a ejecutar.

El APK de prueba usa `com.mundox.lab.vmtest`, de modo que puede instalarse junto
a Mundo X Lab. Requiere Android 8.0 o posterior y ARM64; incluye también x86_64
para pruebas en emulador. Es una compilación debug de desarrollo.

El editor tiene un presupuesto de instrucciones para detener bucles infinitos.
Un resultado LIMIT no significa que la operación haya terminado. PASS indica
que se llegó a HALT y que la comprobación de referencia coincidió. El tiempo
mostrado incluye la ejecución de la VM, la lectura de estados y la traza, y
excluye el pase de la referencia clásica; los bytes de
CompactCore son almacenamiento lógico del tableau y no equivalen a la RAM total
del proceso. Android PSS se presenta por separado.

## Programa mínimo

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

MOV copia un registro o un inmediato. ADD, SUB y XOR escriben en el primer
registro. CMP actualiza banderas como una resta sin cambiar el registro. Z se
actualiza en ADD, SUB, XOR y CMP. C representa acarreo en suma y ausencia de
préstamo en resta/CMP; XOR pone C a cero. MOV, LOAD y STORE conservan las banderas. Las direcciones
pueden ser literales (`[0]`) o registros (`[R1]`); fuera de 0–15 se informa un error.
Los comentarios empiezan por `#`. Esta versión admite texto ASCII; usa comentarios
sin tildes. El tamaño máximo es 16 KiB, con 512 instrucciones y 256 valores de
salida como máximo. La traza muestra hasta 64 pasos.

## Compilar y comprobar

Prueba nativa:

```sh
cmake -S . -B native-build -DCMAKE_BUILD_TYPE=Release
cmake --build native-build -j2
ctest --test-dir native-build --output-on-failure
./native-build/mundo_x_vm --self-test
./native-build/mundo_x_vm --example 0 > suma.mxvm
./native-build/mundo_x_vm suma.mxvm
```

APK, con JDK 17, Gradle 8.9 y las dependencias Android indicadas en el proyecto:

```sh
gradle --no-daemon -PvmTestBuild=true :app:assembleDebug
```

El workflow `basis-vm-apk.yml` construye este APK y comprueba instalación y
ejecución en un emulador Android. Las pruebas del teléfono son independientes:
el informe del emulador no mide el rendimiento de tu dispositivo.

## Alcance

**Todavía no arranca el sistema Android dentro de Mundo X.** Esta aplicación
corre sobre Android y permite probar el primer modelo de máquina del proyecto.
Arrancar Android como sistema invitado requiere mucho más: una arquitectura de
instrucciones compatible, memoria suficiente y sus protecciones, interrupciones,
dispositivos virtuales y un kernel Linux que pueda arrancar en esa plataforma.
