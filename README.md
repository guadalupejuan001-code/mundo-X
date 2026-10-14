# Mundo X

**Mundo X** es un prototipo experimental para explorar computación basada en estados ondulatorios compactos.

## Transformer X

Representación mínima actual:

- **posición del modo = frecuencia/identidad implícita**
- **estado del modo =** `z = Re + i·Im = A·e^(iφ)`
- solo necesitamos materializar los modos que participan

La primera etapa crea un puente con la computación actual:

```
archivo / memoria / bytes actuales
              ↓
           Bridge X
              ↓
       modos + estado z
              ↓
        Transformer X
              ↓
       siguiente estado z
```

La máquina convencional sigue usando bits debajo. La hipótesis de Mundo X es que el **modelo no tiene que razonar en bits**: Bridge X traduce la entrada y Transformer X opera en un espacio de modos.

## Experimento v0.1

1. Leer bytes de un archivo real.
2. Dividirlos en ventanas.
3. Cada valor de byte (0–255) ocupa un modo fijo.
4. Para cada modo activo, Bridge X calcula:
   - amplitud = frecuencia relativa de aparición;
   - fase = posición media circular dentro de la ventana.
5. Transformer X observa el campo de una ventana.
6. Predice cómo estarán esos mismos modos en la ventana siguiente.
7. Se compara contra un baseline simple que supone que el estado no cambia.

El experimento solo cuenta como aprendizaje si:

```
loss(Transformer X) < loss(identity baseline)
```

en datos no usados para actualizar los pesos.

## Estado científico

Esto es un prototipo digital de representación ondulatoria. **No demuestra computación cuántica ni ventaja computacional.** Su función es producir evidencia medible antes de añadir memoria, pliegue o hardware físico.
