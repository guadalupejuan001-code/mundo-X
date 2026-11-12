# InfiniteCore C01 — autonomia de ejecucion opt-in

## Objetivo y estado

La alternativa 3 significa que InfiniteCore organice el trabajo XI de sus consumidores sobre **un propietario de estado** sin depender del planificador del PC emulado ni fingir ser el kernel de Windows o Linux.

La primera entrega (C01) introduce `mundo_x::AutonomousCore`, una politica de admision y despacho en C++17 que funciona en ambos anfitriones. Es opt-in y no modifica la semantica de `InfiniteCore`, `CompactCore`, `EtherCore`, `InfiniteSession`, su RNG, el formato MXI ni sus presupuestos matematicos.

**Estado**: implementación candidata en rama de revisión. Los resultados deben tomarse de CI, no de este documento.

## Contrato C01

- Un `shared_ptr<InfiniteSession>` original es obligatorio y sigue siendo la unica autoridad del estado. Ningún qubit se almacena en el planificador.
- Admisión con limites finitos y explicitos para tamaño de programa, cantidad de trabajos pendientes y bytes totales de la cola. Las solicitudes rechazadas nunca llegan a XI.
- Una hebra de trabajo ejecuta exactamente `InfiniteSession::execute`. El parser XI existente maneja validacion, estado candidato, rollback y confirmacion. No se crean comandos alternativos.
- Despacho con prioridades `Interactive`, `Normal`, `Background`, y una ronda ponderada de 4:2:1. Ningún nivel con cola permanente queda excluido. Los trabajos admitidos se vacían durante `shutdown`.
- El resultado devuelve salida real o error de XI, revision y epoca de la misma `InfiniteSession`, espera, duracion y contadores de admision/exito/fallo. No se atribuye una mejora de velocidad al mero uso de colas.
- No hay cancelacion forzada de instrucciones ni limite temporal fiable dentro de un programa XI. Los presupuestos propios de XI siguen obligatorios.
- Un solo planificador compartido por propietario es necesario para asegurar prioridades globales. Los consumidores que sigan llamando directamente a la sesion no pasan todavia por esta politica.

## Uso

```cpp
#include "mundo_x/autonomous_core.hpp"
auto state = std::make_shared<mundo_x::InfiniteSession>();
mundo_x::AutonomousCore scheduler(state);
auto answer = scheduler.submit("h 0\ncx 0 1\nexpect X:0 X:1\n",
    mundo_x::AutonomousCore::Priority::Interactive).get();
if (!answer.ok) throw std::runtime_error(answer.error);
const auto metrics = scheduler.telemetry();
scheduler.shutdown();
```

## Pruebas

CMake y `.github/workflows/infinite-core.yml` ejecutan `test_autonomous_core` en Windows y Ubuntu. Las pruebas comprueban ejecucion XI real, consulta sin mutaciones, rollback de un fallo tardio, tolerancia a admisiones simultaneas, limites de entrada, cierre ordenado y preservacion del propietario tras destruir el gestor.

## Condiciones de promoción

- CI nativa Windows/Ubuntu aprobada con los tests historicos; comparacion de snapshots/observables frente a la ruta sin gestor.
- Carga mixta interactiva/normal/fondo y observacion de latencia p50/p95, RAM y throughput; una cola mas compleja puede **empeorar** el rendimiento. No publicar ventajas sin A/B.
- Política activada por consumidor de forma explicita. Mantener un escape a la ejecucion original sin transformar estados.

## Siguientes ciclos

- **C02, integración**: un unico recurso gestor opt-in para PC, CLI y aplicaciones; API transportable y supervision de estado; no duplicar hilos ni sesiones por consumidor.
- **C03, adaptacion**: decisiones basadas en mediciones verificables del trabajo real, presupuestos de memoria/tiempo y backpressure; comparación A/B contra C01 y contra ejecución directa. Nunca reducir qubits en silencio, descartar amplitudes ni alterar resultados.

**Alcance real:** autonomo para ordenar trabajos aceptados; no sustituye el planificador del sistema operativo, no usa permisos privilegiados ni hardware cuántico físico. La adaptación automática a CPU/RAM anfitriona y la ejecucion de todos los consumidores se reservan para etapas verificables posteriores.
