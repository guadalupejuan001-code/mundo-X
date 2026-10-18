# Cómo se llama y qué hace el motor de Mundo X

**Tu proyecto se llama Mundo X. Su núcleo compacto original se llama
`CompactCore`, y su extensión original para circuitos con puerta T se llama
`ReducedExactTRepresentation`.** Son nombres que ya estaban en el repositorio;
este mapa no los renombra.

**Corrección posterior:** el titular empleó «computasis» como analogía de
homeostasis, no como una petición de crear una clase o un controlador. La clase
que se añadió con ese nombre fue una decisión de implementación del asistente.
Su implementación, integración, comando y pruebas específicas fueron eliminados
del código activo. Se conservan únicamente sus mediciones históricas. No se
presenta como parte de la arquitectura ni como homeostasis demostrada.

Aquí, **motor** significa el código que guarda una representación del estado,
la actualiza al aplicar operaciones y permite consultar resultados. Por ejemplo,
`CompactCore` guarda relaciones entre qubits en un tableau, aplica H o CNOT y
permite consultar sus generadores. El término describe software ejecutado en
hardware convencional; no identifica hardware cuántico nuevo ni una biblioteca
de otro proyecto.

El ejecutable **`mundo_x` es el runtime**: recibe comandos y llama a las rutinas
correspondientes. Su nombre está definido en [CMakeLists.txt](../CMakeLists.txt),
y las rutas de ejecución están en [main.cpp](../native/src/main.cpp).

| Componente existente | Función | Origen |
|---|---|---|
| [CompactCore](../native/include/mundo_x/compact_core.hpp#L17) | Representación compacta y operaciones para estados Clifford/estabilizadores. | Núcleo original, presente en `f858053`. |
| [ReducedExactTRepresentation](../native/include/mundo_x/exact_t_experiment.hpp#L444) | Extensión que comparte una base estabilizadora y conserva componentes Pauli con coeficientes para Clifford+T. Su número de componentes puede crecer exponencialmente. | Presente en `f858053`. |
| [ExactTExperiment](../native/include/mundo_x/exact_t_experiment.hpp#L56) | Referencia experimental que conserva explícitamente las ramas de la descomposición de T. | Presente en `f858053`. |
| [Quantum Core: QuantumStateX](../src/quantum_core.py#L5) y [DenseCore](../native/include/mundo_x/dense_core.hpp#L174) | Referencias de vector completo, en Python y C++, para comprobar resultados pequeños. La versión actual también ofrece `DenseCore64`. | Ambas referencias estaban en `f858053`; la variante double se añadió después. |
| [FactorizedCore](../native/include/mundo_x/factorized_core.hpp#L29) | Prototipo añadido que conserva factores independientes y los fusiona en bloques cuando una operación los conecta. | Añadido en `6b2d90b`. |

**FactorizedCore es un prototipo posterior, no un nombre nuevo del motor
original.** No hay un selector automático entre Factorized y Compact/Reduced,
ni una integración de esas dos vías. Los resultados de cada implementación
deben atribuirse a la que se ejecutó, sin trasladarlos a las demás.

El origen se comprobó con los archivos de `f858053`, el cambio `6b2d90b` y el
código actual sobre `3125880`. Este mapa no añade dependencias ni adopta otra
arquitectura. Los límites de las representaciones y los errores numéricos siguen
requiriendo pruebas; ningún nombre implica eficiencia universal.
