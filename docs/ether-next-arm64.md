# Ether Next: ejecutor experimental ARM64/NEON

Rama independiente `experiment/ether-next-arm64-neon`, desde main
`6f36ba5`. No modifica `pc-android`, C09R-1, REVERSE-PC ni CPUID.
No ejecuta Windows ni consume una ISO.

## Implementación

EtherCore conserva su dominio cuántico y añade `Transform` / `TransformMemory`
para transformaciones finitas de valores. No contiene otra CPU. XI sigue
usando el `InfiniteCore::apply` original: las involuciones iguales se cancelan
atravesando sólo puertas sobre direcciones disjuntas, después de validar todo
el lote. Los lotes no vacíos mantienen su revisión. La caché de pullbacks vive
sólo durante ese lote, con límites de entradas, términos y direcciones; guarda
transformaciones del marco, nunca respuestas dependientes del estado finito.
Inverse Ether Memory proyecta directamente el fondo cuando no existen deltas
ni bindings, validando también el extremo del rango.

El compilador finito recibe bytes x86-64 de un bloque completo y construye SSA
con nodos de hasta ocho bytes. Compone identidades y cancelaciones enteras
modulares, propaga copias, comparte expresiones estructuralmente iguales y
elimina nodos muertos. El backend recibe únicamente ese grafo reducido.

El JIT emite instrucciones AArch64 Advanced SIMD de 128 bits, carga entradas
bajo demanda, reutiliza registros al último uso y difiere las escrituras. Usa
los 24 registros SIMD caller-saved, memoria RW seguida de RX y sincronización
de la caché de instrucciones. La memoria de transformaciones está acotada,
compara bytes completos y comparte código entre grafos iguales. Cada llamada
lee entradas actuales; cambiar bytes selecciona una entrada distinta y
`invalidate()` descarta todas las entradas. El propietario serializa su uso.

## Frontera semántica

Acepta MOVDQA/MOVDQU registro-registro, PXOR/PAND/POR/PANDN y PADD/PSUB con
lanes de 8/16/32/64 bits, incluidos XMM8..15. Traduce bloques completos; rechaza
memoria, LOCK, instrucciones ajenas y colas truncadas sin ejecutar un prefijo.

Es un ejecutor de transformaciones provistas por el llamador, sin conexión
al bucle de instrucciones de PcMachine. El llamador aporta un `Guard` que
acredita modo largo, SSE2 habilitado y OSFXSR, sin EM/TS, single-step u
observación por instrucción. Un rechazo retorna `false` y deja el buffer
intacto para que el llamador conserve su ruta canónica. RIP, fetch del huésped,
interrupciones y entrega de excepciones siguen siendo responsabilidad de esa
CPU. El main actual todavía no ofrece ese estado SSE2: conectar el adaptador
se pospone para no intervenir en C09R-1.

No acelera SSE2 flotante ni x87: no reasocia FP ni convierte 80 bits a tipos del
host. El subconjunto entero deja MXCSR/x87/EFLAGS del huésped fuera de su API;
no demuestra conformidad completa de redondeos, excepciones o precisión x87.
No hay fusión FMA ni spills; presión superior a 24 registros retorna fallback.
La ejecución nativa está limitada a Linux AArch64 little-endian con ASIMD y
permiso para mappings ejecutables. Otros hosts pueden reducir y emitir, pero
`execute` retorna `false`.

## Reproducción acotada

```sh
tools/run_ether_next.sh /tmp/ether-next-evidence
# Exigir hardware ARM64 nativo:
ETHER_REQUIRE_ARM64=1 tools/run_ether_next.sh /tmp/ether-next-evidence
# Integración CMake optativa:
cmake -S . -B /tmp/ether-build -DMUNDO_X_ETHER_NEXT=ON -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/ether-build --target test_ether_next test_ether_next_quantum -j2
ctest --test-dir /tmp/ether-build -R '^ether_next' --output-on-failure
```

El experimento usa la misma fuente y el mismo emisor antes/después de reducir
el grafo, sin comparar versiones históricas. Conserva 936 equivalencias,
oráculo SSE2 real en x86, referencias independientes por lane en ARM, 24 casos
nativos sin reducir, guards, bytes cambiados, invalidación y FPCR/FPSR intactos.
La prueba cuántica enfocada verifica 1553 condiciones, incluidas equivalencia
Pauli, invalidación del marco y XI con memoria cuántica enlazada.

Los contadores físicos describen instrucciones del código rectilíneo emitido
y ejecutado, no contadores PMU ni un factor de aceleración temporal. El tiempo
monotónico de 20000 llamadas y CPU/RSS son una observación acotada; el coste de
reducción y compilación dinámica se informa por separado. No mide Android,
una aplicación completa ni una CPU x86 completa.

## Resultado verificado

Código ejecutado: `b6b92553d06af9cae270191dab0b11b928484217`.
[Run ARM64 nativo exitoso](https://github.com/guadalupejuan001-code/mundo-X/actions/runs/43379283032)
con Ubuntu 24.04 ARM y GCC 13.3.0; sin QEMU. Resultados permanentes en
`results/ether-next-arm64/` y artefacto original enlazado en ese run.

| Mismo bloque / mismo emisor | Sin reducción Ether | Con reducción Ether |
| --- | ---: | ---: |
| Operaciones NEON por llamada | 15 | 2 |
| Instrucciones AArch64 por llamada | 38 | 14 |
| Cargas de 128 bits | 11 | 3 |
| Escrituras de 128 bits | 10 | 7 |

Equivalencia: 936/936 casos nativos y 24/24 casos sin reducción; todos los
16 registros de salida se comparan. El desensamblado optimizado contiene
`movi v0.16b,#0` y `add v2.4s,v1.4s,v2.4s`, ejecutados directamente por el JIT.
Las 20000 llamadas reutilizan el código con entradas cambiantes: 103986 ns
monotónicos, 0.000106 s de CPU y máximo RSS de proceso 2772 KiB. Reducción y
compilación dinámica: 5551 ns. No se calcula un speedup con estos tiempos.

Regresiones originales locales correctas: ether_core, ether_frame,
ether_rules, ether_memory, infinite_core, infinite_session, ether_fusion e
inverse_memory. La preparación de compacción de ether_fusion usa ahora dos
transacciones Z separadas, pues Z/Z en el mismo lote ya no crea basura.

El commit posterior de evidencia usa `[skip ci]` para no activar campañas
heredadas, builds de Windows ni benchmarks históricos al abrir la PR. El run
ARM anterior verifica el código exacto; la evidencia añade sólo documentación
y resultados. Se canceló el workflow visual ajeno disparado por el push.
