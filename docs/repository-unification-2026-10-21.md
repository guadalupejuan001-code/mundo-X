# Consolidación de Mundo X: estructura, rescates y evidencia

**Corte de revisión: 21 de octubre de 2026.** La base recuperada es
`52150dfa928c088a32e69ce32f53c1e5a3b558c3`. La integración se reúne en
[PR25](https://github.com/guadalupejuan001-code/mundo-X/pull/25), cuyo historial
y comprobaciones permiten consultar el estado remoto posterior a este corte.
Los resultados locales de este documento identifican sus fuentes y su entorno;
una compilación Java no se presenta como una ejecución Android.

## La idea que guía la integración

La dirección de X es conservar un mundo mediante reglas, identidades y cambios,
y reconstruir lo que necesita observar el ojo. Ether y el renderer propio
siguen siendo piezas centrales. Los espejos consultan ese mismo mundo; las
repeticiones geométricas que se pueden resolver mediante composición no necesitan
una copia de la escena por rebote. El presupuesto de referencia es 2 GiB por
proceso en las campañas acotadas, midiendo también el trabajo y la memoria
temporal que necesita cada consumidor.

La representación cuántica compartida es `Ψ = U_Ether φ`: el marco de reglas
globales y las modificaciones finitas describen una autoridad por sesión. Las
puertas, las consultas y los snapshots se refieren al mismo estado. El
renderizador recibe valores y metadatos de una observación coherente, mantiene
esas entradas durante el fotograma y aplica sus reglas geométricas. La lectura
conserva el estado; una puerta puede modificar la imagen de forma causal.

El alcance real por componente está en la [revisión del núcleo](repository-core-review-2026-10-21.md).
Las APK son procesos separados. Compartir fuentes canónicas entre ellas no crea
una única instancia viva que abarque todos los procesos. La RAM y CPU del PC,
los modelos orbitales, los cuerpos de la física y los buffers GLES conservan sus
representaciones propias. La física 3D recuperada tiene un consumidor nativo real;
sus cuerpos todavía no se codifican en Ether ni sustituyen las órbitas de Solar.

## Qué se reunió y qué quedó superado

La [matriz completa](repository-branch-disposition-2026-10-21.md) inventaría
**35 ramas y 25 PR**, con SHAs y procedencia. En la base auditada, 24 heads ya
estaban incluidos por ascendencia. Las otras 11 ramas contienen 70 commits fuera
de ella, entre cambios absorbidos, entregas históricas y aportes pendientes. Ese
número no equivale a 70 funciones nuevas.

| Procedencia | Integración útil | Disposición de la versión antigua |
| --- | --- | --- |
| PR19, ENTER/LEAVE del PC | El arreglo nativo ya estaba integrado. Se recupera la ISO sintética y su regresión Android; exige que ENTER consuma una instrucción y valida el marco y LEAVE. | La instrumentación antigua completa perdería contratos Ether incorporados después. Se conserva sólo el caso pendiente. |
| PR20, Infinite extremo | GHZ 1024, consultas por lote, 4096 direcciones de 1001 dígitos, 65.536 puertas, límites y rollback. | La caja scissor antigua sólo cubre la escena directa. No acredita la cobertura de reflejos y no se usa para recortar la escena actual. |
| PR21, física por reglas | `RulePhysics` 3D, validación, pasos transaccionales, presupuestos, índice conservador y CLI `physics3d`. | El vector de cuerpos mutable, la conversión de subpasos fuera de rango y el paso parcialmente publicado se corrigen antes de recuperar el módulo. |
| PR21, reutilización Android | Comparación exacta de todas las entradas visuales, incluidos espejos, FBO, cámara, texturas y epochs. | El hash FNV antiguo podía colisionar y no contemplaba la óptica nueva. Se recupera su propósito con igualdad exacta. |
| PR21, GHZ 1000 / 24 ZZ / cinco lotes | La carga se ejecuta mediante `ether_stress batch 1000 24`, con correlaciones y snapshot/RNG comprobados. | No se atribuye al ejecutable actual una medición antigua de aceleración scalar/batch. |
| PR22, física y Solar | La física 2D y las conexiones Ether/Solar posteriores ya están presentes. Se corrige además un fallo real de recuperación de memoria en la física 2D actual. | Se conservan las mejoras posteriores; el plan histórico con rutas obsoletas no sustituye la documentación actual. |
| PR4/5 y otras ramas integradas | Lenguajes `.mx`, `.x`, `.qx`, fases, transferencia, núcleos finitos, laboratorios, VM, Windows anfitrión y proyectos portables. | Sus fuentes actuales permanecen. Entregas binarias y disparadores antiguos de CI no se importan como actualizaciones de código. |

El cierre de los borradores se vincula a PR25 una vez publicado su reemplazo.
Los SHAs históricos del inventario permiten recuperar su procedencia.

## Correcciones de esta consolidación

### Física: un fallo de memoria no deja un índice reutilizable incompleto

La API 2D podía reservar `order_`, fallar después al preparar nodos y, en el
reintento, tratar la coincidencia de tamaños como si existiera un árbol válido.
El probe anterior terminó en **SIGSEGV (-11)**. Ahora `index_ready_` sólo se activa
al completar la construcción. La física 3D utiliza el mismo criterio, y su paso
entero se confirma al terminar todas las operaciones que pueden fallar.

La regresión final provoca fallos reales de asignación en cinco puntos 2D y
cinco 3D, comprueba que se conserva el estado público y que reintentar produce
el resultado esperado. El arreglo y los rescates nativos están fijados en
`02e69b81f18329e38d5f0e32a5a082be27bffa3c`.

### Renderer Android: conservar imágenes y ediciones con exactitud

La caché compara los bits de cada entrada que puede cambiar el dibujo. Incluye
programa GL, framebuffer, tamaño, vista, selección, semilla, tiempo, población,
coeficientes, núcleo, epoch, programa de observación, revisión de ediciones y
opciones de espejos. No reserva otra copia completa del framebuffer. El número
de presentaciones, el de dibujos reales y el de imágenes reutilizadas se registran
por separado.

Una selección ID puede escribir en el framebuffer incluso si vuelve a escoger
el mismo objeto; por eso invalida la imagen de color. Los cambios de contexto o
de FBO también invalidan. La exportación usa su destino separado. Solicitar
medición sincronizada obliga a ejecutar el dibujo, de modo que conservar una
imagen no produzca una cifra de rendimiento ficticia.

El mapa de ediciones conserva objetos temporalmente fuera de la población
visible, como exige el formato existente. La GPU recibe sólo `edits.headMap(count)`
y el número de ediciones activas. Cambiar la población vuelve a preparar ese
índice. Reducirla y restaurarla recupera las ediciones guardadas sin consultar
una textura vacía como si contuviera páginas.

La instrumentación compara capturas contra dibujos forzados con **cero píxeles
diferentes**, comprueba invalidación de doce tipos de entrada, la selección del
mismo ID, la restauración de población y la medición sincronizada. El perfil
óptico agrega cambios de reflectancia, composición, presupuesto y restauración.

### Espejos accesibles en la app principal

Graphics conserva el control **Espejos**. COSMIC añade **Motor → Escena →
Espejos del mismo mundo**, conectado al mismo `QuantumRenderer`. Se puede usar
suelo reflectante o dos espejos enfrentados. La selección de un reflejo conserva
el ID del objeto fuente. Los ajustes forman parte de la sesión privada; el
formato portable `.cosmic` todavía no incluye la óptica.

El [contrato de espejos](ether-mirrors.md) explica el algoritmo, sus condiciones
geométricas, el trabajo explícito, los marcadores de recorridos incompletos y
las comparaciones contra un oráculo óptico independiente. El CI añade esa
campaña con límite de 2 GiB y ejecuta el perfil óptico en Graphics Android.

### XLab: una comprobación estática no necesita animación continua

La ejecución antigua de XLab ya había comparado correctamente los 65.536
objetos y su vista de máximo zoom: cero píxeles diferentes en ambas capturas.
Terminó fallando al restaurar la escena en `finally`, mientras la animación y
las capturas cargaban el renderer por software; ese fallo podía ocultar una
excepción anterior.

La prueba ahora pausa la animación para su comparación a tiempo fijo, restaura
su estado anterior y conserva el error primario si también falla la limpieza.
No cambia la cobertura de píxeles ni las tolerancias, ni el comportamiento de
animación de la aplicación. El log anterior y el inventario de workflows se
conservan en los resultados de esta revisión.

### Solar: filtrado definido y una referencia explícita

El shader utilizaba derivadas implícitas dentro de ramas que no ejecutan todos
los fragmentos vecinos. La corrección calcula los gradientes del rayo, de la
superficie y de los anillos de forma explícita, y suministra esos gradientes al
muestreo de texturas. También define los casos límite de polos y tangencias.

La referencia histórica recibe la misma corrección de filtrado y sus guardas
numéricas; mantiene la evaluación completa de materiales y capas. La primera
campaña de 32 escenas fue exacta en Mesa y SwiftShader. La comparación Android
añadió un caso que esas escenas no cubrían: una imagen de Urano de 360 × 203
mostraba una banda de 53 píxeles diferentes, aunque todas las repeticiones de
cada versión coincidían.

Las entradas exactas de Android reprodujeron los mismos 53 píxeles en host.
El límite normalizado de los tiles de la referencia elegía otra fila de máscaras
en y=101; el productor utiliza tiles reales de 128 píxeles. El oráculo que evalúa
los 23 cuerpos, sin descartar ninguno por tile, coincide exactamente con la
producción: cero diferencias en los 73.080 píxeles de ese caso, también con los
materiales y nubes completos de la referencia. Por ello la referencia final
evalúa todos los cuerpos y comprueba de forma independiente el descarte de
candidatos del motor. El shader de producción conserva sus optimizaciones y
sus bytes; no se revierte su particionado correcto para imitar la referencia
antigua. El caso exacto se incorpora a la regresión.

Son pruebas de equivalencia después de corregir el filtrado y definir el
oráculo completo; no afirman igualdad con el color indefinido anterior.

SwiftShader necesitó un trabajador y calentamiento simétrico para las repeticiones
exactas. Los fallos previos, los transitorios y la sensibilidad al controlador
permanecen registrados. La [revisión Solar](solar-shader-determinism-2026-10-21.md)
incluye el parche de referencia, sus hashes, un oráculo independiente de gradientes
y las condiciones precisas de cada campaña. La ejecución Android es un gate
separado; compilar los APK o pasar GLES en host no lo sustituye.

## Evidencia local nueva

| Campaña | Resultado observado | Alcance |
| --- | --- | --- |
| CMake Release + CTest | 78/78, cero fallos y omisiones | Compilación nueva Linux x86_64; límite virtual de 2 GiB por proceso. |
| Recuperación de asignaciones | 11.730 comprobaciones; cinco fallos 2D y cinco 3D recuperados | Estado público y resultado del reintento; el SIGSEGV anterior queda documentado. |
| Física 3D | 442.462 comprobaciones; comparación con recorrido original de pares | Integrador de esferas finitas, límites y rollback; no física universal. |
| CLI, 65.536 cuerpos | Un paso real, un contacto, 24.948 KiB de RSS máximo | Carga concreta; payload de cuerpos 4.718.592 B y capacidad temporal 18.874.288 B. |
| Infinite extremo | 4.399 comprobaciones; GHZ 1024, 4096 consultas lejanas y 65.536 puertas | 17.048 KiB de RSS máximo en esa ejecución. |
| Graphics Java | 14 fuentes, 58 clases | Recursos reales API35, Java17 con compatibilidad Java8; incluye instrumentación. |
| PC Java | 14 fuentes, 47 clases | Incluye la regresión ENTER/LEAVE rescatada. |
| XLab Java | 23 fuentes, 84 clases | Incluye ambas instrumentaciones. |
| COSMIC Java | 46 fuentes, 170 clases | Incluye el menú de espejos, el renderer y las instrumentaciones compartidas. |
| Procedencia nativa | 83 fuentes verificadas byte por byte | Las fuentes revisadas tienen un pin nuevo; las no modificadas conservan su referencia. |
| Solar GLES, Mesa, referencia final completa | 32/32 escenas más el caso Android exacto; color, identidad y profundidad exactos | 5.529.600 + 219.240 píxeles entre las tres salidas; 241.568 KiB RSS máximo de las dos campañas. |
| Solar GLES, SwiftShader, referencia inicial | 32/32 escenas; 1.843.200 píxeles RGBA, cero diferencias | Un trabajador, procesos separados y repeticiones exactas tras calentamiento simétrico; candidato 723.444 KiB RSS máximo. El caso Android adicional reveló después el defecto de particionado de esta referencia. |
| Solar GLES, SwiftShader, referencia final completa | Caso Android de Urano: 73.080 píxeles RGBA, cero diferencias y repeticiones exactas | Incluye todos los cuerpos y la evaluación completa de materiales y nubes; mismo shader de producción. |
| Gradientes Solar | 896 componentes por controlador, cero fallos | Diferencias finitas independientes; máximo error absoluto observado 2,06 × 10⁻⁸. |
| APK Solar | Candidato, referencia definida, instrumentación y variante coexistente compilados | JDK17, Gradle8.9, un trabajador, JVM de 1 GiB; firma, fuentes, assets, ABI y alineación verificados. |

Fuentes: [campaña nativa](../results/repository-unification-2026-10-21/core/gate-status.json),
[revisión de física](repository-core-review-2026-10-21.md),
[Graphics](../results/repository-unification-2026-10-21/graphics-java-compile.json),
[PC](../results/repository-unification-2026-10-21/pc-java-compile.json),
[XLab](../results/repository-unification-2026-10-21/xlab-java-compile.json) y
[COSMIC](../results/repository-unification-2026-10-21/cosmic-java-compile.json), además de los
[pins](../results/repository-unification-2026-10-21/core/integrated-source-provenance.json),
la [campaña Solar](solar-shader-determinism-2026-10-21.md) y su
[procedencia de APK](../results/repository-unification-2026-10-21/solar/android-builds/solar-apk-builds.json).

CTest tuvo un problema local de permiso ejecutable en `infinite_tensor`. Se
conservan los intentos iniciales y el hash del binario; corregir sólo su modo de
ejecución permitió completar el gate sin cambiar las fuentes ni sus bytes.
Los manifiestos comprueban 144 entradas antes y después de las pruebas.

Los resultados Android posteriores deben consultarse en los checks de PR25,
vinculados a su propio commit y APK. La campaña local Android del shader
corregido está pendiente en este corte. Su runner exige observar que el emulador
leyó la configuración de SwiftShader antes de atribuirle el número de trabajadores
solicitado; después exige ocho escenas y todas sus repeticiones exactas.

## Cierre de los fallos encontrados por el CI

La primera pasada remota de `2be39f7` completó 13 de 18 campañas con éxito.
Sus cinco fallos no se descartaron: se conservaron los logs y se aislaron las
causas. En `5e8df8f`, los contratos nativos, de sesión y espejos ya aprobaron,
al igual que los 19 tests seleccionados de Infinite tanto en Linux como en
Windows. Los checks del PR identifican el estado posterior de cada campaña.

- **Dependencias:** el workflow de Infinite ahora compila también los dos
  ejecutables que seleccionaba CTest; el de espejos prepara el puente nativo
  canónico antes de usarlo.
- **Gráficos 8K:** una prueba nueva dejaba la semilla en 2027 y cambiaba la escena
  de una prueba posterior. Se restaura la semilla y el caso de exportación fija
  explícitamente 2026. El PNG anterior era válido y contenía geometría; se
  conserva el umbral original y un oráculo independiente explica la diferencia.
- **Coste del observador:** el índice nuevo añade 320 bytes al observador de
  20 qubits en x64, de 560 a 880 bytes; el payload del núcleo permanece en 340.
  El comparador exige los mismos resultados, checksum y carga, y contabiliza
  expresamente ese índice acotado. Comprueba además un límite virtual de 2 GiB
  y plazos finitos para cada ejecución nativa. No presenta ese aumento como
  ahorro de memoria.
- **Solar exacto:** se observa la lectura real de la configuración del
  controlador, tanto con ruta relativa como absoluta. El caso de Urano se
  resuelve mediante el oráculo completo descrito arriba.
- **Solar 8K:** el hilo gráfico completó un intervalo de 159.880,88 ms frente
  a un plazo de captura de 150 segundos. La instrumentación asigna 240 segundos
  sólo a las dos capturas 8K y conserva los plazos de los demás perfiles. Mantiene
  el dibujo bajo demanda durante el informe para no encolar otra imagen 8K al
  limpiar un fallo. El framebuffer, el PNG, su contenido, sus muestras y la
  restauración exacta siguen siendo obligatorios. El tiempo observado no es una
  medición de tiempo GPU ni acredita rendimiento interactivo a 8K.

La [evidencia de CI](../results/repository-unification-2026-10-21/ci/) conserva
los fallos originales y sus diagnósticos. La compilación conjunta y la ejecución
Android de las correcciones finales son comprobaciones separadas, vinculadas al
commit que contienen, y no se dan por aprobadas por haber identificado la causa.

## Qué demuestra la estructura y qué falta para valorarla

La representación por reglas y la composición óptica constituyen ingeniería
concreta. Ahorran almacenamiento y trabajo en problemas cuya regularidad se
puede conservar y consultar. Los espejos, por ejemplo, mantienen el objeto
fuente y pueden resolver determinados trayectos largos sin visitar todas sus
imágenes. Los oráculos y presupuestos permiten comprobar ese ahorro en un
problema definido.

Una arquitectura puede ser un activo comercial si permite a otros obtener un
resultado valioso con menos coste, o alcanzar una capacidad que necesitan. La
evidencia actual no determina un precio de millones: faltan datos sobre demanda,
adopción, ingresos o una operación de licencia, y comparaciones representativas
contra alternativas sobre dispositivos concretos.

La idea general de compartir representaciones y aprovechar repetición tiene
antecedentes: [High Resolution Sparse Voxel DAGs, Chalmers, 2013](https://research.chalmers.se/en/publication/182658)
comparte patrones de geometría voxelizada, y [NVIDIA documenta instancias que
comparten geometría](https://developer.nvidia.com/blog/creating-optimal-meshes-for-ray-tracing/)
para ahorrar memoria. Estos antecedentes no evalúan este repositorio ni prueban
que su combinación particular sea idéntica; sí impiden deducir novedad general
únicamente de las palabras «reglas», «reutilización» o «espejos».

La [OMPI distingue el coste de construir una tecnología de su valor para un
comprador](https://www.wipo.int/web-publications/intellectual-property-valuation-basics-for-technology-transfer-professionals/en/4-the-cost-method.html).
Influyen el beneficio futuro, las alternativas disponibles y el trabajo que
queda para llevarla a un uso comercial. Por eso esta revisión acredita código,
contratos y resultados medidos, y no asigna una tasación monetaria al repositorio.

El siguiente argumento comercial que conviene demostrar es concreto: una
aplicación que necesita escenas regulares grandes, memoria acotada y edición
por identidad puede incorporar este motor y obtener un beneficio reproducible.
La comparación debe medir calidad y corrección equivalentes, consumo completo,
latencia al cambiar la escena, casos irregulares y coste de integración. La
adopción de esa capacidad por usuarios o clientes permitiría evaluar su valor
con evidencia adicional.
