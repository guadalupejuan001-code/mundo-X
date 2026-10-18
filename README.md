# Mundo X

**Plataforma de computación basada en representaciones compactas, simulación cuántica por software y reconstrucción de información mediante reglas.**

**Estado:** desarrollo activo · **Repositorio principal:** `main` · **Última revisión de esta documentación:** 18 de octubre de 2026.

**XLab 0.2 para Android:** [mesa de circuitos, pruebas interactivas, protocolos
de investigación y ensayos de memoria](docs/xlab_0_2.md). Interfaz nueva con
gráficos de resultados nativos y perfiles de 64 a 2048 MiB sujetos a la RAM
disponible. Se instala junto al laboratorio anterior.
[APK y validación: 12 pruebas nativas y 8 recorridos Android](results/xlab_0_2_2026-10-18.md).

> **Principio de diseño:** representar y transformar lo necesario; no materializar todos los estados, ramas u objetos cuando su comportamiento puede obtenerse a partir de estructuras, relaciones y reglas de evolución.

Mundo X **no nació como una app Android**. La aplicación móvil, el runtime de Windows y los experimentos gráficos sirven para probar la arquitectura en contextos distintos. El núcleo del proyecto es investigar **cómo representar información y calcular su evolución** sin depender, siempre, de una enumeración explícita de cada elemento posible.

[Origen declarado y recorrido cuántico](docs/origen_y_recorrido_cuantico.md):
el titular partió de cuestionar la representación de símbolos y cómo organizar
señales como lenguaje utilizable. El registro distingue ese propósito de cada
implementación, recoge el historial temprano y documenta una limitación
concreta del puente de bytes a modos.
La [revisión y validación de esta etapa](results/revision_origen_y_medicion_2026-10-18.md)
recoge los hallazgos y las pruebas de medición programable.

El software **funciona en los casos documentados**, está en desarrollo y no se presenta aquí como producto terminado. La rama actual contiene simuladores, un runtime programable, pruebas de circuitos, aplicaciones de demostración y una línea neuronal independiente.

**Qué significa «motor»:** [nombres, funciones y origen de los núcleos](results/engine_map_2026-10-18.md).
Mundo X es el proyecto; `CompactCore` y `ReducedExactTRepresentation` son sus
núcleos originales. `FactorizedCore` es un prototipo posterior. Cada examen
identifica el componente ejecutado.

**Corrección de interpretación:** el titular usó «computasis» como analogía de
homeostasis. El controlador experimental añadido después no fue una
especificación suya ni define la arquitectura original. **Su implementación,
comando, pruebas específicas y
supervisor han sido eliminados del código activo**. Los bancos de pruebas usan
los núcleos conservados. Las mediciones anteriores permanecen como registro
histórico y no describen un componente disponible.

Verificación de la retirada: compilación limpia, siete pruebas CTest,
18 comprobaciones del CLI y 52 casos en los supervisores, incluido un rechazo
de presupuesto esperado. Los seis headers de los núcleos conservados no cambian
durante esta retirada.

**Exámenes ampliados:** [núcleos originales, productos de 200 millones y costo del controlador](results/native_exams_2026-10-18.md).
Esta fase conserva el código de los núcleos; registra también los timeouts,
los fallos del criterio numérico anterior y las pasadas de recuperación vacías.

**Escala del 18 de octubre:** [almacenamiento plano y ejecución en flujo](results/scale_exams_2026-10-18.md).
Esta rama reduce las asignaciones por qubit independiente, conserva los circuitos
en flujo y añade exámenes hasta cien millones de qubits producto. El informe
separa RAM, amplitudes, consultas, criterios numéricos y límites del núcleo propio.

**Registro del 17 de octubre:** [exámenes de factorización](results/factorized_exams_2026-10-17.md).
FactorizedCore es un prototipo escrito en este trabajo para explorar
la idea y la arquitectura originales del titular de Mundo X. La implementación
experimental por factores independientes y bloques conserva exámenes
reproducibles; su ejecución nativa no depende de simuladores externos.
Pasó el oráculo independiente en los
casos examinados y conservó productos de diez millones de qubits con unos
2,02 GiB de RAM total, sin expansión global;
el informe registra también límites, falsos negativos de separación y costos.
El controlador incluido en aquel registro y su comando fueron retirados.

## Índice

- [Principio y arquitectura](#principio-y-arquitectura)
- [Componentes y responsabilidades](#componentes-y-responsabilidades)
- [Resultados medidos](#resultados-medidos)
- [Aplicaciones y plataformas](#aplicaciones-y-plataformas)
- [Cómo ejecutar las pruebas](#cómo-ejecutar-las-pruebas)
- [Estructura del repositorio](#estructura-del-repositorio)
- [Alcance, límites y próximos pasos](#alcance-límites-y-próximos-pasos)
- [Registros técnicos y autoría](#registros-técnicos-y-autoría)

---

## Principio y arquitectura

### El problema que aborda

Un simulador cuántico denso representa `n` qubits con **`2^n` amplitudes complejas**. Esa representación es útil y general para circuitos pequeños, pero su memoria crece exponencialmente.

Mundo X desarrolla y prueba **otras formas de representar y transformar estados**. Cuando se conoce una estructura explotable, el programa puede:

1. Conservar relaciones y generadores, en lugar de enumerar todas las amplitudes.
2. Actualizar esa estructura al aplicar operaciones.
3. Compartir información común entre estados relacionados.
4. Fusionar equivalencias durante la evolución, antes de producir copias innecesarias.
5. Reconstruir los resultados que se necesiten en la etapa de validación, visualización o consulta.

Esto **no significa que la CPU deje de usar bits o ejecutar instrucciones convencionales**. Significa que el diseño del algoritmo evita, en los casos adecuados, representar explícitamente todos los objetos o estados que podría producir. El enfoque del proyecto no se limita a recorrer un vector denso como única manera de computar.

### Vista general

~~~text
                           MUNDO X
             Representación y evolución de estados
                              |
          +-------------------+-------------------+
          |                   |                   |
   Cálculo cuántico       Aprendizaje        Aplicaciones
     por software        de estados          y validación
          |                   |                   |
    Dense / Quantum       Bridge X           Android Lab
    Core (referencia)        |                Windows x64
          |             Transformer X        Render GPU/3D
    Compact Core           (separado)         Red / relay
          |
    Clifford / tableau
          |
    Exact T / Reduced Exact T
          |
     Teleport / XASM / pruebas de equivalencia
~~~

**Aclaración de integración:** Compact Core y Reduced Exact T pertenecen al motor nativo C++. Transformer X es un módulo PyTorch separado: **no está conectado actualmente al simulador compacto**. Las escenas GPU son gráficos clásicos que pueden recibir parámetros del estado simulado; no emplean una GPU cuántica.

## Componentes y responsabilidades

### 1. Quantum Core y Dense Core — referencia matemática

**Archivos:** [`src/quantum_core.py`](src/quantum_core.py), [`native/include/mundo_x/dense_core.hpp`](native/include/mundo_x/dense_core.hpp).

Son simuladores de vectores de estado completos, útiles para verificar que las transformaciones del resto del sistema conservan el comportamiento esperado. Contienen primitivas para superposición, fase, interferencia, puertas como H/X/CNOT, entrelazamiento y otras operaciones según la implementación.

Su función principal dentro de Mundo X es ser la **referencia de corrección**, no la representación de escala que se pretende utilizar en producción.

### 2. Compact Core — núcleo de simulación compacta

**Archivo:** [`native/include/mundo_x/compact_core.hpp`](native/include/mundo_x/compact_core.hpp).

Motor C++17 que representa estados **Clifford/estabilizadores** mediante un *tableau* binario empaquetado. No almacena un vector completo de `2^n` amplitudes para esa familia.

Operaciones implementadas:

- Hadamard (`H`), fase (`S`), Pauli (`X`/`Z`).
- CNOT (`CX`) y CZ.
- Medición proyectiva `Z` y operaciones de validación de estados.
- Consulta de probabilidades Z y medición con muestreo Born seudoaleatorio reproducible.
- Construcción y comprobación de GHZ, estados cluster 2D y firmas de estabilizadores.

El almacenamiento del tableau escala aproximadamente con **`O(n²)` bits**, en vez de `O(2^n)` amplitudes complejas. Este resultado se refiere a **estados compatibles con estabilizadores**, no a todos los posibles estados cuánticos.

### 3. Exact T y Reduced Exact T — evolución no-Clifford

**Archivo:** [`native/include/mundo_x/exact_t_experiment.hpp`](native/include/mundo_x/exact_t_experiment.hpp).

El proyecto tiene dos representaciones que permiten estudiar circuitos con puerta no-Clifford `T`:

**A. Exact T, referencia por ramas.** Utiliza la identidad matemática:

~~~text
T = αI + βZ
α = (1 + exp(iπ/4)) / 2
β = (1 - exp(iπ/4)) / 2
~~~

Cada aplicación de `T` crea dos alternativas estabilizadoras ponderadas. Esta referencia preserva las ramas para estudiar corrección; su costo ingenuo crece como `2^T` en el número de puertas T.

**B. Reduced Exact T, representación durante la evolución.** No almacena un tableau independiente por cada rama ingenua. Mantiene:

~~~text
base estabilizadora compartida
+ etiquetas Pauli canónicas
+ coeficientes complejos
+ registro de operaciones
~~~

Las etiquetas equivalentes módulo el grupo estabilizador se identifican y **sus coeficientes se acumulan en el momento de operar**. Así se evita generar las `2^T` ramas completas en los circuitos probados.

La reconstrucción densa se utiliza **para validar al final los circuitos pequeños**, no como estado interno de evolución de la implementación reducida. La fidelidad numérica contra Dense Core fue cercana a 1 en los casos registrados.

**Límite importante:** evitar las `2^T` ramas no demuestra evitar el crecimiento `2^n` en el peor caso. Los circuitos probados de 8 qubits llegaron a 256 componentes; todavía hay un problema abierto de escalabilidad de coeficientes.

#### Estado *magic* y crecimiento de componentes

El comando `magic-witness` comprueba el estado `H → T` de un qubit: compara su geometría con los seis estados estabilizadores puros de un qubit. **Ese testigo concreto usa Dense Core**. Es una prueba de estado no-estabilizador, distinta de la validación de Reduced Exact T.

También hay trazas puerta por puerta para estudiar cuándo crece el número de componentes y cuándo aumenta su dificultad de factorización. Se observó crecimiento de soporte asociado a T en dos circuitos distintos; **no se confirmó** que Hadamard siempre incremente el rango interno.

### 4. Teletransportación de estado cuántico simulado

**Archivo:** [`native/include/mundo_x/teleport.hpp`](native/include/mundo_x/teleport.hpp).

Ejecuta el protocolo de teletransportación de **estado** usando un par de Bell, mediciones y correcciones clásicas. Verifica seis estados estabilizadores de un qubit frente a las cuatro ramas de medición, para **24 comprobaciones**.

No transporta materia ni transmite un qubit físico: es una simulación matemática del protocolo.

### 5. XASM — lenguaje mínimo para el runtime

**Archivo:** [`native/include/mundo_x/xasm.hpp`](native/include/mundo_x/xasm.hpp).

Formato de instrucciones `.x` con el que se ejecutan circuitos sobre Compact Core. Incluye `qubits`, `h`, `x`, `z`, `s`, `cx`, `show`, `bytes`, comprobaciones `assert` y ahora `probz`, `measure` y `seed`. **La puerta T reducida se ejecuta desde el runtime nativo o el laboratorio Android, no desde XASM.**

`probz q` consulta las probabilidades de medición Z; `measure q` obtiene un
resultado y proyecta el estado. `seed n` controla el muestreo seudoaleatorio
(semilla inicial 0). Los ejemplos [interferencia](examples/measurement_interference.x)
y [Bell](examples/measurement_bell.x) muestran fases y correlaciones.
Estas instrucciones también están en `practice`; aún no hay controles Android
para programarlas. Las consultas deterministas generales pueden requerir
eliminación del tableau: [costos y alcance](docs/origen_y_recorrido_cuantico.md#recorrido-programable-añadido).

~~~text
qubits 2
h 0
cx 0 1
show
bytes
assert ghz
~~~

Ejemplos reales: [`examples/hello.x`](examples/hello.x), [`examples/bell.x`](examples/bell.x), [`examples/ghz4.x`](examples/ghz4.x).

### 6. Bridge X y Transformer X — aprendizaje sobre modos

**Archivos:** [`src/bridge_x.py`](src/bridge_x.py), [`src/transformer_x.py`](src/transformer_x.py), [`experiments/train_file_v0_1.py`](experiments/train_file_v0_1.py).

**Bridge X** transforma fragmentos de bytes en hasta 256 modos identificables. Para cada modo activo representa actividad y posición mediante una pareja de componentes reales e imaginarias `z = [Re, Im]`, de la que pueden obtenerse amplitud y fase.

**Transformer X** es una red neuronal PyTorch real, con *TransformerEncoder*, atención y parámetros entrenables. Recibe identificadores de modo y estados complejos representados como dos números reales. Aprende a estimar una corrección:

~~~text
identidad de modo + estado (Re, Im)
       ↓
Transformer X
       ↓
corrección Δz
       ↓
estado estimado z + Δz
~~~

El script de entrenamiento usa ventanas consecutivas de un archivo e informa error de validación frente a una predicción identidad. **No es un modelo conversacional ni está integrado todavía a Compact Core**; el repositorio tampoco incluye un checkpoint entrenado listo para usarse como modelo general. Su utilidad práctica deberá medirse contra alternativas equivalentes.

## Resultados medidos

Las siguientes cifras proceden de ejecuciones registradas del proyecto y, donde corresponde, de capturas en hardware Android real. **No todas son benchmarks A/B controlados ni pruebas independientes de terceros.**

| Área / prueba | Resultado registrado | Alcance |
|---|---|---|
| GHZ 24 qubits, Compact Core | 408 bytes de estado | Runtime nativo |
| GHZ 10,000 qubits, Compact Core | ~23.97 MiB de estado | Estado estabilizador estructurado |
| GHZ 1,000 qubits, Android | 257,000 bytes; 8.909 ms | Captura en dispositivo |
| Cluster 2D 32 × 32, Android | 1,024 qubits; 263,168 bytes; 53.212 ms | Firma del cluster PASS |
| Teletransportación simulada | 24/24 PASS; 51 bytes de estado | Seis estados × cuatro ramas |
| Exact T, 4 qubits / 8 T | 256 ramas; fidelidad 1.000000000 | Referencia de ramas; también probado en Android |
| Reduced Exact T, 4 qubits / 12 T | 4,096 ramas teóricas → 16 componentes | Fidelidad 1.000000000 en CI |
| Reduced Exact T, 8 qubits / 64 T | `2^64` ramas teóricas **no materializadas** → 256 componentes | Fidelidad 1.000000000 en CI |
| GPU visual, Samsung A16 5G | 96,000 elementos a ~60–65 FPS | OpenGL ES, una llamada de dibujo por fotograma |

En la prueba `4 qubits / 12 T`, el registro reporta **737,562 bytes** para la representación ingenua explícita frente a **1,072 bytes de carga lógica** en la representación reducida. Son mediciones de estructuras diferentes dentro del programa; **la carga lógica no equivale a la RAM total del proceso**.

En la investigación posterior, una factorización secuencial de bajo rango **no** comprimió los coeficientes del caso de 8 qubits: el vector de 256 coeficientes complejos dio una estimación de 680 escalares para esa factorización. Se conserva ese resultado negativo porque identifica una limitación técnica real.

La [revisión del 17 de octubre](results/architecture_limits_2026-10-17.md) añade
un banco aislado de escala, comprobaciones numéricas independientes y controles
de entradas. La creación y evolución de Reduced Exact T mejoran aproximadamente 2–2,25× en
los tres circuitos comparados; el crecimiento exponencial de sus componentes
persiste. En esta revisión, su referencia y reconstrucción usan `DenseCore64`
(double), y el reporte declara esa precisión y sus bytes. `DenseCore` conserva
float32 para los benchmarks y resultados históricos. Los tiempos, presupuestos,
fallos y límites de cada comparación quedan documentados en el informe.

**Precisión sobre el renderizado:** los 608 bytes que aparece en las capturas `COMPACT → GPU` representan el **estado lógico explícito instrumentado**, no toda la memoria que emplea Android, los shaders o la GPU. La mejora de FPS registrada provino del traslado del renderizado Canvas a OpenGL ES, el procesamiento por lotes y la reconstrucción local; no se atribuye a una aceleración cuántica física.

Para los detalles, resultados y limitaciones de cada medición:

- [Estado funcional y validaciones](TESTED_FUNCTIONAL_STATUS_2026-10-14.md).
- [Resultados de Exact T, Reduced T, magic y trazas](results/exact_t_decomposition_2026-10-14.md).
- [Benchmark nativo: Compact frente a Dense](results/native_benchmark_2026-10-13.md).
- [Capturas y mediciones visuales Android](results/visual_android_samsung_a16_5g_2026-10-14.md).
- [Teletransportación cuántica simulada](results/compact_quantum_teleportation_2026-10-14.md).

## Aplicaciones y plataformas

Estas aplicaciones son **usos y bancos de prueba del proyecto**. No constituyen el propósito completo de Mundo X ni demuestran por sí mismas una ventaja cuántica.

### Android: Mundo X Lab

Código en [`app/`](app/). Se comunica con el motor C++ mediante **JNI**; la aplicación requiere OpenGL ES 3.0 para sus funciones gráficas.

La interfaz incluye:

- `HELLO X`, GHZ, cluster 2D, teletransportación de estado y pruebas Exact T / Reduced Exact T.
- Consola de práctica con operaciones sobre qubits simulados.
- Comparadores visuales `CLASSIC`, `GENERATIVE`, `COMPACT → VISUAL` y renderizador GPU.
- Una escena 3D procedural navegable; primero se exploró *ray marching* y después geometría rasterizada e instancing. La coherencia urbana todavía necesita desarrollo.
- Sincronización por eventos/estado mínimo con opciones `HOST` y `JOIN`.
- Un launcher HOME y un lanzador de aplicaciones con observación de RAM, batería y estado térmico.
- Una opción de preparación y apertura de Wild Rift sin root y sin modificar el juego.

Una demostración procedural temprana informó una APK de **~1.03 MiB**. Esa cifra corresponde a aquella compilación y **no es una promesa de tamaño para todas las versiones ni una ciudad AAA completa**.

### Gráficos reconstruidos desde estado compacto

~~~text
Compact Core (32 qubits)
   → métricas del estado y entradas del usuario
   → parámetros / uniforms de OpenGL ES
   → shaders en GPU
   → elementos visuales generados localmente
~~~

Esta ruta conecta funcionalmente la simulación con una salida visual. La GPU continúa siendo convencional; la escena no es un vector de amplitudes renderizado directamente.

### Red y sincronización

- [`tools/mundox_relay.py`](tools/mundox_relay.py): servidor TCP simple con salas; puerto predeterminado `7777`.
- `InternetSyncActivity`: pruebas de conectividad, cliente TCP y relay embebido.
- `SyncedGpuActivity`: botones HOST/JOIN, envío de eventos táctiles y reconstrucción local.

El objetivo es **transmitir cambios o eventos, no enviar una escena de miles de objetos completa**. La ruta cuenta con código y compilaciones; todavía falta una validación física completa y documentada entre dos dispositivos para afirmar fiabilidad extremo a extremo. **El protocolo actual no ofrece cifrado ni autenticación y no debe utilizarse para datos confidenciales ni exponerse sin protección.**

### Windows y Linux

El motor nativo está escrito en **C++17**, se construye con **CMake** y se valida mediante GitHub Actions en Linux y Windows x64. Su ejecutable `mundo_x` permite correr circuitos, tests de escalabilidad y experimentos de reducción sin utilizar Android.

## Cómo ejecutar las pruebas

### Motor nativo en Linux

Requisitos: compilador C++17 y CMake.

~~~bash
git clone https://github.com/guadalupejuan001-code/mundo-X.git
cd mundo-X
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure

./build/mundo_x hello
./build/mundo_x run examples/bell.x
./build/mundo_x run examples/measurement_interference.x
./build/mundo_x run examples/measurement_bell.x
./build/mundo_x bench 24 --dense
./build/mundo_x cluster 4 4 --dense
./build/mundo_x cluster 32 32
./build/mundo_x teleport
./build/mundo_x t-experiment 4 8
./build/mundo_x t-reduced-experiment 8 64
./build/mundo_x magic-witness
./build/mundo_x t-trace-variant 8 12
~~~

Los comandos devuelven salidas de comprobación; `PASS` significa que la condición implementada ha sido satisfecha, **no que se haya certificado universalmente el sistema**.

### Windows x64

Con MSVC, CMake y un entorno de compilación configurado:

~~~powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\mundo_x.exe hello
.\build\Release\mundo_x.exe teleport
.\build\Release\mundo_x.exe t-reduced-experiment 8 64
~~~

También puede recuperarse el ejecutable como **artefacto de una ejecución exitosa** del workflow [Build Mundo X Windows 10 x64](.github/workflows/windows-x64.yml).
El ZIP incluye `INICIAR.cmd`, instrucciones y ejemplos; extrae todo y abre ese
archivo para ver las demostraciones y entrar en práctica. El ejecutable x64
enlaza estáticamente el runtime C++ de MSVC. No requiere reinstalar Windows ni
instalar Python.

### Android

El workflow [Build Mundo X Lab APK](.github/workflows/android-apk.yml) compila las pruebas nativas y el APK de depuración. En GitHub: **Actions → Build Mundo X Lab APK → una ejecución exitosa → Artifacts → Mundo-X-Lab-APK**.

Para construir localmente se necesitan el Android SDK, NDK, CMake, Java 17 y Gradle 8.9; las versiones concretas están definidas en el workflow. No se incluye `gradlew`, por lo que el comando local presupone Gradle instalado:

~~~bash
gradle --no-daemon :app:assembleDebug
~~~

La instalación y las mediciones sobre dispositivo físico requieren una ejecución aparte: **compilar no equivale a haber medido rendimiento real**.

### Python: referencia y Transformer X

Requisitos: Python 3.10+, PyTorch y, para tests Python, pytest.

~~~bash
python -m pip install -r requirements.txt pytest
python hello_mundo_x.py
python -m pytest tests/test_quantum_core.py
python src/transformer_x.py
python experiments/train_file_v0_1.py ruta/a/un_archivo --epochs 20
~~~

El entrenamiento utiliza el contenido binario del archivo que se le indique. Conviene utilizar datos propios o autorizados. El entrenamiento guarda un checkpoint local `transformer_x_v0_1.pt`: **ese archivo no viene publicado en el repositorio**.

### Relay local

~~~bash
python tools/mundox_relay.py
~~~

Arranca un relay de pruebas que escucha en el puerto `7777`. Úsalo solo en una red de confianza o un entorno aislado.

## Estructura del repositorio

~~~text
mundo-X/
├── native/
│   ├── include/mundo_x/
│   │   ├── dense_core.hpp            # statevector de referencia
│   │   ├── compact_core.hpp          # estados estabilizadores compactos
│   │   ├── exact_t_experiment.hpp    # T, reducción y diagnósticos
│   │   ├── teleport.hpp              # teletransportación simulada
│   │   └── xasm.hpp                  # intérprete .x
│   └── src/main.cpp                  # ejecutable y comandos
├── src/
│   ├── quantum_core.py               # referencia PyTorch
│   ├── bridge_x.py                   # codificador de modos
│   └── transformer_x.py              # modelo neuronal
├── experiments/                      # scripts y entrenamiento
├── tests/                            # pruebas Python
├── examples/                         # programas XASM
├── app/                              # Android Lab: JNI, GPU, red, UI
├── tools/mundox_relay.py             # relay TCP
├── results/                           # resultados históricos
├── docs/                              # explicaciones técnicas
├── .github/workflows/                 # builds de Android y Windows
├── CMakeLists.txt
└── README.md
~~~

## Alcance, límites y próximos pasos

### Qué está implementado y validado

- Simulación compacta de circuitos Clifford/estabilizadores dentro de los casos probados.
- Ejecución de experimentos no-Clifford con puerta T y comparación de fidelidad frente a una referencia densa pequeña.
- Reducción **durante** la evolución de componentes equivalentes para circuitos Clifford+T definidos.
- Protocolo de teletransportación de estados simulados, pruebas GHZ/cluster y un intérprete XASM.
- Compilaciones Android y Windows, demos gráficas e integración de un estado compacto con GPU.

### Qué no se ha demostrado todavía

- Simulación eficiente de **todos** los estados cuánticos arbitrarios.
- Eliminación general de la dependencia exponencial en el número de qubits.
- Superioridad global frente a todos los simuladores cuánticos existentes.
- Ventaja de **hardware cuántico**, uso de qubits físicos o una teoría física nueva.
- Aceleración gráfica causada específicamente por efectos cuánticos.
- Fiabilidad multi-dispositivo en red pública o un motor 3D de calidad comercial.
- Una mejora medible de Compact Core gracias a Transformer X.

### Próximas comprobaciones propuestas

1. Crear circuitos Clifford+T adicionales, aleatorios y adversos; comparar resultados, tiempos y memoria con referencias independientes.
2. Medir memoria **total del proceso**, carga lógica, número de componentes y costo de evolución por separado.
3. Estudiar la estructura y el rango de los coeficientes cuando aumenta el número de qubits, sin asumir que siempre podrán factorizarse con bajo rango.
4. Completar pruebas de dos dispositivos, corregir límites del generador urbano y seguir midiendo en hardware limitado.
5. Entrenar y evaluar Transformer X con métricas reproducibles **antes** de decidir si debe integrarse al núcleo.
6. Ampliar tests automatizados y detectar fallos con códigos de salida adecuados.

La conclusión de cada prueba se conserva aunque el resultado sea negativo. Mundo X es **un proyecto de ingeniería y cálculo en evolución**: los componentes ya implementados no pierden su funcionalidad porque queden preguntas abiertas.

## Registros técnicos y autoría

- [Estado funcional más detallado — 2026-10-14](TESTED_FUNCTIONAL_STATUS_2026-10-14.md)
- [Estado histórico preservado](PROJECT_STATUS_2026-10-14.md)
- [Quantum Core v0.1 — fundamentos](docs/quantum_core_v0_1.md)
- [Resultados cuánticos no-Clifford](results/exact_t_decomposition_2026-10-14.md)
- [Historial de commits](../../commits/main)
- [Compilaciones y resultados de CI](../../actions)

**Mundo X es un proyecto concebido y dirigido por el titular del repositorio.** El historial Git documenta el desarrollo y las revisiones del código y de sus resultados.

**Estado de distribución:** el código está visible en este repositorio, pero la presencia pública no significa que el proyecto se haya lanzado como producto terminado. En la raíz actual no se incluye un archivo `LICENSE`; no se declara aquí una licencia de reutilización ni se conceden permisos adicionales mediante este README.

---

**Mundo X — representar, transformar, verificar.**
