# Mundo X · Núcleo GHZ para Galaxy Watch4

Aplicación Wear OS independiente. Ejecuta **qubits simulados** directamente en
la CPU del reloj usando el código canónico `QuantumRuntime → InfiniteCore →
CompactCore`. No necesita una app de teléfono, red ni permisos de sensores.

## Uso

1. Instala `Mundo-X-GHZ-Reloj.apk` en el reloj y abre **Mundo X · GHZ**.
2. Pulsa **PREPARAR 3 QUBITS**. Los dos resultados Z tienen probabilidad 50 %.
3. **CAMBIAR** selecciona 3, 16, 64, 256 o 1000 para la siguiente preparación.
4. **FASE +90°** aplica S al qubit 0. El ciclo 0°, 90°, 180°, 270° cambia
   las coherencias X…X y YX…X, sin cambiar las probabilidades Z.
5. **MEDIR EN Z** proyecta el qubit 0 del estado simulado. Se comprueban las
   probabilidades de todos los qubits: todos dan el mismo bit y las dos
   coherencias anteriores pasan a cero. Repetir la medición conserva el bit.
6. **PREPARAR** construye otra preparación independiente. **REPOSO** libera
   las componentes y deja cero qubits materializados y cero bytes de tableau.

Desliza o gira el bisel para acceder a todos los controles y métricas. La
selección de tamaño se conserva. El estado se mantiene mientras viva el
proceso; después de que Android lo cierre se inicia en reposo. No se ejecuta
un servicio de fondo, bucle de animación ni registro de sensores.

## Memoria y límites

La app conserva el tableau actual del núcleo; no conserva un historial de
puertas ni construye un vector de 2^n amplitudes. El payload del tableau es
`n * (16 * ceil(n/64) + 1)` bytes: 51 bytes para 3 qubits y 257000 bytes para
1000. Las direcciones, metadatos, temporales, copias transaccionales y el
runtime Android consumen memoria adicional. La interfaz muestra por separado
el payload, los bytes lógicos de direcciones y el PSS del proceso.

Este es el almacenamiento compacto del backend general de estabilizadores;
no se afirma que sea la representación mínima posible de la familia GHZ.
El estado GHZ tiene una descripción matemática aún más corta, pero esta app
ejecuta las puertas y observaciones contra el núcleo existente. Reposo no
significa cero RAM de la aplicación ni enfriamiento físico de sus bits.

La versión cubre GHZ y cuatro fases Clifford. No incorpora todas las puertas
no Clifford de otros backends MIL del repositorio. Los presupuestos locales
son 1000 qubits activos, una componente máxima de 1000, 512 KiB de tableau y
1000 términos por observable. Las operaciones corren fuera del hilo de UI.

## Alcance científico

GHZ se prepara con H(0) y CX(0,q). La fase se aplica mediante S(0). X…X,
YX…X y Z(0)Z(n-1) se consultan al núcleo, no se sustituyen por una fórmula
del frontend. La medición utiliza el generador pseudoaleatorio del núcleo,
con una semilla entregada por `SecureRandom` de Android en cada preparación.

Es una simulación de mecánica cuántica en hardware clásico. No prepara
qubits físicos en el reloj, no mide entrelazamiento físico, no emite un campo
para entrelazar al usuario y no establece una conexión biológica. No hay
lecturas falsas de sensores ni valores que se presenten como evidencia física.

## Validación y compilación

`watch-ghz-android/tests/watch_ghz_core_test.cpp` contrasta los observables con
un oráculo denso independiente para 3–7 qubits y prueba fases, proyección,
correlación en cada dirección hasta 1000, repetición de medición, errores
atómicos y liberación. `NativeGhzCheck` ejecuta JNI real en el host. La prueba
Android pulsa los controles y comprueba el resultado del núcleo nativo.

El workflow `.github/workflows/watch-ghz.yml` compila Java 17 / Gradle 8.9 /
AGP 8.7.3 / SDK 35 / NDK 27.2.12479018, ejecuta lint, verifica firma e identidad
del APK, e incluye bibliotecas arm64-v8a y x86_64. La prueba UI se ejecuta en
Android API 30 con una pantalla de 450 × 450; no sustituye una prueba física
en Galaxy Watch4. Los resultados de cada ejecución están en el artefacto CI.

Para compilar: `gradle -p watch-ghz-android :app:assembleDebug :app:lintDebug`.
Paquete independiente: `com.mundox.ghzwatch`, versión 1.0.0. Puede coexistir
con WorldLink y BBY-Q. El APK debug permite instalación mediante ADB inalámbrico
en Wear OS; abrirlo en el teléfono no lo instala automáticamente en el reloj.
