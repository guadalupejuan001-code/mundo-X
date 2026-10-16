# Mundo X Ecos — experimento Android independiente

Esta aplicación ejecuta un experimento reproducible de eco cuántico con el
motor `ReducedExactTRepresentation` de Mundo X. El paquete Android es
`com.mundox.echoes`; se instala junto a Mundo X Lab o Mundo X VM y abre
directamente el experimento. Funciona sin conexión ni permisos de red.

## Qué calcula

Se parte de `|0…0⟩` en una cuadrícula de qubits. Un circuito `U` de puertas
H, S, T, CX y CZ mezcla los qubits y acopla vecinos. La semilla, dimensiones,
profundidad y versión del generador identifican el circuito.

La perturbación local es `B = X` en el sitio elegido y la sonda es
`M = Z` en el sitio elegido. Los índices empiezan en cero, recorriendo cada
fila de izquierda a derecha. Se define `B(t) = U† B U`.

- **Una vuelta:** aplicar U, X, U† y medir la esperanza de Z en la sonda.
- **Dos vueltas:** aplicar U, X, U†, Z en la sonda, U, X, U† y medir Z.
- **Control:** repetir el mismo procedimiento omitiendo las perturbaciones X.
  Debe recuperar una señal Z igual a +1 y norma igual a 1.

La señal es una esperanza matemática entre −1 y +1, no una única lectura
aleatoria ni una medición de un procesador cuántico físico. Puede ser positiva,
negativa o cero. Su valor depende del circuito; ninguna de esas tres situaciones
por sí sola significa ventaja computacional. La reversión usa puertas inversas
reales, incluida T†, y el orden inverso de las operaciones.

## Cómo usa Mundo X

El estado se representa como una suma de operadores de Pauli aplicados a una
base estabilizadora compartida. Se combinan componentes equivalentes sin
descartar amplitudes pequeñas. La norma y las esperanzas de Pauli se calculan
directamente en esa representación, sin reconstruir un vector de 2ⁿ amplitudes.
Los coeficientes son números complejos de doble precisión: «exacto» indica que
no se trunca la representación, no que desaparezca el redondeo numérico.

La cantidad de componentes puede crecer exponencialmente. El presupuesto de
componentes es una protección elegible para el celular; si se alcanza, se
informa **LÍMITE**, sin presentar una ejecución incompleta como un resultado
verificado. Se puede cancelar una ejecución. Esta versión admite hasta 63
qubits y profundidad 30; esos límites de la aplicación no son pruebas de
capacidad universal ni criterios para comparar máquinas.

## Verificación y comparación

Para hasta 16 qubits se ofrece un segundo simulador independiente, un vector
de estado complejo de doble precisión con actualizaciones directas de amplitud.
Calcula el mismo circuito, control y observable. No utiliza el reconstructor
`DenseCore` de Mundo X. La aplicación distingue:

- **VERIFICADO:** experimento y control completos, normas válidas y acuerdo
  con la referencia dentro de la tolerancia registrada.
- **COMPLETADO / SIN REFERENCIA:** ejecución terminada y control correcto,
  pero sin contraste independiente de la señal.
- **ERROR, LÍMITE o CANCELADO:** no se debe interpretar como una prueba superada.

Los tiempos de cada motor incluyen inicialización, evolución y lectura. El
control tiene tiempos separados. Son mediciones de una ejecución en ese
dispositivo, no un benchmark estadístico contra todos los simuladores. La carga
del teléfono, compilación, calentamiento y orden de ejecución influyen.

La memoria lógica del motor cuenta su contenido representado, no todo el
consumo del proceso: mapas, capacidades reservadas, asignador y temporales
añaden costes. La PSS de Android mostrada por la interfaz corresponde a toda
la aplicación y a un instante de muestreo, no a un pico aislado por motor.

El concepto de evolución, perturbación y reversión está relacionado con
[Quantum Echoes](https://research.google/blog/a-verifiable-quantum-advantage/),
pero este circuito Clifford+T no reproduce el experimento ni el benchmark de
Google. Superar aquí al vector denso solo demuestra una diferencia en este
caso, no una ventaja sobre Stim, Qulacs o los mejores métodos clásicos.

## Compilar y ejecutar

Requisitos: Java 17, Gradle 8.9, SDK Android 35, Build Tools 35.0.0,
NDK 27.2.12479018 y CMake 3.22.1.

```sh
gradle --no-daemon :echoes:assembleDebug
adb install -r echoes/build/outputs/apk/debug/echoes-debug.apk
adb shell am start -n com.mundox.echoes/.MainActivity
```

El APK incluye arm64-v8a para teléfonos Android de 64 bits y x86_64 para el
emulador. Requiere Android 8 o posterior. Es una compilación de prueba firmada
con una clave de depuración, no una publicación en Play Store.

La acción `quantum-echo-apk.yml` ejecuta pruebas numéricas nativas, compila,
comprueba paquete/firma/alineación de bibliotecas y abre la aplicación en un
emulador Android 15. Ejecuta el experimento y las comprobaciones internas;
conserva logs, capturas y un informe junto al APK y su SHA-256.

```sh
g++ -std=c++17 -O2 -Inative/include native/tests/sparse_observable_test.cpp -o /tmp/sparse-observable-test
/tmp/sparse-observable-test
g++ -std=c++17 -O2 -Inative/include native/tests/quantum_echo_test.cpp -o /tmp/quantum-echo-test
/tmp/quantum-echo-test
```

En el teléfono, pulsa **Ejecutar eco** y después **Comprobar simulación**.
Copia o comparte el informe para conservar parámetros, señales, errores y
tiempos de tu dispositivo. Compartir abre el selector de Android; la app no
envía los resultados automáticamente.
