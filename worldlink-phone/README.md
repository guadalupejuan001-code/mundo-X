# Mundo X · WorldLink LAB 1.0

**Interfaz física independiente de COSMIC**, para Samsung Android +
Galaxy Watch4 Classic (Wear OS). Dos APK firmadas con el mismo certificado
en una única ejecución del workflow \`.github/workflows/worldlink-lab-ui.yml\`.

- Teléfono: WorldLink-Mundo-X-Telefono.apk (instalar en Samsung).
- Reloj: WorldLink-Mundo-X-Reloj.apk (instalar en Watch4 Classic usando
  el depurador inalámbrico que ya tienes: **Elegir otra APK**).

**Importante:** la opción **Instalar Mundo X Enlace** que está integrada
en el depurador anterior instala el proyecto antiguo para COSMIC. Para
este laboratorio independiente usa **Elegir otra APK** y selecciona
WorldLink-Mundo-X-Reloj.apk.

El paquete de ambas APK es \`com.mundox.worldlink\` y ambas tienen
la misma firma. El proyecto anterior, COSMIC X, usa otro paquete.
No hay que instalar COSMIC ni programar qubits para usar el laboratorio.

## Cómo usar

1. Instala ambas APK en sus respectivos dispositivos.
2. Mantén reloj y Samsung vinculados por Galaxy Wearable. En la pantalla del
   Samsung, abre WorldLink y pulsa **BUSCAR RELOJ**.
3. En el reloj abre WorldLink y pulsa **INICIAR LECTURA**.
4. La pantalla Samsung mostrará luz, intensidad magnética, velocidad angular
   y pulso (este último solo si autorizas permisos y el sistema lo expone).
5. **CAPTURAR REFERENCIA** registra el valor presente de la luz y el campo.
6. **EMITIR LUZ REAL** permite un pulso de linterna de 0,95 segundos con
   permiso de cámara. Observa la lectura de luz que proporciona el reloj y
   compara con pruebas sin emisión.
7. **SALIDA FÍSICA: ACTIVADA** habilita que el botón **PEDIR PULSO DE LUZ**
   del reloj active un pulso mientras la app móvil esté abierta.
8. **VIBRAR RELOJ** envía una orden física (haptics).
9. Cambia el gráfico entre **LUZ** y **CAMPO MAGNÉTICO**.
10. Pulsa DETENER en el reloj al terminar. Cada sesión dura como máximo
    tres minutos; al salir de cualquiera de las apps, las emisiones cesan.

## Qué cambia físicamente

El LED del teléfono emite fotones reales. Si llegan al sensor óptico del
reloj, la lectura de lux puede aumentar. Un imán manual puede modificar
el campo magnético local y por tanto la lectura del magnetómetro.
La vibración haptica también es un movimiento físico.

La mera representación digital de un estado de un qubit simulado no causa
entrelazamiento cuántico con partículas exteriores. Nada en el prototipo
implica control del espacio-tiempo. Este es un experimento de control de
señales y **efectos físicos medibles**, no un anuncio de nuevos fenómenos.

## Precisiones

- La Wear OS Data Layer exige **mismo package ID y mismo certificado** para
  enviar mensajes entre apps. Compila las dos APK en el mismo workflow;
  si mezclas builds, el enlace puede fallar.
- Galaxy Watch4 tiene sensores de campo, movimiento, luz y pulso, pero no
  todos tienen por qué estar disponibles para aplicaciones de terceros.
  No se inventan valores: cuando faltan, se muestran como no disponibles.
- La telemetría se transmite mediante servicios de Google Play, que pueden
  usar Bluetooth o Google Cloud. No se usa servidor propio.
- La app no guarda lecturas de salud en archivos; se muestran en memoria
  mientras está activa. La lectura de pulso no sirve para diagnóstico.
- No mires directamente al LED. Evita imanes intensos y ten precaución
  con dispositivos médicos implantados.
- El test de CI verifica compilación, package y firma, pero no sustituye
  una prueba en dispositivos físicos ni demuestra causalidad por sí mismo.
