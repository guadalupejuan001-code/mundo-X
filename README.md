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

## Lo que todavía NO hemos resuelto

Quantum Core todavía usa un **vector de estado denso**.

Con `complex64`, el estado necesita aproximadamente:

```
RAM_estado = 2^n * 8 bytes
```

Ejemplos del **Dense Quantum Core** (vector de estado completo, NO Compact Core):

| Qubits | Estado denso |
|---:|---:|
| 10 | 8 KiB |
| 20 | 8 MiB |
| 25 | 256 MiB |
| 30 | 8 GiB |
| 40 | 8 TiB |

La cifra de **40 qubits = 8 TiB** corresponde exclusivamente al baseline denso:
`2^40 amplitudes × 8 bytes`.

## Comparación con Compact Core actual

Para circuitos Clifford/stabilizer soportados por el Compact Core, no guardamos
las `2^n` amplitudes. Usamos una representación bit-packed estructurada.

Resultados medidos:

| Prueba | Dense Core | Compact Core |
|---|---:|---:|
| GHZ 24 qubits | 128 MiB de estado | **408 bytes** |
| GHZ 10,000 qubits | impracticable como statevector denso | **~23.97 MiB** |

Para 40 qubits, el Compact Core actual necesita aproximadamente **680 bytes de
estado estructurado** para esta familia Clifford, frente a **8 TiB** del vector
denso.

Esto **no significa** que podamos representar cualquier estado arbitrario de
40 qubits en 680 bytes. Significa que, para estados/circuitos con la estructura
que Compact Core soporta, evitamos almacenar información redundante.

Por tanto, **el problema exponencial sigue existiendo en el baseline denso,
pero ya no se aplica a todas las pruebas estructuradas que ejecutamos con Compact Core**.

## ¿Cuántos qubits caben en 1 GB?

Usando exactamente las representaciones actuales de Mundo X:

### Dense Core

El estado denso usa:

```
2^n * 8 bytes
```

Con **1 GB decimal = 1,000,000,000 bytes**:

- 26 qubits caben;
- 27 qubits requieren 1,073,741,824 bytes y ya exceden 1 GB.

Resultado:

```
Dense Core con 1 GB: 26 qubits
```

### Compact Core actual

La representación bit-packed actual usa aproximadamente:

```
16 * n * ceil(n / 64) + n bytes
```

Con **1,000,000,000 bytes**, el máximo que entra según esa fórmula es:

```
63,232 qubits
```

Uso calculado:

```
999,634,688 bytes
```

El siguiente valor ya excede el límite:

```
63,233 qubits -> 1,000,662,225 bytes
```

Comparación:

| Memoria de estado | Dense Core | Compact Core actual |
|---|---:|---:|
| 1 GB decimal | **26 qubits** | **63,232 qubits** |

Eso equivale a aproximadamente **2,432× más qubits representables** dentro de
la familia Clifford/stabilizer que soporta este Compact Core.

> Esta comparación NO significa 63,232 qubits universales arbitrarios.
> Significa 63,232 qubits dentro de la clase estructurada que el motor compacto
> actual puede representar sin materializar un statevector completo.

Referencia adicional:

- con **1 GiB = 1,073,741,824 bytes**, el Compact Core actual llega a **65,532 qubits**.

Esto es deliberado: primero necesitamos una referencia correcta contra la cual medir cualquier representación nueva.

---

# Próxima meta: Compact Quantum Core

La siguiente etapa no es agregar más qubits.

Es reemplazar:

```
[alpha_0, alpha_1, alpha_2, ..., alpha_(2^n-1)]
```

por una representación estructurada que solo materialice lo necesario.

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
