# Migración a MX autoalojado

Base de esta migración: Ether Render X v0.2 (`f2dee1f4f074a12b30ba8a31357790d90f934669`).

## Inventario inicial

- C/C++: 136 archivos.
- Python: 101 archivos.
- `.mx`: 3 archivos.
- `.x` XASM: 12 archivos.
- `.xi`: 5 archivos.

En la primera limpieza se retiraron 7 fuentes/tests Python del prototipo antiguo y el `requirements.txt` raíz, quedando 94 archivos Python. Esos prototipos no estaban en la ruta productiva de Ether/InfiniteCore/Solar/PC.

## Política

No se aceptan **archivos C/C++ o Python nuevos** durante la migración. Los existentes forman un presupuesto decreciente. Una sustitución sólo cuenta cuando la misma función queda cubierta por MX y sus pruebas.

## Fases

### Fase A — lenguaje como autoridad

- `.mx` fuente canónica.
- `.x` y `.xi` quedan como IR/dialectos temporales.
- congelar crecimiento C++/Python.
- definir MXIR y ABI de runtime.

### Fase B — herramientas y pruebas

- reemplazar verificadores Python por herramientas MX.
- mover campañas de exactitud, hashes, fixtures y benchmarks a MX.
- retirar `graphics-desktop` Python cuando exista observador MX equivalente.

### Fase C — núcleo

- portar `CompactCore`, `InfiniteCore`, `EtherCore`, Inverse Memory y runtime transaccional a MX.
- comparar snapshots/observables byte por byte o por contrato numérico.
- retirar headers/sources C++ de cada componente sólo después de equivalencia.

### Fase D — render

- portar Ether Render X a MX nativo.
- backend GPU generado desde MX.
- absorber QuantumRenderer/SolarRenderer por capacidades, no por nombre.

### Fase E — PC / Android

- reemplazar runtime nativo C++ de PC y JNI por artefactos compilados desde MX.
- conservar Java/Android únicamente como host de plataforma mientras sea necesario; después reducirlo a shell de plataforma o generar bindings.

### Fase F — self-host

- compiler stage0 (bootstrap existente).
- compiler stage1 escrito en MX.
- stage1 compila stage2.
- stage2 recompila el compilador y produce salida estable.
- eliminar bootstrap C++.

## Regla de borrado

No se borra una implementación activa porque 'no sea nuestra'. Se borra cuando MX ya reproduce su contrato. Git conserva la historia, así que la fuente retirada sigue auditable sin permanecer en la rama activa.

## Objetivo final

```text
repositorio activo
  .mx          fuente propia
  .mxir        IR propia
  assets       datos/arte
  platform/    descriptores mínimos necesarios
  generated/   artefactos derivados (no autoridad)
```

Sin `.py`, `.cpp`, `.hpp`, `.h`, `.cc`, `.cxx` como fuente activa del proyecto.
