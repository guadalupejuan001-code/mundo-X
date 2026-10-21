# Ether Memory v0.2 — MemoryFabric x86 directo

**Estado:** compatibilidad experimental del PC; **sustituido como arquitectura
principal de memoria cuántica** por
[Inverse Ether Memory](inverse-ether-memory.md).

La ventana x86 continúa disponible para validar integración clásica, pero
`quantum-runtime/` no compila MemoryFabric, SparseRam, x86, VBE ni GLES.

## Objetivo

Ether Memory v0.1 añadió bytes lógicos al lenguaje Infinite mediante
`qload8`/`qstore8`. v0.2 añade una ruta de memoria física x86 directa sin
eliminar la RAM clásica general.

La RAM física del Galaxy A16 5G sigue siendo clásica. Esta integración cambia
la autoridad lógica de una ventana pequeña: sus bytes se leen y escriben en
Ether, mientras el resto de los 2 GiB lógicos continúa en `SparseRam`.

## Mapa

- Ventana física x86: `0x47FFF000..0x47FFF0FF` (256 bytes).
- Byte Ether inicial: `1000000000000`.
- Mapeo: `physical_base + n -> qbyte(base + n)`.
- Cada qbyte usa ocho direcciones consecutivas `q(8a)..q(8a+7)`.
- La página física que contiene la ventana ya pertenece a la apertura VBE
  reservada por BIOS. El framebuffer BGRX 8K máximo termina en `0x47E90000`;
  la ventana comienza 1.507.328 bytes después.

La elección evita cambiar el mapa E820 y no pisa ninguno de los 33.177.600
píxeles del framebuffer 8K máximo implementado.

## Semántica exacta

`MemoryFabric` mantiene `SparseRam` como respaldo clásico y enruta solamente
la ventana anterior hacia callbacks de `InfiniteSession`.

- Una lectura x86 usa `InfiniteSession::qload8`.
- Una escritura x86 usa `InfiniteSession::qstore8`.
- Leer un byte no determinista en Z falla. No se introduce una medición oculta.
- Un byte cero intacto no materializa qubits en el fondo zero.
- Una escritura que cambia bits materializa solamente las modificaciones
  requeridas por InfiniteCore.
- El rollback de una instrucción restaura el byte anterior mediante la misma
  ruta; `qstore8` es atómico por byte.
- Bloques que cruzan el límite clásico/Ether preflightan primero la parte
  SparseRam y conservan los bytes Ether anteriores para poder restaurarlos si
  el proveedor rechaza una escritura.

Los contadores `ram_allocated_bytes` y `ram_budget_bytes` siguen describiendo
SparseRam. El estado y límites de Ether se informan por separado en el objeto
`quantum` del diagnóstico. El JSON del PC expone además base y tamaño de la
ventana.

## Prueba gráfica tipo juego

El repositorio no contiene un videojuego comercial redistribuible. Para probar
integración sin inventar resultados se usa el huésped gráfico propio:

1. Ejecuta instrucciones x86 del invitado.
2. Entra en modo protegido.
3. Programa VBE 1280x720.
4. Escribe y verifica sus propios píxeles.
5. El test comprueba los marcadores de las esquinas y que el framebuffer no
   modifique la ventana Ether.

La suite completa existente también ejecuta un huésped 8K y la instrumentación
Android comprueba framebuffer GLES real. Estas pruebas son cargas gráficas de
integración, no benchmarks de un juego comercial.

## Procedencia

El código nuevo de esta integración está escrito para Mundo X y usa solamente:

- los componentes existentes `InfiniteCore/InfiniteSession`, `SparseRam`,
  `Cpu` y `PcVideo`;
- la biblioteca estándar C++17;
- la gramática Infinite/XI ya integrada para `qload8`/`qstore8`.

No se incorporó fuente de QEMU, Bochs, Wine, Limbo ni otro emulador. Esto
describe procedencia del cambio; no es una certificación jurídica de
originalidad mundial de cada algoritmo del repositorio.

## Límite

Esta versión **no convierte los 2 GiB de RAM x86 en qubits**. Una ventana de
256 bytes puede requerir hasta 2048 qubits materializados en el peor caso,
dentro del límite PC actual de 4096 qubits activos, dejando margen al resto de
la sesión. Ampliar la ventana exige estudiar representación de bytes
estructurados, compactación y conflictos con estados realmente cuánticos antes
de aumentar el tamaño.
