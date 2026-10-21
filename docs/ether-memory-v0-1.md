# Ether Memory v0.1

**Estado:** compatibilidad histórica integrada; **sustituido como arquitectura principal**
por [Inverse Ether Memory](inverse-ether-memory.md).

`qload8/qstore8` se conservan para consumidores heredados, incluido MemoryFabric
del PC. El runtime cuántico puro los rechaza expresamente para evitar volver al
mapeo global de ocho qubits por byte.

Ether Memory no convierte la RAM física del teléfono en qRAM de hardware. La
RAM del Galaxy A16 5G sigue siendo clásica. Esta capa cambia la **representación
lógica**: un byte puede vivir directamente en el mismo dominio de qubits de
Ether y materializar solamente las modificaciones que hagan falta.

## Contrato exacto

- El byte lógico `a` usa ocho direcciones consecutivas:
  `q(8a+0)..q(8a+7)`, con bit 0 como LSB.
- `qload8 a` consulta los ocho Z sin medirlos. Si todos son deterministas,
  devuelve el byte. Si algún bit está en superposición/entrelazamiento respecto
  de Z, falla en vez de colapsarlo silenciosamente.
- `qstore8 a v` acepta `0..255`. Primero comprueba los ocho bits; si todos son
  clásicos aplica únicamente los X necesarios como un batch atómico.
- En fondo `zero`, leer bytes intactos no materializa qubits. Escribir cero
  tampoco. Un byte `0xA5`, por ejemplo, materializa cuatro bits uno.
- Volver un byte a cero restaura sus valores físicos; `compact` libera los
  componentes que coinciden exactamente con el fondo.
- Las direcciones permanecen en decimal textual y pueden superar 64 bits dentro
  del presupuesto de dígitos de InfiniteCore.

Ejemplo:

```text
background zero
qload8 0
qstore8 0 165
qload8 0
qstore8 0 0
compact
```

El mismo lenguaje viaja por `PcQuantumDevice`, así que un huésped x86/x64 puede
enviar estos comandos por los puertos existentes `0x5850/0x5851`.

## Qué resuelve y qué no

Esto demuestra una **memoria lógica respaldada por Ether** para datos escasos o
estructurados. No sustituye todavía `SparseRam` como RAM general de la CPU.
Una carga arbitraria y densa puede materializar muchos qubits y llegar a los
presupuestos de `InfiniteCore`; con el perfil actual hay 4096 qubits activos y
1024 por componente. Por eso no se presenta como 2 GiB de qRAM densa.

La CPU x86 mantiene su RAM clásica exacta. El experimento siguiente,
[MemoryFabric v0.2](ether-memory-v0-2.md), ya implementó una ventana explícita.
Esa ruta se conserva como compatibilidad del PC; la investigación principal de
memoria cuántica continúa en Inverse Ether Memory.

## Procedencia

El código de esta entrega se escribió para Mundo X usando el núcleo
`InfiniteCore/InfiniteSession` y la biblioteca estándar de C++17. No incorpora
fuente de QEMU, Bochs, Wine, Limbo ni otro emulador. La búsqueda del repositorio
no encontró avisos de copyright o licencias de terceros dentro del código de
este componente; las menciones a QEMU que aparecen en `results/` pertenecen al
emulador de Android usado por CI.

Esto es una auditoría de procedencia del repositorio, no una garantía jurídica
de originalidad mundial de cada algoritmo. Las semánticas x86 se contrastan con
las especificaciones de Intel y las APIs Android/GLES siguen siendo interfaces
externas.
