# Ether Core — optimización conservadora, noviembre 2026

Se optimiza **Ether Core** dentro de \`mundo-X\`, sin reemplazar Ether/XI ni
modificar la semántica Clifford, las reglas, los estados, el protocolo de
snapshots o los contratos con las aplicaciones.

## Cambios propuestos

1. En \`EtherCore::pullback\`, el domicilio decimal canónico ya validado se
   transfiere al nodo \`std::map\` con \`try_emplace(std::move(address), ...)\`.
   Esto evita copiar direcciones grandes una vez más. Los alias de dirección,
   presupuestos y errores conservan el mismo orden de validación.
2. En \`toggle_z\`, una única búsqueda ordenada identifica el nodo y permite
   cambiar Z en su sitio, eliminarlo cuando se cancela o insertar un vecino
   Z-only con hint. La comprobación del límite de sitios y dígitos se mantiene
   cuando se necesita un nodo nuevo, y la contabilidad del pico de soporte
   conserva el mismo resultado.

## Condiciones obligatorias antes de fusionar

- Compilar un **mismo test determinista** contra el commit anterior
  \`6f36ba5701efcedd5173fb8107db1994328c5810\` y contra la versión nueva.
  Comparar por byte hashes de todos los observables, signos, ejes, orden,
  estadísticas y tipos/mensajes de error.
- Ejecutar suite existente de Ether, fusión, reglas, memoria, sesión infinita.
- ASan/UBSan sin errores.
- Benchmark pareado de cuatro cargas con cinco repeticiones alternando orden.
  Registrar tiempos, no extrapolar a Android ni afirmar mejora sin medir.
  Rechazar regresiones mayores del 15% en pruebas host, y exigir al menos un
  caso con mejora medible >= 3%.
- Mantener PR independiente y sin merge; un test PASS no demuestra que cambie
  el rendimiento de reverseAndroid. Eso requiere una integración y medición
  específica en el emulador, que usa su propio intérprete y planificador.

Este ciclo no amplía el conjunto de puertas ni las capacidades físicas:
**elimina trabajo redundante manteniendo la representación exacta.**
