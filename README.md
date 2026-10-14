# Mundo X

**Mundo X** es un proyecto experimental de computación orientado a una idea central:

> **No buscamos más potencia por fuerza bruta. Buscamos representar y transformar solo lo necesario.**

El objetivo no es abandonar la computadora clásica. El objetivo es explorar si podemos usar una computadora clásica como soporte de un modelo cuántico/ondulatorio mucho más eficiente, evitando materializar información que el sistema puede describir mediante estructura, relaciones, fase, interferencia o reglas de evolución.

---

## Objetivo principal

La simulación cuántica clásica habitual representa un estado de `n` qubits mediante:

```
2^n amplitudes complejas
```

Eso funciona, pero el costo de memoria crece exponencialmente.

**Mundo X quiere atacar esa representación, no comprar más RAM.**

La pregunta del proyecto es:

> ¿Qué familias útiles de estados y operaciones cuánticas pueden describirse y evolucionar sin almacenar explícitamente todo el vector de `2^n` amplitudes?

No pretendemos afirmar que todo estado cuántico arbitrario puede comprimirse. Queremos descubrir qué estructura puede explotarse sin perder el comportamiento que nos importa.

---

# Quantum Core

**Quantum Core** es el núcleo actual de Mundo X.

Su función inicial es servir como referencia correcta de matemáticas cuánticas antes de intentar comprimir la representación.

Actualmente implementa:

- estados complejos normalizados;
- superposición;
- fase relativa;
- interferencia;
- Pauli-X;
- Hadamard;
- CNOT;
- entrelazamiento de Bell;
- medición y colapso.

Estado general de referencia:

```
|psi> = sum(alpha_k |k>)
```

con:

```
sum(|alpha_k|^2) = 1
```

## Hello Mundo X

El experimento mínimo es:

```
|0>
 ↓ H
(|0> + |1>) / sqrt(2)
 ↓ phase(pi)
(|0> - |1>) / sqrt(2)
 ↓ H
|1>
```

Ejecuta:

```bash
python hello_mundo_x.py
```

Resultado observado con la implementación actual:

```
inicio             -> probabilidades [1, 0]
Hadamard           -> probabilidades [0.5, 0.5]
cambio de fase pi  -> probabilidades [0.5, 0.5]
interferencia      -> probabilidades [~0, 1]

Mundo X says: 1
TEST: PASS
```

El residuo numérico observado para el estado 0 fue aproximadamente `3.7e-33`, esencialmente cero dentro de precisión de coma flotante.

Esto verifica en el simulador la cadena:

**superposición → fase → interferencia → resultado.**

---

## Lo que ya funciona

### Quantum Core

Funciona como simulador de referencia para las primitivas cuánticas anteriores.

### Entrelazamiento

Puede construir el estado de Bell:

```
(|00> + |11>) / sqrt(2)
```

mediante Hadamard + CNOT.

### Transformer X

Existe como componente experimental separado para explorar representación y aprendizaje sobre estados compactos.

No define el núcleo de Mundo X y todavía no se usa como prueba de ventaja computacional.

---

## Estado actual de eficiencia

El README principal ya no usa el statevector denso como referencia visual principal.
Ese método queda únicamente como **baseline histórico** dentro de los resultados de
benchmark.

El estado actual de Mundo X es **Compact Core**.

Resultados medidos:

| Prueba | Compact Core |
|---|---:|
| GHZ 24 qubits | **408 bytes de estado** |
| GHZ 40 qubits | **~680 bytes de estado** |
| GHZ 10,000 qubits | **~23.97 MiB de estado** |

## Capacidad teórica con 1 GB

Para la representación bit-packed actual:

```
16 * n * ceil(n / 64) + n bytes
```

Con **1 GB decimal = 1,000,000,000 bytes**:

```
63,232 qubits -> 999,634,688 bytes
63,233 qubits -> 1,000,662,225 bytes
```

Por tanto:

```
Compact Core actual con 1 GB: 63,232 qubits
```

Con **1 GiB = 1,073,741,824 bytes**:

```
65,532 qubits
```

> Estos valores corresponden a la familia Clifford/stabilizer soportada por el
> Compact Core actual. No representan qubits universales arbitrarios.

Los resultados comparativos contra el motor denso se conservan en:

```
results/native_benchmark_2026-10-13.md
```

para reproducibilidad, pero ya no definen el estado actual del proyecto.

---

# Próxima meta: Compact X general

Compact Core ya demuestra la idea de representación estructurada dentro de
Clifford/stabilizer.

La siguiente etapa es conservar esa eficiencia al salir de esa familia,
empezando por recursos no-Clifford como la puerta `T` / magic, sin volver a
materializar un statevector completo.

Ideas que se pueden explorar y comparar:

- estados generados por reglas;
- representaciones dispersas;
- simetrías;
- factorizaciones;
- redes tensoriales;
- modos/frecuencias;
- relaciones locales;
- transformaciones implícitas;
- combinaciones híbridas.

La regla es estricta:

> **No vale reducir memoria en un lugar si el mismo costo reaparece oculto en otra tabla, caché o estructura.**

---

## Criterio de éxito

Una nueva representación de Quantum Core será interesante solo si cumple simultáneamente:

1. reproduce resultados del núcleo de referencia dentro de una tolerancia definida;
2. conserva normalización y fase cuando sean necesarias;
3. reproduce interferencia y entrelazamiento en los casos compatibles;
4. usa menos memoria explícita para la familia de estados evaluada;
5. no materializa secretamente el vector completo durante el cálculo;
6. permite medir tiempo, memoria y error contra el baseline denso.

Nuestro objetivo no es decir:

> "Mundo X es más poderoso."

Nuestro objetivo es poder demostrar algo del tipo:

```
misma tarea
mismo resultado dentro de tolerancia
menos representación explícita
menos memoria
```

---

## Orden del proyecto

```
1. Quantum Core          -> referencia cuántica densa            [HECHO]
2. Compact Quantum Core  -> baseline Clifford compilable          [ACTIVO]
3. Compact X general     -> superar límites del baseline          [SIGUIENTE]
4. Interface X           -> entrada/salida de información
5. Transformer X         -> usar aprendizaje solo si aporta
6. Fold / modo operativo -> investigar dinámica posterior
```

Nada se añade porque "suene cuántico". Cada etapa debe justificar su costo con una prueba.


---

# Mundo X Lab APK v0.1

La primera APK de Mundo X ya compila correctamente en GitHub Actions.

Build verificado:

```
workflow: Build Mundo X Lab APK
run: #5
status: success
artifact: Mundo-X-Lab-APK
```

Archivo extraído y verificado:

```
Mundo-X-Lab-v0.1.apk
size: 983,977 bytes
sha256: d2673d4599b39327e34e0f13602001f23f3c9f0f5ce7fee97a15cee05be9199e
ABI: arm64-v8a
minSdk: 26
targetSdk: 35
```

El APK contiene el núcleo nativo:

```
lib/arm64-v8a/libmundox.so
```

y expone desde Android:

- **HELLO X** — compara Dense Core y Compact Core en el circuito mínimo;
- **RUN COMPACT** — crea un estado GHZ compacto de 2 a 10,000 qubits;
- **COMPARE DENSE** — compara ambos motores con límite de seguridad de 22 qubits para Dense;
- **PRACTICE** — permite ejecutar comandos `h`, `x`, `z`, `s`, `cx`, `show`, `bytes`;
- lectura del **PSS aproximado del proceso Android** después de cada experimento.

La app usa Java únicamente para la interfaz. El cálculo principal se ejecuta en
C++17 mediante JNI y reutiliza los motores nativos de Mundo X.

Este build es un **debug APK** firmado por el toolchain de Android. No es una
release firmada para publicación en Google Play.

La estructura del APK fue inspeccionada tras la compilación y contiene el DEX,
manifest, recursos y la librería nativa esperada. La instalación física en un
teléfono debe comprobarse en un dispositivo Android arm64.


---

## Estado científico

Mundo X corre actualmente sobre hardware clásico.

**Quantum Core es una simulación clásica de matemáticas cuánticas.**

Todavía no se ha demostrado:

- ventaja cuántica;
- hardware cuántico propio;
- eliminación general del crecimiento exponencial;
- superioridad frente a simuladores existentes;
- una nueva teoría física.

Eso no invalida el objetivo. Define exactamente qué falta demostrar.

La hipótesis de trabajo de Mundo X es más limitada y comprobable:

> **Para ciertos estados y problemas estructurados, quizá podamos obtener el comportamiento útil sin representar explícitamente todo lo que una simulación densa almacena.**
