# Mundo X · Quantum Workstation (Android)

## Uso práctico

La APK `COSMIC X PC` ahora ofrece **dos entradas en el lanzador Android**:

- **Mundo X Quantum**: espacio de trabajo cuántico autónomo. No inicia la CPU x86 ni reserva la RAM virtual del PC.
- **COSMIC X PC**: emulador x86/x64 existente y su visor cuántico, sin alteraciones funcionales.

En **Mundo X Quantum** puedes:

1. Elegir **XI persistente** o **XASM con puertas T** para el editor.
2. Escribir o pegar un programa `.xi` o `.x` y pulsar **Ejecutar**.
3. Ejecutar comandos XI breves desde **Terminal**, sobre el mismo estado que el editor XI. XASM ejecuta circuitos independientes.
4. Abrir y guardar archivos `.xi` y `.x` a través del selector de documentos Android.
5. Consultar el estado del núcleo XI y la memoria sin medir implícitamente qubits.
6. Guardar una sesión en un archivo `.mqx` (JSON con versión, borradores XI/XASM, snapshot MXI y snapshot de Inverse Ether Memory). Los circuitos XASM no exportan aquí su estado final.
7. Reabrir esa sesión después de cerrar la aplicación; la restauración del núcleo XI y la memoria es transaccional.
8. Entrar a COSMIC X PC desde el propio espacio de trabajo.

## Arquitectura

```text
Android (QuantumDesktopActivity)
  ├── Editor XI y terminal ──> QuantumWorkstation JNI
  │                            └── QuantumRuntime
  │                                  ├── InfiniteCore / Ether
  │                                  └── Inverse Ether Memory
  ├── Editor XASM ────────────> run_xasm (Compact / Reduced Exact T)
  │                              (nuevo estado por ejecución)
  ├── Archivos .xi/.x ───────> Android Storage Access Framework
  ├── Sesiones .mqx ─────────> snapshots de XI + memoria + ambos borradores
  └── Acceso COSMIC X PC ───> MainActivity (independiente)
```

Cada instancia de Workstation posee una única sesión nativa. La ejecución XI opera sobre una copia transaccional; el estado visible no cambia si falla el programa o no cabe la salida de consola. Solo se serializan estados a petición explícita del usuario. Los documentos se escriben y leen en segundo plano, con límites de tamaño.

- Editor y programa JNI: máximo 1 MiB.
- Salida JNI: máximo 1 MiB; vista de consola retiene los últimos 100.000 caracteres.
- Archivo de sesión .mqx: máximo 32 MiB.
- Cada snapshot nativo: máximo 24 MiB para el transporte JNI; el propio núcleo impone límites internos más estrictos.
- El núcleo conserva sus presupuestos físicos y lógicos propios.
- Ningún archivo .xi permite ejecutar comandos del sistema anfitrión: solo la gramática XI del runtime.
- Android no necesita acceso a red ni permisos de almacenamiento general para elegir documentos.

## Qué es y qué no es

Es un **entorno operativo para computación cuántica por software** sobre la implementación de Mundo X, con programas, terminal, archivos y recuperación de estado. No es una QPU física ni un sistema operativo capaz de ejecutar binarios generales (Windows, Linux o aplicaciones x86) sobre estados cuánticos. COSMIC X PC permanece como subsistema separado.

La interfaz expone dos rutas separadas: **XI persistente** (Clifford, Ether e Inverse Memory) y **XASM por circuito**, incluido el backend Reduced de 1.000 qubits con puertas T. Las ejecuciones XASM empiezan con un estado nuevo y **no cambian el núcleo XI ni se guardan como estado computacional en el archivo `.mqx`**.

## Próximas funciones de producto

1. Gestor de proyectos, biblioteca de programas y flujo de importación/exportación de resultados.
2. API de aplicaciones para que herramientas del usuario invoquen el núcleo sin pasar por el editor.
3. Interfaz espacial Ether Render X como pantalla de la máquina, sin atribuirle cálculos que aún no ejecuta.
4. Compatibilidad futura de estados XI/XASM solo si se diseña y valida una conversión explícita.

La verificación automática de compilación del APK pertenece al workflow `.github/workflows/own-pc-android.yml`.
