# XLab 0.3.1 — objetos persistentes y recuperación de la vista

El botón **Objetos ↗**, en la cabecera de XLab, abre un ensayo nuevo del
principio de Mundo X: reconstruir una escena desde reglas y conservar las
excepciones que cambian su comportamiento. No se incorporó un motor gráfico
externo: utiliza Java y OpenGL ES 3 de Android. El núcleo cuántico existente no
se sustituyó ni se modificó para este ensayo.

## Una escena, dos representaciones

Cada objeto tiene un ID estable, una posición de cuadrícula, altura y tono
deterministas a partir de su ID y una semilla de 32 bits. Se puede elevar,
ocultar o restaurar un objeto. La escena conserva hasta 4.096 excepciones y se
guarda localmente; al volver a abrir la pantalla se reconstruyen esos cambios.
Crear una escena con otra semilla o cantidad reinicia sus excepciones.

El renderizador dibuja cubos reales de 12 triángulos con profundidad,
iluminación y niebla. Ambas rutas usan la misma malla, cámara, resolución,
shader y cantidad de instancias:

- **Reglas + cambios:** el shader calcula las transformaciones a partir del ID.
  Busca las excepciones en una textura de pares enteros ordenados.
- **Explícita:** un buffer conserva cuatro floats por objeto: posición X/Z,
  altura y tono. Se prepara una vez por escena, no en cada fotograma.

No son objetos con colisiones o dinámica física individual. La rotación y el
campo de alturas son funciones analíticas compartidas por ambas rutas. El
ensayo de representación no utiliza CompactCore para generar su geometría y
no acredita aceleración cuántica. Las funciones cuánticas de XLab permanecen
en sus herramientas correspondientes.

## Controles y persistencia

Arrastrar orbita la cámara y pellizcar cambia el zoom. Tocar busca una
intersección del rayo con los cubos visibles; también se puede elegir un ID
numérico. Los objetos ocultos se recuperan por ID y **Revertir**.

**+256 cambios** añade excepciones nuevas repartidas por la escena sin
sobrescribir las anteriores. **Cambiar todos** permite ensayar densidad completa
en escenas de hasta 4.096 objetos. El límite no expulsa cambios previos.
**Limpiar** restaura la regla original.

**Copiar informe** y **Compartir** exportan la semilla, el texto canónico de la
escena, los resultados de comprobación y las mediciones. No hay envío automático.

### Restaurar una escena copiada

**Restaurar escena** lee el portapapeles. Acepta el informe JSON completo de
**Copiar informe**, del que extrae el campo `scene`, o directamente el texto
canónico exportado por el modelo. Importa semilla, cantidad y excepciones; no
incorpora las mediciones ni el historial del informe.

La cantidad debe corresponder a uno de los perfiles del selector y superar las
comprobaciones de recursos del dispositivo. El texto se valida antes de cambiar
el modelo: si está mal formado, el perfil no es compatible o faltan recursos,
la escena actual se conserva. La importación se hace desde el portapapeles;
no hay un selector de archivos.

## Recuperar la vista y reconocer un dibujo vacío

Un error durante el dibujo lo suspende y muestra un aviso. **Recentrar /
recuperar** restaura el encuadre inicial y reintenta el dibujo; cambiar entre
**Reglas + cambios** y **Explícita** también permite reintentarlo. Ambas acciones
conservan la escena y sus excepciones. **Nueva escena** vuelve a intentar el
dibujo con la escena regenerada y reinicia sus cambios, como se indica arriba.
Si OpenGL no pudo iniciarse, hay que volver a abrir el laboratorio.

El plano cercano de la cámara se ajusta a su distancia:
`near = max(0.1, distancia × 0.01)`. Así se evita mantener un plano cercano
demasiado pequeño al alejarse sobre cuadrículas grandes, que reduce la precisión
de profundidad, especialmente con buffers de 16 bits.

La banda sobre la vista informa **comprobando**, **dibujo visible**, **sin objetos
visibles** o **dibujo detenido**. Refleja la última consulta de visibilidad GPU
disponible; los cambios de cámara, escena o ruta solicitan otra consulta. Detectar
muestras visibles no demuestra que todos los objetos estén en pantalla ni que
el renderizado sea rápido. Esta consulta se hace fuera de las muestras de la
comparación A/B y no sustituye a **Imagen A/B**.

El informe conserva `last_graphics_error`, incluso después de recuperar el
dibujo. Cuando hay estadísticas disponibles, añade `depth_bits`,
`projection_near`, `projection_far`, `camera_distance`, `viewport_width`,
`viewport_height`, `visibility` y `draw_suspended`. Estos campos permiten
relacionar el error con la profundidad, el encuadre y la ruta ensayada.

## Comprobación de imagen

**Imagen A/B** renderiza ambas rutas a tiempo fijo en un framebuffer RGBA8 de
128 × 128 con profundidad. Lee los píxeles reales con `glReadPixels`, exige
al menos 16 píxeles de objetos y compara cada canal con tolerancia de 2 niveles
de 255. Informa diferencias, cobertura, hash de ambas imágenes y huella de escena.

Una imagen vacía no pasa esta comprobación, incluso si ambas rutas coinciden.
El resultado sólo comprueba la cámara, semilla y cambios ensayados; no es una
demostración universal de equivalencia. La instrumentación también contrasta
la imagen antes/después de editar para impedir que se ignoren las excepciones.

También se capturan las transformaciones producidas por el mismo shader para
hasta 1.024 objetos y se comparan con la CPU. La mezcla entera del shader usa
productos por bytes y acarreos para conservar el resultado módulo 2³² en
traductores GLES antiguos. El algoritmo y las semillas de la CPU se conservan.

## Medición y escala extrema

**Medir A/B** exige primero una comprobación de imagen y transformaciones
correcta; si falla, rechaza la comparación. Congela escena, cámara y tiempo.
Cada ruta recibe 700 ms de
calentamiento y 2.500 ms de muestra. Informa número de muestras, P50/P95 de
los intervalos entre fotogramas y tiempo de CPU enviando el dibujo.

Estos intervalos incluyen presentación, sincronización y planificación.
El tiempo de envío de CPU no es tiempo de ejecución GPU. No se usan consultas
de temporización GPU, y no se informa utilización GPU inventada. La preparación
inicial de la referencia y los uploads se realizan fuera de las muestras.
El orden es reglas y después explícita: temperatura y orden pueden influir.
Al menos tres muestras por ruta son necesarias para completar la medición.

**Escala extrema** recorre 1.024, 4.096, 16.384, 65.536, 262.144 y 1.048.576
objetos. El máximo representa 12.582.912 triángulos enviados por fotograma;
los objetos pueden ocupar menos de un píxel, ser tapados o quedar fuera del
encuadre. No se afirma que un millón de objetos sea fluido en cualquier equipo.

La escala termina cuando:

- completa todas las etapas;
- una ruta alcanza P95 de 100 ms o más;
- Android informa poca memoria o temperatura severa;
- la estimación preventiva de memoria supera 30 % de RAM total o 50 % disponible;
- transcurren 60 segundos o el usuario pulsa **Parar**.

El renderizador también abandona una medición cuando retorna un fotograma
de más de 1.000 ms. Son límites cooperativos: no interrumpen una llamada bloqueada
del controlador gráfico. Al terminar o salir de la pantalla se restaura la
escena anterior a la escala, con sus cambios. El informe conserva las etapas
terminadas y el motivo de parada. La selección manual también comprueba
presupuesto antes de aumentar la cantidad.

## Qué significan los bytes

El formato lógico de regla y cambios cuenta `32 + 8 × excepciones` bytes.
La referencia de cuatro floats requiere `16 × objetos` bytes. Esta referencia
no pretende ser la mejor codificación alternativa posible: posiciones y tonos
siguen teniendo regularidad aunque cambie la altura de todos los objetos.

La textura dispersa usa pares RG32I con posible relleno de filas. El informe
separa su carga asignada, el VBO explícito y los 864 bytes de malla compartida.
Después de comparar, ambas representaciones pueden seguir asignadas; cambiar
de ruta por sí solo no libera la referencia. Una escena nueva invalida su VBO.
Las estructuras Java, buffers temporales y sobrecarga del controlador quedan
fuera de esos tamaños lógicos. El PSS medido corresponde al proceso Android;
no se presenta la descripción lógica como toda la RAM o memoria GPU utilizada.

## Instalación y validación

Android 8+, OpenGL ES 3, ARM64 y x86-64. La versión 0.3.1 utiliza el paquete
`com.mundox.xlab` y la etiqueta **XLab · Objetos+**. Es una compilación debug
de laboratorio. Se instala en paralelo a
0.3, cuyo paquete es `com.mundox.lab.xlab.objects`, para conservar sus datos y
evitar exigir una actualización sobre sus firmas debug anteriores, que podían
cambiar entre compilaciones. No borra ni migra automáticamente los datos de
la instalación anterior.

Para trasladar una escena: abre 0.3, pulsa **Copiar informe**, abre **XLab ·
Objetos+** y pulsa **Restaurar escena**. Conserva la instalación anterior hasta
comprobar la escena restaurada.

La entrega oficial usa la firma persistente del proyecto:
[XLab-Objetos-0.3.1.apk](https://github.com/guadalupejuan001-code/mundo-X/releases/download/v0.3.1/XLab-Objetos-0.3.1.apk).
[Validación: 18 recorridos Android y recuperación del dibujo](../results/xlab_0_3_1_2026-10-18.md).
La vista previa debug de CI puede usar una firma temporal; la entrega oficial
conserva el mismo código y payload de aplicación, con la firma persistente.

Las comprobaciones puras Java cubren vectores de hash independientes,
reversibilidad, edición aislada, snapshots inmutables, importación atómica,
persistencia, límites y selección. Las pruebas Android añaden lectura real de
framebuffer, cambios de imagen, cancelación y restauración durante la escala.
Un emulador con SwiftShader verifica funcionamiento y límites del ensayo; sus
tiempos no son resultados de la GPU física de un teléfono.
