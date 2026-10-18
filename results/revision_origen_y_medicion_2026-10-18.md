# Revisión del origen y medición programable — 2026-10-18

## Alcance y conclusión

Se sincronizó GitHub y se recuperaron los 156 commits completos de `main`
hasta `eddc2c4`. Se revisaron las líneas de representación/Transformer,
simulación nativa y Python, Android/JNI/GPU/red, pruebas, supervisores,
documentación y compilación. La revisión de fuentes no equivale a ejecutar
todos los experimentos históricos ni a verificar un teléfono físico.

El [relato aportado por el titular](../docs/origen_y_recorrido_cuantico.md)
explica el punto de partida que no estaba en la conversación accesible:
cuestionar la representación de símbolos, minimizarla, organizar señales
y pensar cómo hacerlas utilizables mediante una interfaz cuántica.
Los primeros README de Git documentan modos activos y fases antes del núcleo
cuántico. La arquitectura como propósito es más amplia que cada implementación.

La evidencia actual sostiene representaciones y simulación cuántica
estructurada sobre hardware convencional. No contiene una realización física
de qubits. «Energía» y «caos», en el relato, aún requieren magnitudes y reglas
medibles para convertirse en una propuesta física.

## Hallazgos que afectan el siguiente paso

| Línea | Comportamiento observado en el código y los registros |
| --- | --- |
| Bridge X | Resumen de frecuencias y fases medias; entradas diferentes pueden tener el mismo resultado. La colisión `ABBABBBB` / `BAABBBBB` está explicada en el documento de origen. |
| Transformer X | Predicción experimental de modos; sigue separado de Compact/Reduced. |
| Compact Core original | Tableau Clifford, fases, entrelazamiento y proyección Z; ahora permite probabilidades Z y muestreo reproducible. |
| Reduced Exact T original | Acumula componentes equivalentes durante la evolución; el crecimiento de soporte sigue siendo un límite. |
| Factorized posterior | Conserva factores independientes y bloques; sus pruebas gigantes de productos no corresponden a estados arbitrarios. |
| Visuales | GPU convencional guiada por métricas del tableau; esas métricas dependen de sus generadores y no sustituyen observables o probabilidades Born. |
| Red | Transmisión de métricas/eventos; falta una verificación completa de estado y reloj lógico entre dos teléfonos. |

Python interpreta el índice 0 como el bit más significativo; el C++ denso usa
el menos significativo. Una futura conexión debe traducir esa convención.
Bell por sí solo no revela la diferencia.

## Cambios concretos

- Se añadió `probability_one_z` y medición Z según Born al Compact Core
  existente. La producción opera sobre el tableau, sin vector denso ni backend
  de simulación ajeno. La consulta general determinista tiene costo polinómico
  y memoria temporal, documentados en el header y el recorrido.
- XASM y la práctica nativa incorporan `probz`, `measure` y `seed`. Hay ejemplos
  ejecutables de interferencia y de mediciones correlacionadas de Bell.
- XASM rechaza números malformados y argumentos sobrantes antes de ejecutar la
  instrucción correspondiente.
- El editor Android comprueba operandos antes de aplicar puertas; evita el uso
  anterior de índices sin inicializar al recibir `h` o `cx 0`.
- El resumen Android da prioridad a ERROR/FAIL sobre PASS parcial.
- Se registró el origen declarado y se marcó el documento Quantum Core v0.1
  como histórico, conservando su contenido.
- El workflow Windows incorpora el runtime C++ estático, ejemplos,
  `INICIAR.cmd`, instrucciones y hash del ejecutable.

## Verificación local

Compilación limpia de Release con GCC 14.2 y CMake. **9/9 pruebas CTest PASS**:
las siete anteriores y dos nuevas de medición e integración XASM.

La prueba de medición realiza **8.317 comprobaciones y 502 mediciones**, con
124 ramas aleatorias. Usa un oráculo numérico independiente `double` solo en
el test, con matrices y proyección explícita para 30 circuitos Clifford de
1–6 qubits y casos dirigidos. Comprueba probabilidades, fidelidad del proyector
estabilizador, rango independiente, fases Y, Bell, interferencia, repetición,
consumo del generador e índices inválidos.
También se comprueba medición GHZ y correlación a través de los límites del
empaquetado de 63, 64 y 65 qubits.

La integración XASM realiza 16 comprobaciones. Además, se ejecutaron los cinco
ejemplos `.x` y dos sesiones de práctica: Bell/reset/semilla y rechazo de
instrucciones malformadas. **7/7 recorridos CLI PASS**.

El cuerpo real del editor JNI, extraído con un transporte mínimo para probarlo
en Linux, pasó tres programas válidos, 22 instrucciones malformadas y tres
operandos inválidos con UBSan. El resumen Android fue revisado para priorizar
ERROR/FAIL. Esto no equivale a ejecutar la Activity ni el APK en Android.

La revisión del cuerpo de Bridge se hizo con la biblioteca estándar y
cuantización float32. No se ejecutó PyTorch ni su suite en esta sesión porque
no estaba instalado. La ejecución del APK en un teléfono y el rendimiento en
la HP requieren mediciones en esos dispositivos. El warning preexistente de
paréntesis en `visual_metrics` permanece; no se modificó ese cálculo.

## Criterio para seguir desarrollando la arquitectura

La siguiente prueba de representación debe declarar qué información de una
secuencia debe conservarse y qué consulta se desea resolver. Medir
reconstrucción/colisiones cuando se exige fidelidad exacta; medir error y éxito
de la tarea cuando se admite un resumen. Contar representación, reglas,
identificadores, precisión, temporales y lectura.

La medición programable completa un recorrido operativo del núcleo cuántico
simulado. La conversión de señales de entrada a estados, y la implementación
de qubits físicos, son trabajos diferentes que necesitan sus propios contratos
y evidencia. Cambiar Windows o instalar otro sistema operativo no aporta por
sí mismo coherencia cuántica física.
