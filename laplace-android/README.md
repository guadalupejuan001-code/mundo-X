# LAPLACE X · Laboratorio matemático independiente · Android

**Estado:** desarrollo experimental. Este proyecto no utiliza ni modifica REVERSE-PC, REVERSE Workbench, BBY Q ni sistemas bursátiles.

## Qué se implementó exactamente

La ecuación de Laplace en el cuadrado 2D (problema estacionario, no un predictor temporal):

```text
∂²u/∂x² + ∂²u/∂y² = 0
```

El algoritmo implementa diferencias finitas con la discretización de cinco puntos y sobre-relajación sucesiva (SOR), contornos Dirichlet conocidos. Ejecuta en CPU Android, sin servidores ni hardware cuántico físico. Utiliza fórmulas matemáticas estándar pero código propio en Java para que el teléfono lo ejecute directamente y sin código prestado de motores gráficos. La fuente de las reglas de frontera son polinomios armónicos reales: x+2y-1, x²−y², x³−3xy² y x⁴−6x²y²+y⁴.

## Cuatro interfaces separadas

- **CAMPO:** resolver la ecuación en cuadrículas 17×17, 33×33, 65×65 o 129×129, visualizar mapa real de la solución numérica y consultar residuo, error e iteraciones.
- **CONVERGENCIA:** medir el error del esquema respecto a una solución analítica, mostrar gráfica, tabla y ganancia relativa de precisión.
- **DETERMINISMO:** ejemplo temporal sintético separado de la PDE. Expone qué cambia al introducir un evento desconocido, sin atribuirlo a la ecuación espacial.
- **RESULTADOS:** ejecutar auditoría local, consultar criterios aprobados y exportar resultados CSV.

Es una **APK independiente**, paquete `com.mundox.laplace`, instalable sin REVERSE. No contiene permisos INTERNET ni operaciones financieras.

## Criterios verificables

- `LaplaceMathTest` contrasta el campo calculado contra soluciones analíticas independientes en 4 casos y 4 tamaños, con **131 verificaciones** (incluye casos negativos y convergencia).
- `LaplaceAuditTest` repite una segunda campaña de **114 verificaciones**, con fórmula del oráculo escrita independientemente.
- Se comprueba el residuo `max |Δ_h u|`, que las fronteras permanecen intactas y que se respeta el principio del máximo para soluciones armónicas.
- Campo cuártico: error máximo debe disminuir aproximadamente 4× al duplicar el número de intervalos (segundo orden). Esto no significa exactitud literal en coma flotante ni predicción del mercado.
- `LaplaceInstrumentation` abre realmente las cuatro pantallas en Android 15, resuelve una cuadrícula, ejecuta auditoría, comprueba la ausencia de permisos de red y obtiene capturas.

### Datos locales obtenidos en la auditoría de escritorio

| Malla | Error máximo medido | Iteraciones |
| --- | ---: | ---: |
| 17×17 | 0.0011475901030 | 87 |
| 33×33 | 0.0002875575682 | 172 |
| 65×65 | 0.0000719308462 | 336 |
| 129×129 | 0.0000179853069 | 663 |

La referencia analítica del caso cuártico satisface la ecuación diferencial continua; la malla discreta tiene error de truncamiento, por lo que demostrar el descenso de error es esencial. Los datos locales se vuelven a medir en CI y en la app. La aproximación de 129×129 produce residuo máximo de orden `2.38e-8` para ese caso en Java de escritorio.

## Compilar y probar

```sh
javac -encoding UTF-8 -d build-laplace \
  laplace-android/app/src/main/java/com/mundox/laplace/LaplaceEngine.java \
  laplace-android/app/src/main/java/com/mundox/laplace/LaplaceAudit.java \
  laplace-android/tests/com/mundox/laplace/LaplaceMathTest.java \
  laplace-android/tests/com/mundox/laplace/LaplaceAuditTest.java
java -ea -cp build-laplace com.mundox.laplace.LaplaceMathTest
java -ea -cp build-laplace com.mundox.laplace.LaplaceAuditTest
gradle -p laplace-android :app:assembleDebug :app:assembleDebugAndroidTest
```

La canalización `.github/workflows/laplace-x-android.yml` compila una APK firmada para pruebas y documenta paquete, firma, prueba de emulador y capturas en el artefacto **LAPLACE-X-STANDALONE-APK**.

## Diferencia científica imprescindible

El **demonio de Laplace** es una hipótesis filosófica de determinismo mecánico, no la ecuación de Laplace. El predictor anterior de REVERSE-PC utilizaba extrapolación discreta de precios y **no implementaba** esta ecuación. Resolver exactamente un campo no significa conocer el futuro de la bolsa. La aplicación comunica ambas cosas por separado y ofrece métricas calculables, no promesas de ganancias financieras.

Fuentes de referencia matemática:
- Universidad de British Columbia: https://ubcmath.github.io/MATH316/laplace.html
- SIAM, *Finite Difference Schemes and Partial Differential Equations*, sección 13.
