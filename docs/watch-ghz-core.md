# Mundo X · Núcleo GHZ para Galaxy Watch4

Aplicación Wear OS independiente. Ejecuta **qubits simulados** directamente en
la CPU del reloj usando el código canónico `QuantumRuntime → InfiniteCore →
CompactCore`. No necesita una app de teléfono ni red. Desde la versión 1.1.0
incluye una prueba de hardware independiente del estado simulado, con el
permiso Android `VIBRATE`. No solicita permisos biométricos.

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
un servicio de fondo ni un bucle de animación. Los sensores se registran solo
durante la prueba explícita de hardware descrita abajo.

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
Paquete independiente: `com.mundox.ghzwatch`, versión 1.1.0. Puede coexistir
con WorldLink y BBY-Q. El APK debug permite instalación mediante ADB inalámbrico
en Wear OS; abrirlo en el teléfono no lo instala automáticamente en el reloj.

## Hardware: primer experimento compartido

Abre **HARDWARE · PRUEBA REAL** desde la pantalla GHZ. El inventario consulta
el modelo, Android, el vibrador y los sensores que expone ese dispositivo;
no presupone que todos los Galaxy Watch tengan los mismos sensores o APIs.
La prueba se habilita si hay acelerómetro, magnetómetro y vibrador.

1. Deja el reloj apoyado e inmóvil sobre una superficie, fuera del cargador.
2. Pulsa **INICIAR · 13 SEGUNDOS** y no toques el reloj durante el registro.
3. En cuatro intervalos separados se solicitan dos pulsos de 200 ms y se
   ejecutan dos controles sin vibración, en orden aleatorio balanceado.
4. Anota los cuatro valores de ΔB y de variación del acelerómetro. Repite en
   la misma posición para comparar la variabilidad entre sesiones.

La captura solicita 50 Hz sin batching; la frecuencia efectiva depende del
dispositivo y se comprueba con los timestamps. El magnetómetro sin calibración
de hierro duro tiene preferencia; si no existe, se usa el calibrado y queda
identificado. Se guardan todos los valores de cada evento, incluido el bias
estimado si Android lo entrega, su precisión declarada y su tiempo de llegada.

Cada orden usa `elapsedRealtimeNanos()`, la misma base temporal de SensorEvent.
Se comparan ventanas de 700 ms: de −750 a −50 ms y de 0 a +700 ms respecto
al instante de solicitar el pulso o ejecutar el control. Se exige al menos
cuatro timestamps distintos por ventana, cobertura de sus extremos dentro de
200 ms y ningún hueco mayor de 250 ms. Si falla, se muestra **muestras
insuficientes**, sin reemplazarlas por ceros. Un retraso de planificación mayor
de 250 ms interrumpe el experimento y conserva el estado parcial.

ΔB es la norma de la diferencia entre los vectores medios magnéticos (µT).
El RMS de aceleración es la desviación vectorial respecto al promedio de la
propia ventana (m/s²). No es una medición de la frecuencia mecánica del motor;
una señal rápida puede sufrir aliasing a esta tasa. Los tiempos de orden no
equivalen a medir el comienzo físico de la vibración. Android puede suprimir
un pulso por sus políticas aunque la llamada no lance una excepción.

El objetivo es comprobar si una orden del actuador se acompaña de una respuesta
repetible de los sensores. Un cambio magnético no separa por sí mismo el
movimiento del reloj, interferencias del motor y variaciones del entorno.
Cuatro intervalos son una exploración, no una estimación de significación ni
una demostración de causalidad, superposición física o entrelazamiento. No se
usan los números GHZ para producir o interpretar estas lecturas.

**DETENER**, salir de la pantalla o perder el primer plano cancela los pulsos
pendientes y desregistra ambos sensores. No hay servicio en segundo plano.
Se conserva como máximo una sesión en `files/hardware-latest.json`, escrita
de forma atómica. Repetir reemplaza el registro anterior. Su contenido incluye
el inventario completo, órdenes, lecturas originales y comparaciones. No se
sube a ninguna red. Puedes extraer cada sesión antes de repetirla mediante
la misma conexión ADB que utilizaste para instalar:

```sh
adb exec-out run-as com.mundox.ghzwatch cat files/hardware-latest.json > hardware-latest.json
```

La estadística tiene comprobaciones con fixtures sintéticos independientes.
La prueba Android verifica inventario, captura completa y archivo, cancelación
manual y cancelación al salir. Las lecturas de emulador prueban software y no
constituyen evidencia del Watch4 físico. Ese paso lo realiza el usuario con
su reloj, y sus datos determinan el siguiente cambio del proyecto.

El APK usa firma de desarrollo. Si una versión anterior fue generada con otra
clave y Android muestra `INSTALL_FAILED_UPDATE_INCOMPATIBLE`, retira la app
anterior antes de instalar esta; eso elimina su estado y archivos locales.

Fuentes oficiales de las interfaces utilizadas:

- [Android: sensores y capacidades](https://developer.android.com/develop/sensors-and-location/sensors/sensors_overview)
- [Android: magnetómetro calibrado y sin calibración](https://developer.android.com/develop/sensors-and-location/sensors/sensors_position)
- [Android: pulsos VibrationEffect](https://developer.android.com/reference/android/os/VibrationEffect)
- [Android: tiempos de SensorEvent](https://developer.android.com/reference/android/hardware/SensorEvent#timestamp)

## Versión 1.2: mando para Campo 4D

Incluye un APK compañero para el teléfono y un mando en el reloj. Consulta [instalación, controles y límites](watch-map-control.md). GHZ sigue siendo una simulación; los nuevos controles editan el mapa virtual.
