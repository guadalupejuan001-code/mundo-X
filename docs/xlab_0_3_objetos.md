# XLab 0.3 — objetos persistentes y escala de renderizado

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
El formato canónico tiene importación/exportación comprobada en el modelo; esta
versión no añade un selector de archivos de importación a la interfaz.

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

Android 8+, OpenGL ES 3, ARM64 y x86-64. Es una compilación debug del paquete
`com.mundox.lab.xlab.objects`, etiquetado **XLab · Objetos**, que se instala
junto a XLab 0.2 sin borrarlo. Las claves debug de CI pueden cambiar entre ejecuciones,
por lo que una instalación sobre otra compilación puede rechazar la firma.
Los datos del usuario no se borran automáticamente para resolver ese rechazo.

Las comprobaciones puras Java cubren vectores de hash independientes,
reversibilidad, edición aislada, snapshots inmutables, importación atómica,
persistencia, límites y selección. Las pruebas Android añaden lectura real de
framebuffer, cambios de imagen, cancelación y restauración durante la escala.
Un emulador con SwiftShader verifica funcionamiento y límites del ensayo; sus
tiempos no son resultados de la GPU física de un teléfono.
