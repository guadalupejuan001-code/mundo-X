# Validación numérica independiente

Estas herramientas comparan `ReducedExactTRepresentation` con una referencia
NumPy `complex128`. La referencia aplica matrices sobre ejes tensoriales y no
usa `DenseCore`. Los archivos del repositorio se leen para compilar el adaptador;
los circuitos y resultados se guardan en `--output-dir`.

Requiere Linux, Python 3, NumPy y `g++` con C++17. No instala dependencias.

```bash
python3 tools/architecture_numerics/stress.py \
  --suite sanity --tolerance 1e-9 --output-dir /tmp/mundox-numerics
python3 tools/architecture_numerics/stress.py \
  --suite representative --tolerance 1e-9 \
  --output-dir /tmp/mundox-numerics --resume
```

`sanity` reproduce 200 casos deterministas de la revisión original: circuitos
aleatorios de 1–8 qubits y hasta 200 puertas, T⁸, circuitos con sus inversos y
T²=S. `representative` selecciona 12 casos del corpus profundo, incluyendo
100 000 puertas, cancelaciones y mediciones. `deep` contiene 77 casos de
1–10 qubits, con profundidades de 1 000, 10 000 y hasta 100 010 puertas;
`all` reúne los 277 casos. Las puertas son H, S, X, Z, CX, CZ y T. Algunos
casos profundos repiten fases T sobre estados producto; sus nombres históricos
`T8-product` no implican que todos sus exponentes sean múltiplos de ocho.

Para reproducir el baseline con base Clifford `float32`, use `--repo` apuntando
a su checkout y `--tolerance 3e-5`, con otro directorio de salida. El candidato
con reconstrucción `float64` se verifica con `1e-9`. La muestra finita no prueba
corrección para todo circuito, escalabilidad ni ventaja cuántica.

Cada compilador, adaptador y referencia ejecuta secuencialmente con límite de
1 GiB de memoria virtual, 19 segundos de CPU, 20 segundos de pared y un hilo
BLAS. Un tiempo agotado se registra como tal y hace fallar la ejecución; no
establece un límite matemático de la arquitectura. `--lock-file` permite
compartir un bloqueo exclusivo con otras pruebas. El bloqueo por defecto está
en el directorio padre de la salida. Los grupos de procesos se terminan al
agotarse el tiempo, incluyendo los hijos del compilador.

La salida contiene `manifest.json`, `results.json`, `summary.json`, los
circuitos completos y los estados originales de ambos simuladores. Se guardan
semillas, hashes de circuitos y cabeceras, norma, fidelidad normalizada y error
L2. La columna `dense_diagnostic` corresponde a `dense_reference()` de la
implementación y sirve para diagnóstico; NumPy es la referencia independiente.
`--resume` conserva éxitos previos solo con las mismas cabeceras y tolerancia.
`--only TEXTO` filtra los identificadores; úselo junto a `--resume` para ampliar
una salida existente.

Los casos de `CompactCore` comprueban que sus generadores finales tengan
expectativa +1 sobre la proyección NumPy y que los indicadores de medición
coincidan. También exigen rango binario completo y conmutación entre los
generadores. `measure_z_forced` devuelve `false` ante cualquier medición ya
determinista, incluso si se solicita la rama imposible, y conserva el estado.
La referencia reproduce esa semántica; estas pruebas no validan un muestreador.

La exploración separada del núcleo denso `float32` se reproduce con:

```bash
python3 tools/architecture_numerics/dense_threshold_check.py \
  --output-dir /tmp/mundox-dense-threshold
```

Busca el primer prefijo de H,T alternadas que supera error de norma² `3e-5`,
y contrasta ese prefijo con NumPy y la representación reducida. El umbral
encontrado depende del circuito, la precisión y la plataforma; no se presenta
como una profundidad universal admisible.
