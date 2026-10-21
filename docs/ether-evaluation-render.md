# Ether: evaluación algebraica y recorrido gráfico · 20 de octubre de 2026

## Referencia y prioridad

Este trabajo extiende la compactación de reglas al cálculo de consultas mixtas
y a la imagen producida por el motor. La prioridad es evitar trabajo repetido
y mantener acotada la RAM. El tamaño del archivo en disco no fue un objetivo
de optimización.

La base reproducible es `f096930749a8945ebb85678ed61f8273e01a0ad7`, que contiene
la compactación previamente verificada sobre el PR #25
(`aab0930378796ebfd42f9629e2575108ca94fcff`). `main` permanecía en
`7b028db2344912b2ab86a0affa2a1fe56180922b`. Se comprobaron las 35 ramas al
comenzar y nuevamente durante el cierre: no aparecieron nuevas cabezas. El PR
#25 seguía abierto como borrador; no se ha fusionado ni publicado esta rama.

Rama local: `codex/ether-evaluation-render-20261020`.
Código nativo: `6baf22bed1f8b964de2ba24bb702c6a9a3b789d4`.
Cambio gráfico: `767d95352cbf5f76c91ce0049d510cfeb96d9dc1`.
Las fuentes ejecutadas tienen hashes en los JSON de resultados. La procedencia
nativa se comprueba contra ese commit inmutable, sin relajar el verificador.

## Qué cambia en el cálculo

Antes, los productos mixtos podían evitar las capas solamente con hasta 16
sitios iniciales y una cota conservadora del soporte de todas sus imágenes.
Las demás consultas volvían a recorrer las capas. Los intervalos que después
se cancelaban podían llegar a expandirse primero.

Ahora cada imagen de X o Z se mantiene como dos intervalos de paridad. Se
combinan sus extremos y **se expande únicamente el resultado combinado**. Esto
funciona también cuando las imágenes individuales tienen un interior enorme
que desaparece al combinarlas. No hay una caché de respuestas ni una selección
aproximada de términos.

Los signos se conservan mediante `i^fase (producto X)(producto Z)`. Al multiplicar
una imagen se añade `2*(Z_acumulado · X_entrante)` módulo cuatro. Un índice de
sumas XOR calcula la paridad de esa intersección a partir de los extremos,
sin enumerar los puntos interiores. En una subretícula, con coordenada t, la
paridad de cobertura antes de t es `XOR_e<t (t-e mod 2)`; basta almacenar la
paridad del número de extremos y de sus coordenadas. Esto incluye las fases
de Y, los signos negativos y las cancelaciones entre sitios.

Con k términos iniciales, D dígitos por extremo y P términos de salida, la
preparación usa O(k D) memoria y O(k log(k) D) trabajo; la salida explícita y su
mapa cuestan O(P D) memoria y O(P log(P) D) trabajo. La profundidad lógica
interviene en el tamaño de las direcciones, pero ya no se recorre capa a capa
en esta ruta. El entero que cuenta las reglas continúa siendo `size_t`.

Para secuencias de menos de 32 capas se conserva la propagación sencilla;
los generadores X/Z individuales conservan su fórmula directa desde ocho
capas. La selección evita construir un índice innecesario para cargas cortas.
No impone un nuevo máximo de términos admisibles.

También se eliminaron dos estructuras redundantes: Ether valida direcciones
y duplicados en el mismo mapa de entrada; InfiniteCore deja de construir otro
mapa de entrada que la ruta con reglas no utilizaba. Las identidades se retienen
hasta completar la validación, para seguir rechazando sus alias duplicados.

## RAM y significado de los presupuestos

El límite de sitios se comprueba en la entrada no identidad y en la salida
materializada. Los extremos simbólicos no son qubits materializados. Una
dirección de extremo exclusiva puede tener un dígito adicional sin que exista
un sitio de salida con esa longitud. Los sitios finales sí conservan la
comprobación de dígitos; una consulta puede ahora caber cuando sus imágenes
individuales se cancelan antes de expandirse.

`last_query_peak_sites` describe soporte explícito, no bytes de los índices
simbólicos ni RSS. Por eso se miden por separado las asignaciones a `new`, su
pico simultáneo y el RSS del proceso. Algunos productos pequeños necesitan
unos KiB adicionales para el índice aunque ahorren muchas reservas acumuladas.
No se afirma una reducción universal del pico de RAM.

Cada proceso de la campaña tiene **2 GiB de espacio virtual como límite duro**.
El RSS es la memoria residente observada. Son magnitudes distintas; este
perfil de anfitrión tampoco constituye un límite global de RAM de la APK.

No se cambiaron las firmas C/JNI, la estructura del estado, MXI1/MXI2, el RNG ni
los presupuestos de producción de InfiniteCore. El texto de algunos errores
de entrada con reglas procede ahora del validador de Ether; su rechazo y tipo
de error se conservan. El banco del EtherCore aislado fija explícitamente
65.536 sitios de trabajo para investigar salidas mayores que las admitidas
por la configuración predeterminada de la sesión.

## Qué cambia en los gráficos

Se conserva el modelo de ojo: el rayo visita celdas y reconstruye los objetos
que necesita consultar. Antes de calcular hash, excepciones, animación y
coeficiente de Ether, ahora comprueba si puede atravesar la huella XZ de la
columna. Una columna cuya huella no intersecta el rayo tampoco puede producir
la intersección tridimensional. La prueba completa original sigue decidiendo
el impacto, su normal y el color.

Para escenas sin excepciones, la altura máxima de la regla antes del
coeficiente es `(.7 + 255/128) * 1.08 = 2.9075625`. Se usa la cota superior
2.908, en lugar de la cota 5.18 necesaria cuando hay excepciones. El factor
`1 + abs(fuerza)` conserva la cota para todos los coeficientes permitidos. Esto
reduce las celdas recorridas por los rayos que atraviesan espacio vacío.

No se añadieron arrays por objeto, niveles de detalle, poda visual ni texturas
de resultados. Resolución, hash, geometría, movimiento, materiales, IDs y
selección conservan su definición. Los shaders de escritorio y Android son
idénticos. Graphics, COSMIC y el visor PC que importa esos recursos reciben
el cambio al reconstruirse. Solar usa otro shader: no se le atribuye esta
aceleración de renderizado.

Se descartó una reescritura del multiplicador de bytes del hash porque las
mediciones no mostraron una ventaja sostenida clara. También se descartó usar
el índice de intervalos en palabras cortas: el coste temporal adicional de
memoria no compensaba de forma suficiente.

## Resultados medidos

| Carga nativa | Antes, ms | Después, ms | Cociente antes/después |
| --- | ---: | ---: | ---: |
| 1.000 consultas de 8 sitios, 2 capas | 2.385 | 2.018 | 1.18× |
| 1.000 consultas de 64 sitios, 2 capas | 26.772 | 19.948 | 1.34× |
| 1.000 consultas de 8 sitios, 8 capas | 7.069 | 7.040 | 1.00× |
| 1.000 consultas de 64 sitios, 16 capas | 107.004 | 105.723 | 1.01× |
| 10 consultas de 8 sitios, 128 capas | 1.879 | 0.338 | 5.56× |
| 10 consultas de 64 sitios, 512 capas | 154.099 | 1.923 | 80.11× |
| 3 consultas de 32 sitios, 256 capas, direcciones de 512 dígitos | 22.705 | 1.227 | 18.50× |
| Lote de sesión: 128 consultas de 32 sitios, 128 capas | 111.836 | 8.393 | 13.33× |
| 2 consultas de 64 sitios, 4.096 capas | 2435.241 | 2.427 | 1003.50× |
| 2 consultas de 128 sitios separados, 128 capas | 428.215 | 9.261 | 46.24× |

Los casos cortos de ocho y dieciséis capas quedan próximos al tiempo anterior;
la mejora principal aparece al evitar recorridos profundos.

| Render por rayos, doce vistas variables | Antes, ms | Después, ms | Cociente |
| --- | ---: | ---: | ---: |
| 320×180; 1,024 objetos | 12.17 | 6.11 | 1.99× |
| 960×540; 1,048,576 objetos | 198.57 | 83.35 | 2.38× |
| 1920×1080; 1,048,576 objetos | 798.53 | 308.27 | 2.59× |

| Recorrido completo con sesión viva | Antes, ms | Después, ms | Cociente |
| --- | ---: | ---: | ---: |
| 320×180; 1,024 objetos | 93.75 | 37.79 | 2.48× |
| 960×540; 1,048,576 objetos | 830.36 | 591.22 | 1.40× |

**31.795.200 píxeles** comparados directamente entre shaders, sin diferencias.
**22 imágenes completas** adicionales del recorrido con Ether coinciden por SHA-256,
junto con todos sus coeficientes y el estado final serializado.

| Frontera del EtherCore aislado | Tiempo, ms | Pico RSS, MiB | Salida explícita |
| --- | ---: | ---: | ---: |
| 4,096 capas; 256 términos; 4 dígitos iniciales | 1.45 | 2.27 | 4,289 términos |
| 4,096 capas; 1,024 términos; 4 dígitos iniciales | 3.05 | 2.52 | 5,057 términos |
| 4,096 capas; 4,096 términos; 4 dígitos iniciales | 10.37 | 4.26 | 8,129 términos |
| 4,096 capas; 4,096 términos; 512 dígitos | 51.36 | 32.34 | 8,192 términos |
| 4,096 capas; 4,096 términos; 4096 dígitos | 317.53 | 224.12 | 8,192 términos |
| 1,000,000,000,001 capas; 4,096 términos; 4 dígitos iniciales | 11.46 | 4.38 | 10,827 términos |

En el banco, el parámetro de dígitos es un ancho mínimo: el valor 1 deja
las direcciones normales (2.000–6.095 en estas fronteras), de cuatro dígitos.

El caso de direcciones de 4.096 dígitos reservó **198.674.612 bytes temporales
simultáneos** a `new`; el RSS fue aproximadamente **224,12 MiB**. La descripción
de las capas sigue siendo constante; domina la longitud de las direcciones
y de sus intervalos y resultados explícitos.

En las dos consultas de 64 sitios y 4.096 capas, las reservas acumuladas
bajaron de **308.885.584 a 1.058.202 bytes** y el pico temporal de
**463.072 a 458.976 bytes**. La ganancia principal de esa carga es tiempo
y tráfico de asignaciones; el pico simultáneo ya estaba dominado por la salida.
En diez consultas de 64 sitios y 512 capas, el índice eleva el pico temporal
de 68.608 a 79.110 bytes a cambio de evitar el recorrido por capas.

El máximo RSS de la comparación de shaders fue **132,40 MiB**;
el recorrido completo de un millón de objetos terminó en aproximadamente
**107,95 MiB** después del cambio. Las pequeñas diferencias de RSS entre
versiones del recorrido completo no acreditan una reducción de RAM gráfica.

Los tiempos nativos son medianas de cinco repeticiones alternadas, mismo
compilador, carga, CPU fijada y condiciones. Cada consulta del bucle cambia
sus direcciones. La comparación de shaders alterna ambas versiones sobre
las mismas doce vistas con semillas, excepciones, observaciones, cámara y
tiempo variables; excluye el primer dibujo de la mediana. Las medianas de
recorrido completo abarcan ocho fotogramas distintos por versión, incluyendo
puerta, consulta, sincronización, dibujo y lectura de píxeles.

La GPU disponible es **Mesa llvmpipe por software**. No son resultados de FPS
en el Samsung ni de una GPU física. En particular, un cociente del núcleo no
se extrapola a la imagen completa.

## Corrección y cobertura ejecutada

- **73/73 CTest**: núcleo, sesión, puente C, física de escena y PC.
- **799.838 comprobaciones** en `ether_rules`, con semilla 42: oráculo finito
  independiente, productos de hasta 128 sitios, fases Y, signos, bordes,
  direcciones largas, límites, palabras enormes e inversas compactas.
- El mismo ensayo pasa con AddressSanitizer y UndefinedBehaviorSanitizer.
  LeakSanitizer está desactivado; no se presenta como prueba de fugas.
- **4.000 comparaciones** contra la versión anterior, incluidas 2.000 consultas
  de direcciones largas: cero diferencias de términos o signos.
- Estado serializado, restauración, mediciones y continuación de RNG idénticos
  byte a byte a profundidades 0, 8, 31, 64 y 4.096.
- Las comparaciones gráficas incluyen color, IDs, cámara horizontal, vista
  general, excepciones y coeficientes. El recorrido completo usa una sesión
  Ether viva, 127 capas y 65 observables, de los cuales 64 tienen 32 términos.
  Una puerta cambia los píxeles a cámara fija; la observación no modifica el
  estado; la restauración invalida la caché y conserva la imagen.
- **914 rayos** contra una referencia geométrica independiente: cero
  discrepancias. También se comprueban el primer y último objeto de escenas
  de 1.000.000, 1.000.003 y 1.048.576 objetos; selección, ocultación y
  restauración; y 5.000 excepciones. La referencia raster tiene 3, 6 y 11
  píxeles distintos en las tres vistas, por los bordes ya conocidos; el oráculo
  independiente coincide con la ruta de rayos.

Fuentes modificadas: `ether_core.hpp`, `infinite_core.hpp`, `ether_rules.cpp`
y los dos `eye.frag`. Se revisaron también el contrato de `infinite_session`,
el observador Pauli, CompactCore, los puentes, el adaptador Python, la
sincronización de escena, el renderer y el empaquetado de recursos Android.
No se modificaron el tableau, los modelos orbitales de Solar ni la CPU PC.

No se generó una APK ni se ejecutaron JNI Android, emulador Android o dispositivo
físico en esta entrega. Esas verificaciones siguen pendientes de su entorno.

## Límites restantes y siguiente cambio estructural

1. **Salida explícita y direcciones largas.** Aunque la composición sea compacta,
   la API de pullback entrega cada término. El siguiente paso sería permitir
   que los consumidores operen sobre intervalos sin convertirlos antes en un
   mapa de sitios; debe conservar fondo Bell, excepciones y signos.
2. **Componentes finitos.** La materialización sigue usando CompactCore y sus
   tableaux; el coste de estados activos arbitrarios no desaparece. Los
   presupuestos de componentes y qubits activos de producción permanecen.
3. **Sesión.** Las operaciones siguen serializadas por el mutex. Lectores con
   instantáneas inmutables y trabajo privado pueden estudiarse por separado,
   contabilizando la suma de RAM de todos los evaluadores concurrentes.
4. **Imagen.** Los rayos que sí cruzan muchas columnas todavía deben resolverlas;
   el framebuffer, la lectura de píxeles y las excepciones cuestan RAM y tiempo.
   El siguiente índice espacial tendría que permitir saltos demostrablemente
   vacíos sin cambiar un píxel.

Las campañas terminan en sus objetivos registrados y presupuestos explícitos;
no se presentan como búsqueda exhaustiva del máximo físico. La prueba de
un billón de capas corresponde a una composición algebraica representada,
no a un billón de puertas ejecutadas ni a un contador de qubits físicos.

## Repetición

```bash
cmake -S . -B build-evaluation -DCMAKE_BUILD_TYPE=Release
cmake --build build-evaluation -j 3
ctest --test-dir build-evaluation --output-on-failure -j 2
python3 tools/ether_evaluation_check.py --output evaluation.json
python3 tools/ether_render_evaluation_check.py --output render-evaluation.json
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 LP_NUM_THREADS=2 python3 graphics-desktop/validate.py
g++ -std=c++17 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined \
    -I native/include native/tests/ether_rules.cpp -o ether-rules-sanitized
ASAN_OPTIONS=detect_leaks=0 ./ether-rules-sanitized
```

Los bancos necesitan Linux, Python 3, g++, el historial Git que contiene la
referencia, NumPy y EGL/GLES para la parte gráfica. Compilan ambas versiones
del núcleo desde fuentes fijadas. La compilación no se incluye en el tiempo
ni el RSS del proceso gráfico medido. El presupuesto por caso nativo es de
28 segundos de CPU y 30 de pared; por caso gráfico, 43 y 45 segundos.

Evidencia completa: [cálculo](../results/ether-evaluation-2026-10-20.json),
[render y recorrido completo](../results/ether-render-evaluation-2026-10-20.json),
[validación y referencias](../results/ether-evaluation-validation-2026-10-20/).
