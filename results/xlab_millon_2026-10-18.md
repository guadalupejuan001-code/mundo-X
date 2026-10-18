# XLab · Objetos+ 0.3.2 — ensayo del millón y dibujo bajo demanda

Pasaron **19 recorridos Android**, el **ensayo separado de 1.048.576 objetos**,
**12 pruebas nativas** y **448 comprobaciones del modelo Java**. El máximo se
comprobó con el campo quieto, la población completa y regreso a 1.024 objetos.

[Descargar ZIP](https://github.com/guadalupejuan001-code/mundo-X/releases/download/v0.3.2/XLab-Objetos-0.3.2.zip)
· [enlace alternativo al mismo ZIP](https://raw.githubusercontent.com/guadalupejuan001-code/mundo-X/6b206566022afa8b7b20b9e6dd3153ce8dad9182/dist/XLab-Objetos-0.3.2.zip).
Descomprime e instala el APK incluido.

## Archivos y trazabilidad

- Código: `e3cebb5fe1056913fcfacd811785566b8a6b40fe`.
- [Compilación y recorridos habituales](https://github.com/guadalupejuan001-code/mundo-X/actions/runs/39153118422).
- [Ensayo dedicado de 1.048.576 objetos](https://github.com/guadalupejuan001-code/mundo-X/actions/runs/39153266247).
- [Firma y empaquetado independientes](https://github.com/guadalupejuan001-code/mundo-X/actions/runs/39153321055).
- APK: **2.417.216 bytes**, SHA256
  `25d541789c029e1352328c695fad579467087ff3fcc27c1d249f329409b02701`.
- ZIP: **883.727 bytes**, SHA256
  `78bcd523444e49b431b145ffa4a61bfb06db01474a910af2d6a2be51a77d2fed`.

El ZIP contiene APK firmado, checksum, instrucciones e informe de firma. Sus
ocho entradas de aplicación coinciden con el APK de CI, salvo las firmas. La
firma v2/v3 conserva el certificado de 0.3.1; las bibliotecas ARM64 y x86-64
mantienen alineación de 16 KiB. La clave privada no se publica. El paquete
`com.mundox.xlab` actualiza Objetos+ 0.3.1 conservando sus datos.

## Cambio que se comprueba

Antes, **Campo: quieto** detenía la evolución analítica, pero GLSurfaceView
seguía dibujando la misma escena continuamente. Ahora utiliza dibujo bajo
demanda: editar, seleccionar, mover la cámara, cambiar de ruta o comprobar
la imagen solicita un nuevo dibujo. Las consultas de visibilidad pendientes
solicitan los fotogramas que necesitan y después se detienen. El benchmark
mantiene una secuencia de fotogramas durante su calentamiento y muestra.

Los perfiles desde 262.144 objetos abren quietos al crear, restaurar o volver
a cargar una escena. El usuario puede activar el campo. El perfil máximo sigue
enviando **1.048.576 instancias**, cada una con 12 triángulos: **12.582.912
triángulos y 37.748.736 vértices** por dibujo completo. No se añadió LOD ni se
sustituyó parte de la población por representantes.

El texto de estadísticas distingue el estado quieto de los intervalos entre
fotogramas. Esperar una acción humana no es una medición de fluidez; **Medir A/B**
sigue midiendo sus propias muestras con cámara y tiempo congelados.

## Primer intento y alcance de la corrección

El [primer intento Android](https://github.com/guadalupejuan001-code/mundo-X/actions/runs/39151979582)
alcanzó la comprobación de visibilidad del millón y se detuvo durante la captura
global de pantalla, antes de la comparación A/B. El sistema del emulador cayó:
su watchdog terminó `system_server` después de bloqueos de 91 segundos.
Los registros muestran presión de CPU y E/S, RCU stalls y servicios Android
bloqueados. No acreditan un error de cálculo ni un error GL como causa primaria.

La nueva ejecución evita el redibujado estático continuo y la captura global
del compositor para este perfil. Conserva como requisitos la consulta de
visibilidad, la lectura real de los dos framebuffers y el regreso a 1.024
objetos. También deja asentarse los servicios de primer arranque del emulador.
El cambio reduce trabajo repetido; no identifica por sí solo todos los factores
del bloqueo de aquel entorno.

## Resultado Android del millón

Android 15 x86-64, Google SwiftShader por software, viewport **996 × 778** y
profundidad de **24 bits**. El emulador se lanzó con `-memory 2048`; Android
declaró **2.593.198.080 bytes** de RAM total y **1.113.915.392 bytes** disponibles
antes de cargar la escena. No debe presentarse como prueba en un teléfono
físico de 2 GB.

Se cargó el perfil 1.048.576, semilla 2026, sin excepciones. La consulta de
visibilidad de la vista pasó. Después se dibujó cada ruta completa a tiempo
fijo en un framebuffer RGBA8 de **128 × 128** y se leyeron sus píxeles reales:

| Resultado | Reglas + cambios | Referencia explícita |
| --- | ---: | ---: |
| Instancias enviadas | 1.048.576 | 1.048.576 |
| Píxeles con objetos | 3.420 | 3.420 |
| Hash de imagen | `5ff4041ec22afd29` | `5ff4041ec22afd29` |
| Transformaciones muestreadas contra CPU | 1.024, cero diferencias | 1.024, cero diferencias |

La comparación de imágenes obtuvo **cero píxeles diferentes** y diferencia
máxima de canal **cero**. Cámara a distancia 3.174,4, plano cercano 31,744 y
lejano 7.290,4. Se volvió a 1.024 objetos y se exigió otra imagen visible antes
de terminar la instrumentación con un caso aprobado y cero fallos.

El JSON indica `visibility: pending` durante la comprobación A/B porque ésta
invalida la consulta de la vista; conserva además
`live_visibility_confirmed_before_verification: true`. La cobertura de los
framebuffers es una comprobación independiente. No se solicitó captura global
de pantalla al compositor en este perfil; no reemplaza la lectura de píxeles.

- [Imagen A/B y contexto del millón](xlab_millon_2026-10-18/xlab-1048576.json).
- [Memoria previa](xlab_millon_2026-10-18/xlab-1048576-preflight.json).
- [Instrumentación del millón](xlab_millon_2026-10-18/million-instrumentation.txt).

La referencia asignó **16.777.216 bytes de VBO**. La descripción lógica de la
regla fue de **32 bytes**, la textura reservó 8 bytes y la malla compartida 864.
Ambas representaciones podían permanecer asignadas. Estas cifras no miden la
RAM total ni la sobrecarga opaca del controlador. No se midió fluidez continua
al millón; la prueba comprueba funcionamiento en la escena declarada.

## Dibujo quieto comprobado con el contador GL

La prueba adicional lee el contador real del renderizador en el hilo GL sin
solicitar un dibujo al inspeccionarlo:

| Momento | Contador inicial | Contador posterior |
| --- | ---: | ---: |
| Quieto durante 600 ms | 563 | 563 |
| Tras elegir y elevar un objeto | 563 | 566 |
| Animación reactivada | 680 | 683 |
| Quieto de nuevo durante 600 ms | 686 | 686 |

También pasaron la imagen A/B y un benchmark completo con el campo quieto.
La suite mantiene los recorridos de circuitos, memoria, persistencia,
recuperación GL, importación, escala y 65.536 objetos con zoom máximo.

- [Contadores, imagen y benchmark quieto](xlab_millon_2026-10-18/xlab-objetos-demand.json).
- [Los 19 recorridos](xlab_millon_2026-10-18/normal-instrumentation.txt).
- [Firma y conservación del payload probado](xlab_millon_2026-10-18/XLab-Objetos-0.3.2-firma.json).

## Comprobación independiente de todos los IDs

Una captura de transform feedback en llvmpipe comparó **las 1.048.576
transformaciones e IDs** con la CPU, en las dos rutas: cero diferencias,
incluido el ID **1.048.575**. Semilla 2026, sin excepciones. Esta prueba usa
OpenGL por software en el host, no Android ni una GPU física.

[Resultados completos de transform feedback](xlab_millon_2026-10-18/host-transform-feedback.json).

## Límites que se conservan

El millón se carga con **Objetos ↗ → 1.048.576 objetos → Nueva escena**. La
campaña automática puede parar antes por P95 ≥ 100 ms, memoria, temperatura
o duración de 60 segundos. El selector manual conserva sus comprobaciones
preventivas: al millón estima 72 MiB y necesita al menos 144 MiB disponibles,
además de no estar en estado de poca memoria ni temperatura severa.

Estos tamaños son estimaciones, no medidas del pico de memoria del controlador.
Los cambios persistentes individuales siguen limitados a 4.096 excepciones.
El ensayo gráfico no incorpora física de colisiones por objeto y no acredita
computación cuántica física. La fluidez de cada teléfono requiere medición allí.

[Guía de uso e instalación](../docs/xlab_0_3_objetos.md).
