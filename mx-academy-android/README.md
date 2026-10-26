# MX Visual v0.2 — tocar primero, entender después

Aplicación Android **para personas que todavía no saben programar**, dedicada solo a MX (Mundo X Language). Los controles están en español sencillo y los programas se generan dentro de la aplicación.

## Uso

1. Abre el primer experimento y observa un qubit que empieza en cero.
2. Toca acciones visuales: INVERTIR (X), ABRIR OPCIONES (H), CAMBIAR FASE (Z/T), CONECTAR (CNOT).
3. Observa las probabilidades de obtener cada resultado, calculadas de verdad por Mundo X.
4. Pulsa **COMPROBAR MI EXPERIMENTO** para evaluar el estado contra el objetivo.
5. Solo cuando tengas curiosidad, abre **MOSTRAR CÓMO SE ESCRIBE EN MX** y copia el código que acabas de construir.

## Experimentos

- Estado inicial y medición conceptual.
- Inversión con Pauli X.
- Superposición con H.
- Entrelazamiento de dos qubits.
- Interferencia H–Z–H.
- GHZ de tres qubits.
- Repetición `repeat`.
- Circuitos reutilizables `circuit`.
- Fase T con motor reducido.

## Veracidad técnica

Se compila **MX auténtico** usando `mundo_x::compile_mx` y se ejecuta su XASM con `mundo_x::run_xasm`. Para visualizar la distribución de resultados se calculan expectativas Pauli-Z, y de ellas se derivan las probabilidades de la base computacional por transformación de Walsh. No son animaciones precalculadas ni valores inventados. El panel animado es una representación conceptual; las barras son las probabilidades reales del simulador.

Cada ejercicio se comprueba con aserciones del runtime, añadidas al programa compilado. La app no introduce otros lenguajes de estudio. La UI Android y el puente JNI usan Java y C++ como infraestructura porque MX v0.1 todavía no se autoaloja ni genera APKs.

## Límites y privacidad

- Hasta tres qubits y 64 puertas en el laboratorio principiante.
- Sin permisos de Internet, servicios remotos, anuncios ni cuentas.
- Progreso local en preferencias.
- Android 8+, arm64-v8a y x86_64.
- ID de paquete separado: `com.mundox.academy.visual`; coexiste con la versión anterior.

## Compilación reproducible

```sh
gradle -p mx-academy-android --no-daemon :app:assembleDebug
```

APK generada: `mx-academy-android/app/build/outputs/apk/debug/app-debug.apk`.

El workflow `mx-academy-android.yml` verifica la firma, empaqueta el APK y lo conserva en `mx-academy-android/dist/` de la rama de desarrollo. La firma es de depuración; una versión posterior con otra firma podría requerir reinstalación.

## Siguientes mejoras

- Pruebas de UI Android en dispositivo/emulador.
- Representación de fase además de probabilidades Z.
- Editor libre opcional y visualización de la expansión MX → XASM, solo tras los fundamentos.
