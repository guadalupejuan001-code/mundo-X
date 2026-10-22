# Mundo X Language (MX) — especificación de dirección v1

**Nombre canónico del lenguaje del proyecto:** Mundo X Language (`MX`).

**Extensión fuente canónica:** `.mx`.

`XASM` (`.x`) pasa a ser una representación intermedia/compatibilidad temporal. `XI` (`.xi`) pasa a ser un dialecto de runtime temporal que debe converger en MX. Ninguno de los dos debe crecer como lenguaje independiente.

## Propósito

MX será el único lenguaje fuente de los componentes propios de Mundo X: núcleo, memoria, render, física, PC, herramientas y pruebas. El objetivo final es que el compilador de MX esté escrito en MX y pueda compilarse a sí mismo.

## Principios

1. **Una autoridad de lenguaje.** Las funciones nuevas se expresan primero en MX.
2. **Sin dependencia semántica de C++ o Python.** El comportamiento pertenece a la especificación MX, no a detalles de un host.
3. **Bootstrap explícito.** Mientras el compilador aún dependa de C++/Java/otras herramientas, esa capa se etiqueta como bootstrap temporal y no como arquitectura final.
4. **Determinismo.** Un programa, entradas y semilla iguales deben producir el mismo estado observable dentro del contrato numérico definido.
5. **Presupuestos visibles.** Memoria, expansión, componentes, regiones y trabajo deben tener límites/estadísticas consultables.
6. **Estado por reglas.** El lenguaje debe poder expresar dominios lógicos, reglas globales, excepciones finitas y materialización bajo demanda.
7. **Gráficos como dominio nativo.** Cámara, mundo, regiones, visibilidad y shading son conceptos del lenguaje, no llamadas opacas a un motor externo.

## Capas

```text
MX source (.mx)
    ↓
MX compiler / self-hosted frontend
    ↓
MXIR  (IR propia; reemplaza gradualmente XASM/XI)
    ↓
backends
 ├─ ARM64
 ├─ x86-64
 ├─ Android runtime
 └─ GPU shaders / command stream
```

## Estado actual

El compilador `.mx` existente soporta declaraciones de backend/qubits, constantes, expresiones enteras, circuitos, parámetros tipados, repetición y puertas. Todavía no es un lenguaje general capaz de reemplazar el runtime completo, el sistema de archivos, Android, red o compilación nativa.

Por tanto, `MX v1` se define como dirección de arquitectura; la eliminación de los hosts existentes se hace por sustitución comprobada, no borrando funcionalidad.

## Self-hosting gate

El bootstrap C++ puede eliminarse cuando se cumplan simultáneamente:

- existe un compilador MX escrito en MX;
- el compilador MX compila su propia fuente;
- la salida de dos generaciones consecutivas es equivalente según hash/IR normalizada;
- las suites de núcleo, Ether, render y lenguaje pasan sin usar C++/Python del repositorio;
- Android/PC reciben binarios generados desde MX.

## Propiedad

La especificación, sintaxis original y código escrito específicamente para MX pertenecen al titular del repositorio en la medida permitida por la ley aplicable. Nombres genéricos, ideas matemáticas, algoritmos de dominio público, estándares, APIs, toolchains, Android, OpenGL/Vulkan, CPU ISAs y otros componentes de terceros conservan sus propios derechos y licencias.
