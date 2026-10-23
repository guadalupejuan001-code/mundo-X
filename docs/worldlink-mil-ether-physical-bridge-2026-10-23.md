# Mundo X / WorldLink v3 — MIL1000 × Ether COW

Base reproducible: main @ 92d0113acec83f5e5a91e906171a821fe3625f16,
con MIL 1000/ReducedExactT/Adaptive y PR #54 de InverseMemory
(256 páginas copy-on-write). El JNI compila las cabeceras canónicas C++
directamente; no hay una implementación paralela del núcleo.

## Puente construido

**Reloj → señales físicas → MIL 1000 / Ether → ondas de sonido reales**

1. El Watch4 envía un comando explícito con su movimiento, o bien el usuario
   activa manualmente el botón de cálculo en Samsung. El reloj puede enviar
   velocidad angular en centésimas, hasta 18 rad/s, sin ejecutar sonido fijo.
2. El núcleo MIL crea un Bell SIMULADO H(q0), CX(q0,q999), agrega una T,
   una fase arbitraria controlada por señales y una controlled-phase directa.
3. MIL obtiene XX y ZZ con **16 palabras de 64 bits** para cubrir q999, y
   calcula la magnitud CHSH para ajustes Bell fijos:
   S_sim = sqrt(2) * (ZZ+XX). No es medición de Bell física.
4. Ether QuantumRuntime ejecuta operaciones XI reales de la versión COW
   y guarda eventos en 128 direcciones lógicas circulares. Cada evento se
   lee de nuevo y se verifica sin colapsar inadvertidamente qubits.
5. El resultado XX del estado de MIL determina una frecuencia **180–1200 Hz**.
   Con la orden del usuario (o autorización remota visible) el altavoz emite
   una onda de presión real y limitada. Sin autorización, no se emite nada.

Los límites nativos son 1.000 qubits lógicos y 32 componentes por operación,
no 1.000 qubits físicos y no memoria infinita. Se reconstruye un circuito
finito por operación; no se acumulan fases arbitrarias indefinidamente.

## ¿Tenemos entrelazamiento físico?

**No todavía.** Tenemos un circuito software con Bell y CHSH SIMULADOS y
un enlace real a un actuador, pero no un dispositivo cuántico externo.
El Galaxy Watch4 no tiene una fuente de fotones entrelazados ni detectores
individuales con conteo de coincidencias. La iluminación PPG no puede
convertirse en ese hardware cambiando únicamente la codificación del sensor.

Para llevar la tecnología a un experimento de entrelazamiento de fotones real
haría falta un módulo externo con fuente controlada de pares entrelazados,
dos analizadores de polarización, detección de fotones individuales,
etiquetado temporal y registro de coincidencias para cuatro ajustes de CHSH.
Solo entonces WorldLink podría comparar S_medido con S_sim; no debe
interpretar S_sim>2 como una violación medida en laboratorio.

Fuentes experimentales:
- https://www.nist.gov/programs-projects/single-photonics-and-quantum-information
- https://www.nist.gov/publications/radial-polarization-imaging-entangled-biphoton-state
- https://api.samsungmobilepress.com/press-releases/galaxy-watch4-and-galaxy-watch4-classic-reshaping-the-smartwatch-experience

## APKs e instalación

Las dos APK son independientes de WorldLink 1.0/2.0: identificador común
com.mundox.worldlink.quantum y firma idéntica por compilación conjunta.

- Samsung: WorldLink-MIL-Ether-Telefono.apk
- Watch4: WorldLink-MIL-Ether-Reloj.apk

Para el reloj, usar la app de depuración que ya tienes con Elegir otra APK,
no el botón que instala la aplicación anterior incrustada.

La CI comprueba pruebas C++ de MIL y Ether, unidad del emisor acústico,
compilación NDK arm64, presencia de libworldlink_quantum.so, paquetes y firmas.
No acredita funcionamiento en los dos dispositivos reales hasta probar
transportes Wear OS y fenómenos físicos en ese hardware.
