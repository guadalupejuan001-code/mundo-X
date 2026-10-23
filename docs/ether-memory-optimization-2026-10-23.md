# Ether / Inverse Memory: optimización transaccional v1

Referencia base: `main` @ `d4a63cafdb47dc237b07c0d48fe2cf3e254e54b7`
(22 de octubre de 2026). Esta modificación se desarrolla por separado y no
cambia automáticamente `main`.

## Objetivo

Optimizar el **trabajo y la RAM real** utilizados por las operaciones, no
solamente la cantidad de direcciones que puede describir Ether.

Antes, `QuantumRuntime::execute` creaba una copia de `InverseMemory` antes
de saber si el programa iba a escribir. Como sus dos mapas `std::map`
tenían propiedad por valor, copiar una memoria con N excepciones clásicas y
Q enlaces cuánticos implicaba replicar N + Q nodos, incluso para `mem_read`.
El dueño `InfiniteCore` ya compartía estados mediante `shared_ptr` y
separación por escritura; la memoria lógica no.

## Cambios de esta etapa

1. **Copy-on-write independiente** para excepciones y enlaces. Crear
   una copia transaccional comparte ambos mapas; las consultas siguen
   leyendo el mismo contenido inmutable desde los propietarios existentes.
2. **Separación por páginas.** Cada mapa se divide en 256 páginas
   deterministas de direcciones. El candidato comparte el directorio; al
   escribir separa el directorio y **solo la página afectada**. Un
   `mem_store` no clona enlaces y un `mem_bind` no clona excepciones.
   Las operaciones sin cambios no separan los datos. `mem_reset` publica
   mapas vacíos nuevos; liberar el último propietario de los mapas viejos
   todavía puede costar tiempo de destrucción.
3. **Persistencia y validación.** Una fusión ordenada de las páginas
   conserva el texto de snapshots MXIM1. El benchmark compara también un
   hash determinista del snapshot completo antes y después.
   Su lector rechaza enlaces cuyo byte de ocho qubits excedería el
   presupuesto de dígitos de la memoria. La restauración conjunta del
   `QuantumRuntime` también valida contra el presupuesto **del núcleo
   receptor**, antes de confirmar cualquiera de los dos estados.
4. **Regresión y evidencia.** Se prueban 64 copias lectoras de una memoria
   con 1.024 excepciones, cuatro variantes de escritura independientes,
   restauraciones rechazadas sin efectos parciales y el programa XI original.
   El banco autónomo `ether_memory_copy_benchmark` mide por separado
   256 copias/lecturas sobre 8.192 excepciones y 512 enlaces, y el coste
   de 16 primeras escrituras; no impone umbrales frágiles de rendimiento.

## Costes que continúan

- La primera escritura copia un directorio fijo de 256 punteros y
  únicamente la página que contiene la dirección afectada. Con distribución
  equilibrada, el coste de copia local corresponde aproximadamente a N/256;
  en el **peor caso**, todas las direcciones podrían caer en una misma página
  y conservar un coste O(N). No se promete rendimiento constante universal.
- El directorio añade memoria fija por mapa, incluso vacío. Los snapshots
  fusionan los cursores ordenados de las páginas: conservan MXIM1 byte a byte,
  pero el coste de exportación debe compararse con el mapa monolítico.
- Los dos mapas aún almacenan nodos y cadenas de texto por excepción y enlace.
  No son memoria física infinita ni qRAM.
- `mem_materialize`, las exportaciones de snapshots y las consultas a
  enlaces cuánticos conservan su coste real. No se atribuyen mejoras a
  GPU, Solar, física, emulación de PC ni otros subsistemas que no pasen por
  este camino.
- La lectura de MXIM1 continúa sin recuperar el contador interno
  `revision`; eso ya ocurría en la revisión base y deberá tratarse
  aparte si un consumidor requiere esa información.

## Cómo verificar esta rama

```bash
cmake -S quantum-runtime -B quantum-build -DCMAKE_BUILD_TYPE=Release
cmake --build quantum-build --parallel 2
ctest --test-dir quantum-build --output-on-failure
./quantum-build/ether_memory_copy_benchmark
```

El workflow `quantum-only.yml` conserva `ctest.log` y la salida JSON del
benchmark. No interpretar el resultado como una aceleración frente a
simuladores externos ni como FPS medidos en Samsung: debe compararse la
**misma carga** antes/después, mismo hardware y compilación, además de
registrar RSS/PSS del proceso si el objetivo es consumo real de RAM.

## Siguiente etapa y criterio de aceptación

1. Ejecutar la regresión completa y el benchmark de esta etapa; resolver
   cualquier fallo sin relajar aserciones ni presupuestos.
2. Medir cargas reales con muchas lecturas, escrituras, enlaces y restores
   intercalados; comprobar latencia p50/p95 y memoria pico por proceso.
3. Si el coste O(N) de primera escritura domina, introducir almacenamiento
   persistente por páginas / árbol inmutable o un diario transaccional que
   evite copiar todas las excepciones. Elegir tras mediciones.
4. Auditar las rutas Ether -> puente nativo/JNI -> Graphics/Solar/PC.
   No afirmar reducción global hasta medir cada consumidor y el extremo a
   extremo en dispositivo real.

Esta etapa conserva el modelo semántico: un fondo por regla, excepciones
finitas, enlaces explícitos, consultas sin medición oculta y transacciones
reversibles.
