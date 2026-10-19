# Experimentos Android de Mundo X

Este directorio recupera **Mundo X VM** y **Mundo X Ecos**, que ya existían en
las ramas propias `codex/basis-vm-android` y `codex/quantum-echo-android`, para
ejecutarlos dentro del nuevo paquete `com.mundox.cosmic`.

La procedencia exacta de cada archivo está en [PROVENANCE.json](PROVENANCE.json).
No se incluyen el proyecto COSMIC X PC, `pc-android`, ni la emulación de Windows.

## Integración

| Experimento | Activity original | Biblioteca |
| --- | --- | --- |
| Máquina de 8 bits | `com.mundox.lab.BasisVmActivity` | `mundox_basis` |
| Ecos | `com.mundox.echoes.MainActivity` | `mundox_echoes` |

El nuevo Gradle incorpora `android-experiments/java` como fuente y su CMake
incorpora este directorio. El adaptador `BasisVmNative` conserva las tres
operaciones JNI de la VM sin reemplazar el `NativeCore` moderno de XLab.
Ecos conserva su Activity y sus símbolos JNI originales.

Los dos experimentos compilan contra `native/include`, el núcleo canónico del
repositorio. Las extensiones de las ramas antiguas se integran en ese núcleo:
operaciones restringidas a estados de base para la VM; T inversa, observables
dispersos, presupuesto de componentes y cancelación para Ecos. Se mantienen
las optimizaciones actuales de Clifford/T, fases arbitrarias y transferencia
de estados producto. No hay una copia antigua paralela del núcleo.

## Qué se puede comprobar

La VM ejecuta programas de 8 bits con cuatro registros y 16 bytes de RAM en
CompactCore. Incluye suma de 1 a 10 (salida 55), Fibonacci y copia de memoria,
con intérprete clásico independiente que contrasta cada instrucción. Este
experimento ejecuta una máquina de estados de base, sin arrancar un sistema
operativo invitado.

Ecos calcula evolución, perturbación y reversión Clifford+T con una semilla
reproducible. El control omite la perturbación; para hasta 16 qubits, una
referencia independiente de amplitudes complejas contrasta señal y norma.
El presupuesto o la cancelación se informan como tales, sin publicar una
ejecución parcial como verificada.

Las pruebas originales `basis_vm`, `quantum_echo` y `sparse_observable` están
registradas en CTest y usan los mismos encabezados canónicos que el APK:

```sh
cmake -S . -B native-build -DCMAKE_BUILD_TYPE=Release
cmake --build native-build -j2
ctest --test-dir native-build --output-on-failure
```

El catálogo completo del nuevo APK se define en
`cosmic-android/app/src/main/java/com/mundox/cosmic/Experiments.java`.
También incorpora directamente los 12 experimentos de `app/src/main/java`,
y el motor gráfico actualizado de `graphics-android`; las pantallas se abren
dentro de la misma instalación.
