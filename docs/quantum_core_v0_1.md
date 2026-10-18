# Quantum Core

> Registro histórico de la referencia v0.1. Compact Core y Reduced Exact T ya
> están implementados; la «próxima etapa» al final describe el plan de entonces.
> Para el estado actual y el recorrido programable, consulta el
> [README](../README.md) y el [origen y recorrido cuántico](origen_y_recorrido_cuantico.md).

## Propósito

Quantum Core es la referencia cuántica de Mundo X.

No existe para perseguir el mayor número posible de qubits. Existe para definir
qué comportamiento debe conservar una futura representación más eficiente.

La pregunta central de Mundo X es:

> ¿Podemos obtener comportamiento cuántico útil para familias estructuradas de
> estados sin materializar el vector completo de 2^n amplitudes?

Quantum Core denso es el baseline contra el cual se probará esa hipótesis.

---

## Representación actual

Para `n` qubits:

```
|psi> = sum(alpha_k |k>)
```

con:

```
sum(|alpha_k|^2) = 1
```

Las amplitudes son complejas, por lo que magnitud y fase relativa forman parte
del estado.

La implementación actual usa `complex64` y almacena explícitamente las
`2^n` amplitudes.

Costo mínimo aproximado del estado:

```
2^n * 8 bytes
```

sin contar temporales, PyTorch, Python ni otras estructuras.

---

## Primitivas implementadas

- estado complejo normalizado;
- Hadamard;
- Pauli-X;
- rotación de fase;
- CNOT;
- superposición;
- interferencia;
- estado de Bell;
- medición y colapso;
- fidelidad entre estados.

---

## Resultado mínimo validado

`hello_mundo_x.py` ejecuta:

```
|0>
 -> H
 -> phase(pi)
 -> H
 -> |1>
```

Resultado observado:

```
H|0>                 -> [0.5, 0.5]
phase(pi)            -> [0.5, 0.5] con fase distinta
interferencia final  -> [~0, 1]
TEST                  -> PASS
```

Este experimento demuestra que el simulador distingue entre probabilidad y
fase: dos estados pueden mostrar las mismas probabilidades intermedias y aun
así producir resultados finales diferentes por interferencia.

También se implementó el estado de Bell:

```
(|00> + |11>) / sqrt(2)
```

mediante Hadamard + CNOT.

---

## Qué NO demuestra

Estos resultados no demuestran:

- ventaja cuántica;
- reducción general de memoria;
- hardware cuántico;
- novedad científica;
- capacidad de comprimir estados cuánticos arbitrarios.

La implementación actual sigue siendo un simulador clásico denso.

---

# Próxima etapa: Compact Quantum Core

El próximo experimento debe intentar representar una familia concreta de
estados sin almacenar todas sus amplitudes.

La comparación mínima será:

```
Dense Quantum Core
vs
Compact Quantum Core
```

y mediremos:

- bytes de estado;
- memoria pico;
- tiempo;
- error/fidelidad;
- operaciones soportadas;
- si en algún momento se materializó el vector denso.

Una representación compacta solo cuenta como avance si reduce costo real sin
mover el mismo costo a otro lugar.

## Principio del proyecto

**Eficiencia antes que potencia.**

No aumentar recursos para cargar una representación ineficiente.

Primero intentar reducir la representación.
