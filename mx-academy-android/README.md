# MX Academia para Android

Laboratorio educativo exclusivamente de MX (Mundo X Language), con ocho lecciones en español, editor de código y progreso guardado localmente.

El programa del estudiante se compila con mundo_x::compile_mx y se ejecuta con mundo_x::run_xasm, utilizando los motores originales de Mundo X. Los ejercicios se verifican mediante aserciones añadidas después del programa del alumno, no comparando texto ni simulando respuestas.

Para compilar: gradle -p mx-academy-android :app:assembleDebug

Requiere Android 8+ y arm64-v8a/x86_64. No necesita acceso a Internet ni servicios remotos. Presupuestos pedagógicos: 6 qubits, 256 operaciones y 32 puertas T.

La fuente del estudiante es MX. El paquete Android y el puente nativo todavía usan Java y C++ como herramientas de soporte porque MX v0.1 no genera interfaces Android ni se aloja a sí mismo.
