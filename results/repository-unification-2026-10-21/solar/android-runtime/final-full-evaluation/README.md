# Ensayo histórico: referencia completa frente a candidato con máscaras GPU

Esta carpeta conserva el fallo reproducible del commit
`3954b0008db6b61acb1700b8201d75f1f18623e2`: **6/8 escenas exactas y 8 píxeles
distintos de 584.640**, cuatro en Tierra y cuatro en Saturno. Ambas versiones
repitieron sus ocho capturas exactamente y coincidieron sus **29 hashes de
entrada por captura**. El resultado motivó retirar el filtrado GPU; el shader
final posterior (`e36370ee…`) tiene su evidencia en `../../full-evaluation-fallback/`
y en los resultados CI correspondientes a su propio commit.

| Componente de este ensayo histórico | Identificador |
| --- | --- |
| Shader candidato con máscaras GPU | `361676e4882e9d5560c05d098f9f2092425d579323002082d779ff4c2b72bc50` |
| Referencia completa e independiente | `1c978eab12807d2f791bebe4167564027e7ef705947a58f16313461224cc2753` |
| APK candidato | `a32e5de449e008d0552633f714999eefd46b0e5f8576024edd89eb2037a1730e` |
| APK de referencia | `b92745aad2803b7690d83ae8ff90051d752822cb833e999f43570d12dfd0b81e` |
| APK de instrumentación común | `09302337c282dd8348e180824399baf28617e357025a33e959b221b540c85862` |

## Evidencia conservada

- `runner.json`: estado de fallo, configuración, tiempos y resultados completos.
- `comparison-analysis.json`: diferencia de cada captura y repetición, coordenadas,
  número de canales afectados y comparación de entradas.
- `before/` y `after/`: informes originales, logs, memoria, compilación solicitada
  de ART y 16 PNG por versión, incluidas las repeticiones.
- `campaign-provenance.json`, `SwiftShader.ini` y `driver-config-open.log`: APKs,
  fuentes y apertura observada de la configuración exclusiva del controlador.
- `attempt-display-timeout/`: intento previo detenido antes de instalar APKs,
  después de agotar 30 segundos durante la preparación de pantalla.
- `tcg-display-deadline.patch`: única diferencia del runner local en el reintento;
  permitió 120 segundos por comando de preparación. Conservó los límites de
  pruebas y la igualdad exacta de imágenes.

El entorno fue Android 10/API 29, x86_64, TCG, 2.048 MB de memoria de invitado y
SwiftShader con `ThreadCount=1`. Se solicitó ART `speed`; el informe no infiere
cobertura AOT completa. Las ocho capturas de la referencia fueron idénticas a
las del fallo CI Android 15/KVM, run `39526904178`, job `118983587207`, artifact
`12313533382` (SHA-256 del ZIP:
`8c73f2220329f5de1473d23ac869051b2c3b563d1b3771abde6662713b72aacb`).

## Preservación sin pérdidas

`lossless-frame-provenance.json` registra los hashes y la orientación de los
**32 PNG**. Antes de trasladar los originales se reconstruyeron y compararon
byte por byte sus **9.354.240 bytes RGBA**; también coincidieron todos los hashes
registrados por Android. Los PNG están orientados para su lectura visual, por lo
que se invierten las filas al reconstruir el orden original de OpenGL.

Los 32 archivos `.rgba` y los dos `.tar` se trasladaron, sin borrar datos, a:

`/workspace/scratch/d0e1d0a0e3a1/closure/unification/solar/raw-failed-full-reference`

Allí se preservan las rutas relativas y `relocation-manifest.json`. Los archivos
TAR contienen únicamente las 16 capturas y el informe JSON de su versión: se
verificaron los 34 miembros de archivo contra las copias extraídas. Sus hashes
quedan en el manifiesto. El repositorio conserva PNG, JSON, logs y procedencia,
y evita duplicar 18.823.168 bytes de readbacks y contenedores de transporte.

Para comprobar la reconstrucción desde una copia del repositorio:

```python
from pathlib import Path
from PIL import Image
import hashlib, json

root = Path("results/repository-unification-2026-10-21/solar/android-runtime/final-full-evaluation")
manifest = json.loads((root / "lossless-frame-provenance.json").read_text())
for frame in manifest["frames"]:
    with Image.open(root / frame["png_path"]) as picture:
        assert picture.mode == "RGBA"
        raw = picture.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes()
    assert len(raw) == frame["raw_bytes"]
    assert hashlib.sha256(raw).hexdigest() == frame["raw_sha256"]
    assert frame["raw_sha256"] == frame["reported_sha256"]
```
