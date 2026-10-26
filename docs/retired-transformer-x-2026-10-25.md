# Retiro de la línea Bridge X / Transformer X v0.1 — 2026-10-25

## Decisión

Se retiran del árbol activo de Mundo X los archivos `src/bridge_x.py`, `src/transformer_x.py` y `experiments/train_file_v0_1.py`.

Motivo técnico: `TransformerX` era una instancia entrenable de `torch.nn.TransformerEncoder` que recibía rasgos `[Re, Im, mode_id, sin, cos]` y predecía un pequeño delta; no implementaba un nuevo primitivo cuántico, ni estaba conectado a InfiniteCore, Ether, Reduced, Solar o la ruta nativa Android. `BridgeX` construía un resumen por frecuencia y fase circular que pierde el orden de bytes: entradas distintas pueden generar estados idénticos. Por tanto, no se debe presentar esa línea como codificación reversible universal, nueva computadora cuántica o mejora medida de rendimiento.

Esta retirada **no** afirma que toda transformación basada en modos o frecuencias carezca de posibles aplicaciones nuevas. Simplemente no existe en esta implementación una demostración que justifique mantenerla como innovación activa. Tampoco equivale a una búsqueda exhaustiva de anterioridad o patentes.

## Alcance

- Fuentes eliminados del árbol activo: `src/bridge_x.py`, `src/transformer_x.py`, `experiments/train_file_v0_1.py`.
- El README ya no ofrece esos módulos como función instalada ni instrucciones de entrenamiento obsoletas.
- La referencia histórica del origen queda fechada y marcada como histórica.
- `src/quantum_core.py`, `requirements.txt`, los núcleos C++17, Ether, Inverse Ether Memory, GPU/Android, Solar y PC permanecen intactos; PyTorch continúa siendo dependencia de la referencia Python densa.
- Los documentos de estado fechados de 2026-10-14 y las mediciones históricas no se reescriben retroactivamente.

## Reversibilidad y evidencia

La implementación anterior está preservada por Git en el commit de partida:
`7d0e919fc13eb5340d86cb6f7cb2b6cd97172a8a`.

Para inspeccionar exactamente el código retirado, sin restaurarlo al árbol actual:

```sh
git show 7d0e919fc13eb5340d86cb6f7cb2b6cd97172a8a:src/transformer_x.py
git show 7d0e919fc13eb5340d86cb6f7cb2b6cd97172a8a:src/bridge_x.py
git show 7d0e919fc13eb5340d86cb6f7cb2b6cd97172a8a:experiments/train_file_v0_1.py
```

La retirada no acredita mejoras de rendimiento por sí misma. Su objetivo es reducir una línea desconectada y evitar afirmaciones que exceden lo observado.
