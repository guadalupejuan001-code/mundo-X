# InfiniteCore C03 — integración comprobable en PC, Android y CLI

Base: PR #84, commit a3c08a1afd4c057446fb9dfa09635df2425769db, apilada sobre C01 PR #83.
Sin merge automático.

## Autoridades independientes

- InfiniteSession del PC continúa siendo la autoridad real que comparten PcQuantumDevice, MemoryFabric, editor, emulador x86/x64 y visor GLES. Sus transacciones, RNG, estado, epochs y snapshots siguen intactos.
- C01 se conecta opt-in a ese mismo shared_ptr de InfiniteSession. Un solo worker recibe tareas XI del editor y puertos del huésped, con fallback directo si se desactiva. No crea otra autoridad.
- C02 se integra como laboratorio aislado. El PC captura su snapshot canónico, C02 importa esa copia solo estando inactivo y corre el mismo XI en workers independientes. No se escribe ningún resultado en la sesión viva. El snapshot central se compara antes/después.
- C02 transaccional se ofrece en la nueva CLI nativa sobre SU autoridad privada, con compare-and-commit y reintentos. No se conectan escrituras C02 a un PC vivo que también tiene accesos directos mediante MemoryFabric, JNI y los dispositivos invitados sin coordinación; se evita perder actualizaciones.
- Solo se altera la capa de aplicaciones/adaptadores y se añaden pruebas y herramientas. No se modifican EtherCore, InfiniteCore, CompactCore, InfiniteSession ni el protocolo original de memoria.

## Android del PC

Dos controles nuevos en el panel: Planificador C01: activado/desactivado y Medir C02 · 1/2/4 workers aislados.

C01 conecta las peticiones de escritura y observación XI del host y el invitado al mismo scheduler, conservando el mismo state owner. El botón C02 ejecuta doce copias privadas del programa por configuración y muestra tiempo total en ms, tareas/s y workers concurrentes. No toca el estado de la CPU, el núcleo ni la GPU, y los resultados son medidas locales, no cifras inventadas.

El JSON del PC contiene scheduler_c01 e isolated_workers_c02_available para inspeccionar lo que está efectivamente activado. El editor y el canal del huésped mantienen el límite de entrada original de 65536 bytes.

## CLI Windows y Ubuntu

Compilar el nuevo ejecutable mundo_x_autonomous junto al antiguo mundo_x_infinite, que no cambia.

Ejemplo de uso:

    mundo_x_autonomous --workers 4 --mode isolated examples/autonomous_c03.xi examples/autonomous_c03.xi
    mundo_x_autonomous --workers 2 --mode transactional examples/autonomous_c03.xi examples/autonomous_c03.xi

Cada resultado sale como línea JSON. Isolated no confirma cambios. Transactional usa epoch compare-and-commit con reintentos; agotamiento produce fallo explícito. Los parámetros C02 de admisión dinámica, 1024 trabajos, 16 MiB por programa, 128 MiB de entrada, aging 2/5 segundos, se mantienen. No equivalen a toda la memoria del proceso ni a capacidad cuántica física infinita.

## Validación exigida

- PC native: host/guest comparten C01 en el puerto original; C02 ejecuta XI real sobre copias y el snapshot original permanece idéntico; ejecución directa funciona tras desactivar el scheduler.
- CLI: ejecución aislada y transaccional de ejemplos reales, cero salida inventada.
- CI Windows/Ubuntu: suites históricas InfiniteCore, Ether, C01, C02 más PC y CLI de C03.
- Android PC V1: compila Java/JNI NDK y ejecuta instrumentación de 30 controles; se incorporan aserciones en pruebas existentes del toggle y botón sin inflar el número de pruebas.
- Proveniencia estricta de cada fuente nativa y documentación de resultados medidos.

## Frontera de BBY Q

BBY Q U03 sigue utilizando REVERSE-PC con módulos y checkpoints versionados por SHA. No se debe afirmar que C02 acelera BBY Q sin una integración separada que mantenga sus siete bancos y sus recibos transaccionales. Tampoco se integran GPU Virtual ni QSSD en esta PR. La observación no demuestra que C02 deba sustituir el kernel de Windows, Linux o Android.

Hasta que las campañas aprueben, esta PR permanece en borrador y no se despliega ni fusiona.
