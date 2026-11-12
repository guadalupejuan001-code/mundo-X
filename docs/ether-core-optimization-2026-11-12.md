# Ether Core — optimización conservadora (12 de noviembre de 2026)

Se optimiza **Ether Core** dentro de `mundo-X`, conservando las operaciones
Clifford H/CZ, el estado, las direcciones y su validación, los snapshots
y los contratos existentes. El cambio **no** modifica Ether/XI como CPU de
reverseAndroid ni introduce aceleración cuántica física.

## Modificaciones del núcleo

1. **`EtherCore::pullback`:** después de verificar y normalizar la dirección
   decimal, se hace `emplace(std::move(address), Bits{})`. Evita copiar
   otra vez direcciones largas. La primera variante `try_emplace` no
   mantuvo buen rendimiento en todos los escenarios y fue descartada.
   Los alias canónicos y las excepciones conservan el contrato original.
2. **`toggle_z`:** `std::map::find` actualiza el bit Z directamente cuando
   la celda existe, la elimina cuando se cancela o inserta un vecino nuevo
   con los mismos controles de dígitos y de máximo soporte. Se descartó
   `lower_bound` porque penalizaba claves decimales largas.

No se cambian `get_bits` ni `set_bits` para las otras rutas: el cambio
está limitado a las dos operaciones medidas. El funcionamiento anterior
es la referencia inmutable de comparación.

## Evidencia reproducible (GitHub Actions)

Run final: https://github.com/guadalupejuan001-code/mundo-X/actions/runs/43188185004

Baseline exacto: `6f36ba5701efcedd5173fb8107db1994328c5810` (`main`).
Candidato: `8d06ad20ada2a2aa30b16eaa6bdc3cd53b3959be`.

- **2.583 consultas deterministas:** 1.663 respuestas correctas comparadas
  byte a byte y 920 excepciones equivalentes, digest idéntico
  `149e7a35bd784ef2` en ambos binarios.
- **8/8 pruebas nativas** de Ether, fusión, memoria, reglas, sesión infinita.
- **ASan/UBSan:** PASS sobre la misma batería determinista.
- Cuatro benchmarks host, **cinco pares alternando orden**:

| Carga | Baseline mediana | Candidato mediana | Aceleración |
| --- | ---: | ---: | ---: |
| Consultas cortas | 14.540,3 μs | 12.990,9 μs | 1,1193× |
| Dirección decimal larga | 48.976,4 μs | 46.174,3 μs | 1,0607× |
| Soporte denso | 526.460 μs | 322.119 μs | 1,6344× |
| Generador profundo | 21.608,8 μs | 21.539,6 μs | 1,0032× |

Los resultados son de **CPU anfitriona en CI**, no de Android ni garantía
de velocidad en todos los dispositivos. El caso profundo es prácticamente
neutral. La ganancia principal se concentra en soporte denso.

## Puertas y límites

El workflow `.github/workflows/ether-core-exact-optimization.yml`
compara baseline y candidato a nivel de resultados, ejecuta pruebas originales
y sanitizadores y rechaza regresiones mayores del 15% en las cuatro muestras,
además de exigir al menos un caso con mejora de 3%.

La PR permanece **draft y sin fusionar**. No se tocan los checkpoints del
emulador, GPU Virtual, REgraphics, QSSD ni formatos persistidos. Antes de
atribuir mejoras a reverseAndroid debe verificarse que su intérprete utilice
esta ruta y comparar tiempos reales sobre el mismo checkpoint.

**Conclusión:** se elimina trabajo redundante para las consultas que usan
Ether Core, sin ampliar ni alterar su contrato matemático.
