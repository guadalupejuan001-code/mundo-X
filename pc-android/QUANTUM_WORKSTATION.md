# Mundo X · Quantum Workstation (Android)

## Uso práctico

La APK `COSMIC X PC` ahora ofrece **dos entradas en el lanzador Android**:

- **Mundo X Quantum**: espacio de trabajo cuántico autónomo. No inicia la CPU x86 ni reserva la RAM virtual del PC.
- **COSMIC X PC**: emulador x86/x64 existente y su visor cuántico, sin alteraciones funcionales.

En **Mundo X Quantum** puedes:

1. Escribir o pegar un programa XI y pulsar **Ejecutar programa**.
2. Ejecutar comandos breves desde **Terminal**, sobre el mismo estado que el programa.
3. Abrir y guardar archivos `.xi` a través del selector de documentos Android.
4. Consultar el estado del núcleo y la memoria sin medir implícitamente qubits.
5. Guardar una sesión en un archivo `.mqx` (JSON con versión, programa, snapshot MXI y snapshot de Inverse Ether Memory).
6. Reabrir esa sesión después de cerrar la aplicación; la restauración de núcleo y memoria es transaccional.
7. Entrar a COSMIC X PC desde el propio espacio de trabajo.

## Arquitectura

```text
Android (QuantumDesktopActivity)
  ├── Editor XI y terminal ──> QuantumWorkstation JNI
  │                            └── QuantumRuntime
  │                                  ├── InfiniteCore / Ether
  │                                  └── Inverse Ether Memory
  ├── Archivos .xi ──────────> Android Storage Access Framework
  ├── Sesiones .mqx ─────────> core + memory snapshots + programa
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

La interfaz inicial usa el runtime XI con estado persistente (Clifford, Ether e Inverse Memory). El backend XASM de 1.000 qubits con soporte no-Clifford existe en el repositorio, pero **aún no está conectado a esta interfaz**. Debe integrarse en una capa de ejecución explícita sin simular una continuidad de estado que el backend actual no ofrece.

## Próximas funciones de producto

1. Selector de modo `XI persistente` / `XASM universal por programa`.
2. Gestor de proyectos, biblioteca de programas y flujo de importación/exportación de resultados.
3. API de aplicaciones para que herramientas del usuario invoquen el núcleo sin pasar por el editor.
4. Interfaz espacial Ether Render X como pantalla de la máquina, sin atribuirle cálculos que aún no ejecuta.

La verificación automática de compilación del APK pertenece al workflow `.github/workflows/own-pc-android.yml`.
