# Mundo X · WorldLink v0.1 (Galaxy Watch4 Classic)

Experimento de **interacción física comprobable** entre COSMIC X Engine Android,
el Watch4 Classic y la iluminación del ambiente.

**No es entrelazamiento físico entre partículas.** El estado cuántico es una
simulación de Mundo X. La activación del LED del teléfono emite fotones
reales; el sensor óptico del reloj puede medir un cambio de lux bajo
condiciones controladas. No se ha implementado una interacción que controle
la geometría del espacio-tiempo.

## Ciclo físico

1. El usuario lleva puesto el Galaxy Watch4 Classic.
2. El reloj captura acelerómetro, giroscopio, magnetómetro, luz ambiental
   y, con permiso opcional, pulso.
3. Wear OS Data Layer transmite lecturas al teléfono.
4. WearLinkRules clasifica el giro de muñeca, y WearLinkBridge aplica una
   puerta H al NativeCore del proyecto COSMIC X existente. La escena sigue
   utilizando los motores de Mundo X.
5. Si se habilitó expresamente el LED y concedió permiso de cámara,
   PhoneLightEmitter envía un pulso de luz de máximo 1,2 segundos.
6. El reloj detecta, si su sensor óptico está accesible, el cambio de lux
   respecto a una lectura anterior. Repetir con LED apagado sirve de control.

No se modifica el firmware ni el hardware del reloj.

## Instalación

El workflow de GitHub Actions compila dos APK en el mismo runner y con la
misma clave de firma debug:

- Teléfono Android: Mundo-X-WorldLink-phone.apk
- Reloj Wear OS: Mundo-X-WorldLink-watch.apk
- Identificador compartido obligatorio: com.mundox.graphics

**ATENCIÓN:** Android puede rechazar la actualización de COSMIC X Engine si
está firmado con otra clave. Guarda antes tus proyectos con el control Guardar;
desinstalar puede borrar los datos de la app.

1. Instala el APK de teléfono en el Samsung.
2. Instala el APK de reloj en Watch4 Classic (por ejemplo con depuración
   inalámbrica ADB en el reloj; no es una APK de teléfono).
3. Mantén el reloj emparejado con el Samsung.
4. Abre COSMIC X Engine en el teléfono y pulsa Cuerpo ↔ Mundo.
5. Abre Mundo X Enlace en el reloj y pulsa INICIAR.
6. Comprueba lecturas de movimiento y lux en el teléfono.
7. Activa giro de muñeca → H virtual para conectar tu movimiento al núcleo.
8. Opcional: habilita el LED, autoriza cámara, y pulsa Probar luz física.
9. Compara la variación de lux con linterna apagada y encendida.

El reloj se detiene al salir de la actividad y automáticamente después de
tres minutos. La linterna se apaga al finalizar cada pulso o al abandonar
la app. Sin permiso BODY_SENSORS no hay pulso cardíaco. Si un sensor del
reloj es inaccesible, se informa en pantalla.

## Privacidad, precisión y seguridad

La Data Layer normalmente usa Bluetooth; Wear OS puede retransmitir
por Google Cloud con cifrado de extremo a extremo cuando Bluetooth
no está disponible. No se envían muestras a servidores de Mundo X.
No se almacenan lecturas biométricas en archivos ni bases de datos.
No usar el pulso para diagnóstico. No mirar el LED directamente.

## Validación

El workflow ejecuta WearLinkRulesCheck mediante javac y construye ambas
APKs, verificando firma y hash. Un hardware real es obligatorio para
validar el transporte, las mediciones ópticas, el sensor de luz y
la causalidad del mundo físico.

Ensayo recomendado: alternar cinco pruebas de luz apagada y cinco de
luz encendida a igual distancia y orientación; registrar los lux y
comparar la variación. Esa interacción es física, pero no certifica
entrelazamiento cuántico externo ni modificación de curvatura espacio-temporal.
