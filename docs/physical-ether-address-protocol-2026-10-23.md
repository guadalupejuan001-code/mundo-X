# Ether: dirección física operativa y decodificación mediante inferencia

**Estado:** prototipo computacional + prueba sintética aprobada.
**Versión base:** main @ 92d0113acec83f5e5a91e906171a821fe3625f16
(incluye Ether InverseMemory copy-on-write en 256 páginas).

## 1. Qué significa «dirección física»

En el software de Mundo X, una dirección lógica puede consultarse sin
almacenar todo el registro. No existe evidencia de que una posición del
universo sea una dirección digital equivalente. La correspondencia que
investigamos es una **dirección de medición operativa**:

    A = (frame, x_m, y_m, z_m, time_s, observable, resolution_m)

- frame: sistema de referencia definido y calibrado por el experimentador.
- (x,y,z): coordenadas del sensor en metros en ese sistema de referencia.
- time_s: instante de medición relativo a una sesión, en segundos.
- observable: magnitud física consultada (esta etapa: campo magnético
  vectorial o magnitud del campo).
- resolution_m: resolución espacial nominal de la evidencia.

No es una posición absoluta en el universo, una llave para extraer datos de
la naturaleza, ni una prueba de que exista una lattice discreta física.

## 2. Qué hace esta versión con el Éter original

El nuevo adaptador se encuentra en:

    native/include/mundo_x/physical_ether_address.hpp

Utiliza el **QuantumRuntime original** de Mundo X, que contiene la
**InverseMemory original optimizada en 256 páginas copy-on-write**.
No incorpora otro motor de memoria ni copia los datos a una malla.

Cada observación tiene 7 números binarios IEEE-754 exactos:
x,y,z,t,Bx,By,Bz. Se codifican en el almacenamiento lógico de Ether, por
byte, junto a un encabezado y un marcador de muestra. El registro
completo se publica como una sola transacción XI, evitando observaciones
a medio escribir. Hay un límite explícito de 128 muestras.

A petición, el decodificador relee los bytes canónicos, ajusta seis
parámetros de un modelo físico y calcula a demanda nuevas consultas:

    B(r) = B0 + 3*r*(m·r)/|r|^5 - m/|r|^3

Aquí B0 es un campo aproximadamente uniforme; m es un momento dipolar
efectivo en microteslas·m³. El origen del frame coincide con el centro
supuesto del imán. Se excluye la singularidad del centro (r < 5 cm).

**Supuestos**: fuente dipolar puntual, campo estático en la ventana de
calibración, sensor y posiciones calibrados, y ausencia de otras fuentes
significativas. La ecuación no es un modelo completo de la realidad ni
determina automáticamente fuentes desconocidas.

El ajuste opera sobre observaciones escasas y seis parámetros, no sobre
todas las direcciones del espacio. Las consultas no mutan Ether, ni
materializan qubits, ni crean voxeles. El método rechaza geometrías
no identificables, coordenadas fuera de calibración, referencias
equivocadas y resoluciones no respaldadas.

## 3. Prueba reproducida en CI

Test C++: native/tests/physical_ether_address.cpp
Importador CSV: native/src/physical_ether_lab.cpp
Prueba sintética: examples/physical_ether_dipole_synthetic.csv
Plantilla para mediciones reales: examples/physical_ether_real_template.csv

    cmake -S quantum-runtime -B quantum-build -DCMAKE_BUILD_TYPE=Release
    cmake --build quantum-build --target test_physical_ether_address mundo_x_physical_ether_lab -j 2
    ctest --test-dir quantum-build -R physical_ether --output-on-failure

Ejemplo de consulta operativa:

    ./quantum-build/mundo_x_physical_ether_lab \
      examples/physical_ether_dipole_synthetic.csv 0 0 .24 2

El CSV distingue TRAIN y HOLDOUT. El programa ajusta únicamente TRAIN,
reserva HOLDOUT para evaluación y muestra el error del modelo comparado
con un campo uniforme aprendido del mismo TRAIN.

Cada fila representa una posición distinta: promedia por separado las lecturas
repetidas de un sitio antes de importarlas. Se requieren al menos 8 sitios TRAIN
y 4 HOLDOUT (se recomiendan 8 HOLDOUT). Dos posiciones deben estar separadas por
al menos `resolution_m` metros, tanto dentro de cada grupo como entre grupos.
Cambiar el tiempo o desplazar coordenadas menos que esa resolución no convierte
un sitio de entrenamiento en una validación espacial independiente. Estos
controles no certifican el origen físico de un archivo ni la independencia
estadística de las lecturas.

La regresión de la interfaz CSV se ejecuta con:

    python3 tools/test_physical_ether_cli.py ./quantum-build/mundo_x_physical_ether_lab

Incluye datos repetidos, solapamiento TRAIN/HOLDOUT, ruido de posición inferior
a la resolución, controles uniformes y de fuente cambiada, y entradas inválidas.
Una consulta inválida termina con error y sin publicar un JSON parcial.

Los resultados sintéticos aprobados en el workflow de GitHub incluyen:

- 16 posiciones de entrenamiento y 8 independientes.
- Más de 2.000 comprobaciones en la prueba C++, incluidos los rechazos anteriores.
- RMSE de las ocho posiciones: ~0,0187 µT (prueba C++);
  importador CSV independiente: ~0,0226 µT por diferencias de orden
  de ruido generado en el ejemplo.
- Modelo constante de control: ~4,57 µT de RMSE.
- Fuente cambiada: ~7,70 µT de RMSE en el ensayo independiente.
- 1.000 consultas de puntos distintos sin nuevos bytes en InverseMemory.
- Ningún qubit activo creado por mediciones clásicas de campo.
- Rechazo de sensores NaN, geometrías degeneradas y snapshots de
  otra referencia sin modificar el estado previo.
- Ensayo negativo de campo uniforme: no se declara evidencia dipolar.

**Esos valores son pruebas de software con señales sintéticas, NO
mediciones del espacio físico ni evidencia de una lattice universal.**

## 4. Primer ensayo físico con Galaxy Watch4

El Watch4 incluye un magnetómetro; su telemetría WorldLink previa
ya enviaba mx,my,mz, pero la pantalla presentaba la magnitud y
todavía NO exporta a este importador posiciones calibradas ni el
vector convertido a un sistema de referencia fijo.
La captura `mundox.hardware-probe.v1` de MundoX GHZ 1.1 registra vectores y
tiempos del sensor, pero tampoco incluye posiciones medidas ni una orientación
de referencia calibrada. Su JSON no puede convertirse automáticamente en este
ensayo espacial; hacen falta esas mediciones adicionales.

Para obtener evidencia físicamente interpretable:

1. Deja un imán pequeño y ordinario inmóvil en el origen de una hoja
   milimetrada o estructura con distancias conocidas. No acerques
   imanes a dispositivos médicos implantados, medios magnéticos
   sensibles ni sistemas que requieran calibración.
2. Usa el reloj quieto, con **la misma orientación en cada muestra**,
   sin movimientos de muñeca ni variaciones de inclinación. Mide
   x,y,z con regla en metros. Si el reloj cambia de orientación,
   hay que transformar la lectura vectorial al frame mediante
   una rotación de referencia calibrada; el mero giroscopio no basta
   para resolver sin deriva toda la orientación absoluta.
3. Exporta los **tres componentes** del campo Bx,By,Bz en µT; no basta
   el valor escalar |B| mostrado por la interfaz vieja.
   Por ahora eso requiere ampliar la exportación de WorldLink o un
   lector de sensores independiente compatible con Wear OS.
4. Reserva antes del ajuste al menos 8 posiciones distribuidas por el
   volumen como HOLDOUT. Obtén al menos 8 TRAIN distribuidas en 3D,
   preferentemente 16 o más.
5. Registra tiempos relativos, distancias y resolución. Los puntos
   de prueba deben estar dentro de la región espacial y temporal
   de calibración (el prototipo tolera hasta 15 s fuera del rango
   temporal de muestras TRAIN; el modelo sigue siendo estático).
6. Evalúa primero con imán fijo. Después retira/mueve el imán y
   **predice de nuevo sin reentrenar**: el error debería crecer.
   Repite con controles sin imán; la variación de la orientación
   del Watch4 no debe confundirse con cambio de campo.
7. Comparación predeclarada: el RMSE HOLDOUT debe mejorar al menos
   un 20 % y 0,15 µT frente al control constante. Reporta también
   mediciones fallidas o no disponibles.

## 5. Qué aceptaremos y qué no

**Aceptaremos** que una representación compacta de Éter puede inferir
y anticipar un campo físico particular, dentro de las hipótesis y el
dominio medido, cuando datos físicos independientes lo demuestren.

**No aceptaremos** que el resultado pruebe que toda la realidad
funciona como un registro lógico, que cada posición es un qubit,
que exista acceso causal a una lattice universal o que una
simulación cuántica esté entrelazada con materia exterior.

Siguiente desarrollo condicionado por los datos: agregar un adaptador
de exportación vectorial con marca temporal y pose calibrada; registrar
las fallas de modelo; comparar otras hipótesis físicas. No se genera
una nueva APK ni una escena gráfica en esta etapa.
