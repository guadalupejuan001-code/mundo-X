# Verificación del comparador de referencia en Windows

La publicación `f6c5614` eliminó el controlador añadido. Android completó sus
pruebas nativas y el APK; Windows compiló, pero `reduced_optimization` falló con
`independent reference mismatch at step 0`. Los otros cuatro tests pasaron.

Se compararon cuatro variantes en el mismo runner con MSVC 19.51.36260.0,
usando los mismos headers y tres ejecuciones por variante:

| Variante | Resultado |
|---|---|
| Prueba original, Release | Tres fallos |
| Prueba original, Release con `/Ob0` | Tres ejecuciones correctas |
| Prueba original con `/Od` | Tres ejecuciones correctas |
| Prueba con diagnósticos ampliados, Release | Tres ejecuciones correctas |

Esto demuestra sensibilidad del test a optimización e inlining; no demuestra
por sí solo un defecto del compilador ni identifica una causa matemática.
La [comparación registrada](https://github.com/guadalupejuan001-code/mundo-X/actions/runs/39095902326)
conserva también los tres fallos originales; el éxito global de ese workflow de
investigación no significa que todas sus variantes pasaran.

La corrección configura `/Ob0` **solo para `test_reduced_optimization` en MSVC**.
El ejecutable `mundo_x` conserva su optimización normal. Los cálculos de los
núcleos, la comparación de fase y los umbrales numéricos permanecen intactos.
Es una mitigación de la prueba; la causa raíz sigue abierta. El ajuste también
afecta a las funciones de headers compiladas dentro de ese test y no certifica
por sí solo todas las rutas optimizadas del runtime.
El mensaje de fallo incluye ahora circuito, operandos, error L2 y amplitudes
de las dos referencias para investigar cualquier regresión posterior.

Como comprobación adicional, la rama portátil de paridad también pasó 7.081
comparaciones bajo GCC en una copia aislada. Eso no emula MSVC y no se utiliza
como explicación del fallo de Windows.
