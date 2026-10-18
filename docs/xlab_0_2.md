# XLab 0.2 — laboratorio interactivo Android

XLab utiliza el Compact Core de Mundo X para ejecutar circuitos y protocolos
Clifford en la CPU del dispositivo. La interfaz dibuja circuitos y gráficos a
partir de resultados del cálculo nativo.

## Mesa

- Presets de Bell, interferencia H–Z–H, GHZ y cluster lineal.
- Editor con H, X, Z, S, CX, CZ y medición Z; hasta 8 qubits y 128 operaciones.
- Recorrido por pasos, selección de cables y consulta de probabilidades.
- Histogramas de 128 a 4096 lecturas, con semilla reproducible.
- Generadores y JSON del estado calculado para inspección y exportación.

Los bits del histograma se muestran como `q0 q1 …`: q0 es el de la izquierda.
Cada lectura muestrea una copia del estado actual. Si el programa ya contiene
una medición, las lecturas usan ese estado condicionado; no vuelven a elegir
una nueva rama del programa completo.

## Pruebas e investigación

Las pruebas piden una predicción y la contrastan con el núcleo: interferencia,
repetición de mediciones de Bell y estados con igual probabilidad Z pero fase
distinta. El contador de aciertos se guarda localmente.

Tres protocolos permiten variar datos y examinar el circuito aplicado:

| Protocolo | Control | Comprobación |
| --- | --- | --- |
| Codificación superdensa | Mensaje de dos bits | Decodifica el mensaje mediante el recurso Bell. |
| Bernstein–Vazirani | Secreto de cuatro bits | Recupera el secreto con una invocación lógica del oráculo XOR, descompuesto en CX. |
| Código de repetición de tres qubits | Sin error, X en q0, q1 o q2 | Mide paridades, elige la corrección por el síndrome y recupera los datos de la entrada lógica `1`. |

Son implementaciones de protocolos conocidos. El último ensayo estudia un
único error X y una entrada concreta; no comprueba corrección general de fase,
ruido arbitrario o tolerancia a fallos. La consulta lógica de BV se simula con
puertas clásicamente calculadas; no acredita ventaja de hardware cuántico.

## Carga y RAM

Los perfiles van de **64 a 2048 MiB de presupuesto lógico**. El usuario puede
ajustar los qubits al presupuesto o introducir una cantidad menor. Se trabaja
con un tableau real GHZ o cluster lineal, que se libera al terminar.

Antes de iniciar se comprueban el tamaño exacto del tableau y la información
de Android. El presupuesto permitido es el mínimo entre 2048 MiB, 30 % de la
RAM total y 50 % de la RAM disponible. Android `lowMemory` bloquea el inicio.
La carga se ejecuta fuera del hilo de interfaz y permite cancelación cooperativa.
La reserva inicial debe terminar o fallar antes de responder a la cancelación.

Un teléfono con 2 GB totales también necesita memoria para Android y las demás
aplicaciones. El perfil de 2048 MiB requiere un equipo con suficiente RAM libre.
La fórmula es una política preventiva, no una garantía contra cambios externos
de presión de memoria.

La pantalla distingue bytes lógicos del estado ensayado, PSS real del proceso,
RAM disponible, puertas completadas, tiempo y generadores comprobados. La
validación de carga inspecciona como máximo 17 generadores: es parcial.

## Bitácora e instalación

Se conservan los últimos veinte ensayos en el dispositivo. Pueden compartirse
como texto o copiarse como JSON. XLab no incorpora un servicio de chat ni envía
automáticamente esos resultados.

El APK requiere Android 8.0 o posterior y admite ARM64 y x86-64. El identificador
`com.mundox.lab.xlab` permite instalarlo junto al laboratorio anterior, cuyo
identificador era `com.mundox.lab`. El botón «Anterior» abre las herramientas
anteriores incluidas en este paquete.

Se compila una versión debug. El workflow Android construye el APK, ejecuta las
pruebas nativas y arranca un emulador Android 15 x86-64 con 2048 MiB para las
pruebas de interfaz y las capturas. Esa configuración no sustituye una medición
de rendimiento en un teléfono físico.
