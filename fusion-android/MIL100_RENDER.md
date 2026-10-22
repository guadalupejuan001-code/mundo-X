# Mundo X Fusion 3D 1.4 — MIL 100q × Ether Render X

## Qué hace la actualización

El motor original EtherRenderer tiene **64 cuerpos como límite por pasada**, porque utiliza una máscara espacial de 64 bits. Esa implementación se preserva sin modificaciones.

La APK 1.4 añade una pantalla interactiva que tiene **100 qubits lógicos reales del simulador MIL Reduced** y **100 objetos tridimensionales**. Se utiliza la técnica de **dos pasadas independientes de 50 cuerpos**, proyectadas con la misma cámara, seguidas de una composición por profundidad de cada píxel. Si los rayos llegan a un objeto en ambas pasadas, gana el que esté más cerca del observador.

La ampliación no crea un vector de 2^100 amplitudes ni introduce un motor gráfico ajeno. La imagen utiliza el trazador de rayos propio de Mundo X, pero está computada por la CPU clásica.

## Arquitectura del estado cuántico

El estado simulado es un GHZ de 100 qubits:

    |GHZ100(θ)> = (|0>^⊗100 + exp(iθ)|1>^⊗100) / sqrt(2)

Preparación real en ReducedExactTRepresentation:
- Hadamard sobre q0.
- 99 CNOT desde q0 hacia q1...q99.
- Fase arbitraria en q99, con posibilidad de cambiarla de forma reversible desde la interfaz.

Observables calculados y validados por MIL usando máscaras Pauli multilabra:
- Norma del registro: 1.
- <Z0> = 0.
- <Z0·Z99> = 1, una correlación que atraviesa la palabra de 64 bits.
- <X0·X1·...·X99> = cos(θ).

Las cien esferas representan los índices q0–q99 del registro y su iluminación está ligada al mismo observable **global GHZ** de 100 factores. No se afirma que sus ubicaciones en el espacio sean posiciones cuánticas de partículas, ni que el brillo represente cien amplitudes independientes.

Ejemplo: θ=0 produce coherencia +1, θ=π/4 produce 0,70710678 (fase de tamaño T), y θ=π/2 produce 0. Volver a θ=0 recupera el mismo framebuffer con la cámara fija. La geometría no cambia cuando cambia únicamente la fase.

## Ingeniería del render

    MIL Reduced GHZ100 → coherencia global <X^100>
                |
             2 bridges de materiales MIL ↔ Ether
                |
          +-------------+--------------+
          |                            |
       Render A                    Render B
       50 objetos                  50 objetos
       q0–q49                      q50–q99
          |                            |
       color+depth                  color+depth
          +------------+---------------+
                       |
                 nearest-depth
                       |
                100 objetos 3D

Cada pasada tiene sus propios IDs locales 0..49. El compositor añade el desplazamiento de 50 al ID del segundo framebuffer. Los objetos se distribuyen en una esfera Fibonacci reproducible y la cámara puede girar o acercarse.

Los materiales se recalculan únicamente cuando cambia el estado del registro, no por píxel ni por frame de cámara. El registro cuántico y sus dos renderizadores son objetos nativos persistentes, en un hilo propio serializado para evitar liberación concurrente.

## Verificaciones

1. Mil100EtherScene construye el GHZ100 y exige norma unitaria y <Z0Z99>=1.
2. El cambio a fase π/4 se contrasta con cos(π/4), no con números predeterminados.
3. Se comprueba que el framebuffer realmente cambia con la fase, que no cambia la máscara geométrica y que la fase inversa recupera exactamente el hash original.
4. Los dos renders Ether se verifican contra sus oráculos exhaustivos existentes.
5. Un trazador de rayos independiente evalúa los 100 objetos y verifica el cuerpo visible y su profundidad por píxel en cientos de rayos.
6. La prueba Android ejecuta la vista, lee los observables del estado real y confirma que el slider modifica la coherencia y la imagen.

Los resultados incluyen tiempo de lectura de observables, tiempo de raytracing, memoria lógica del estado y número de componentes de MIL. Las cifras corresponden al dispositivo que ejecute la prueba.

## Límites explícitos

- **100 qubits lógicos no son 100 qubits de hardware físico.** El ordenador utiliza software convencional.
- **100 qubits de render no son 100 qubits de Hamiltoniano nuclear**: la evolución radial actual usa 8 qubits para 256 posiciones.
- **100 objetos visibles no son 100 partículas de la reacción D–T**. El visor GHZ es una exploración separada de la arquitectura cuántica.
- Un GHZ de 100 qubits con unas pocas fases no-Clifford es un estado estructurado y eficiente; esto NO demuestra que cualquier estado arbitrario de 100 qubits pueda representarse con la misma RAM.
- **MIL admite hasta 1.000 qubits lógicos** en su perfil existente, pero esta pantalla visualiza exactamente 100. No se atribuyen 1.000 objetos o un Hamiltoniano nuclear de 1.000 qubits a esta actualización.
- Renderizar dos pasadas puede ser más lento que una. El avance verificado es superar **el límite de 64 cuerpos visibles** sin perder la verificación geométrica; todavía no hay una ganancia de FPS demostrada.

## Archivos y pruebas

- Backend cuántico y composición 3D: fusion-android/native/mil100_ether_scene.hpp
- JNI Android: fusion-android/app/src/main/cpp/mil100_ether_jni.cpp
- Vista táctil: fusion-android/app/src/main/java/com/mundox/fusion3d/Mil100EtherView.java
- Contrato del visor: fusion-android/app/src/main/java/com/mundox/fusion3d/FusionActivity.java
- Prueba nativa independiente: fusion-android/tests/mil100_ether_render.cpp
- CI específica: .github/workflows/mil100-ether-render.yml
- CI con instalación en Android y conservación del render nuclear: .github/workflows/fusion-standalone-android.yml

Sin permisos de Internet ni control de equipos físicos. Esta demostración no fabrica elementos ni predice nuevos estados de la materia.
