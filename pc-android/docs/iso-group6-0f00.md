# ISO Windows 10 en Android — bloqueo protegido `0F 00`

## Evidencia nueva del Samsung

El usuario seleccionó su ISO oficial y obtuvo:

| Variable | Lectura |
|---|---|
| Modo | `protected16` (antes `real16`) |
| Pasos CPU | **31.599** (antes 2.817) |
| Lecturas BIOS | 21 (sin cambio) |
| Dirección CS:IP | **`0050:0854`** |
| Opcode visible | **`0F 00`** |
| RAM de páginas x86 | **464 KiB** |
| PSS del proceso Android | **67 MiB** |
| Controlador 8042 | 2 comandos, 1 dato, último comando 0xFF |
| A20 | Habilitada |
| Windows listo | No |

La ejecución cruzó de real16 a protected16. Esto es
**progreso del cargador BIOS de la ISO**, no del kernel ni escritorio
de Windows 10.

## Diagnóstico del fallo

`0F 00` no es una instrucción completa: la operación precisa
se selecciona por el campo /reg del tercer byte **ModRM**, que
la versión anterior **no había leído** antes del error.

| ModRM /reg | Semántica IA-32 |
|---|---|
| /0 | SLDT: consultar selector LDTR |
| /1 | STR: consultar selector TR |
| /2 | LLDT: cargar descriptor LDT del GDT |
| /3 | LTR: cargar TSS del GDT, marcar el descriptor ocupado |
| /4 | VERR: comprobar acceso de lectura, devuelve ZF |
| /5 | VERW: comprobar acceso de escritura, devuelve ZF |
| /6 y /7 | Reservadas; deben fallar |

**No conocemos el ModRM de la ISO oficial por la captura.** No se
ha demostrado que use `LTR`, `LLDT` ni ninguna de las otras
formas. El nuevo decodificador registrará los bytes completos si
vuelve a encontrar un caso no soportado.

## Implementación

`x86_cpu.hpp` incorpora el grupo x86 0F00 con:

- Lectura real del ModRM y operandos de registro o memoria.
- Registros LDTR/TR con selector visible y descriptor
  oculto (base, límite, validez), restablecidos por reset.
- Snapshot y rollback de ambos registros junto con la CPU.
- Para LTR: validación de descriptor TSS disponible de 16/32 bits,
  presencia, mínimo de límite, protección de GDT y
  escritura de **bit busy** en memoria del huésped.
- Para LLDT: tipo de descriptor LDT o selector nulo.
- Para VERR/VERW: ZF según descriptor presente, tipo
  lectura/escritura y DPL/RPL; se permiten consultas
  de selectores de LDT realmente cargado.
- Validación explícita en IA-32e de descriptores de sistema
  de 16 bytes. Los casos no implementados o inválidos
  se detienen con el opcode y ModRM completo, sin dar
  resultados inventados.

**No** se implementan los cambios de tarea por hardware,
puertas de tarea ni excepciones de ring3, el kernel de Windows
completo o la ejecución de software x86 sin límites.

## Cómo se probó

`x86_group6_0f00_test.cpp`: pruebas directas de las seis
formas, ModRM, memoria, límites, selectores, bit busy,
denegaciones de permiso y rollback.

`pc_group6_iso_test.cpp`: ISO sintética El Torito cuyos
bytes ejecutan desde `2000:0000`, habilitan PE por CR0,
cargan la GDT real y saltan exactamente a `0050:0854`,
donde ejecutan **`0F 00 D8` (LTR AX)**, STR, VERR/VERW,
LLDT y SLDT antes de HLT.

`WindowsInstrumentation.java`: repite el escenario
ISO→CPU real16→protected16→TSS/LDT por JNI dentro de
Android 15.

Este fixture usa **LTR** deliberadamente para probar
la variante más exigente; **no afirma que esa variante
sea la solicitada por la ISO real del usuario**.

## Próximo ensayo real

Instalar `MUNDO-X-WIN10-ISO-LAB v0.1.3`, abrir la ISO
local del usuario, pulsar `Arrancar BIOS (ISO)` y copiar
el diagnóstico:

- ¿Pasó más de 31.599 pasos?
- ¿Superó `0050:0854`?
- ¿Hay selectores LDTR/TR cargados?
- ¿Qué opcode, ModRM o puerto será el siguiente?

No se debe declarar compatibilidad con Windows 10 hasta
que arranque realmente. El requisito de instrucción,
firmware y hardware sigue pendiente.

## Referencias

- Manual Intel, *Instruction Set Reference*: `0F 00`,
  `SLDT`, `STR`, `LLDT`, `LTR`, `VERR/VERW`.
- https://cdrdv2-public.intel.com/671110/325383-sdm-vol-2abcd.pdf
- https://courses.cs.washington.edu/courses/cse451/24sp/resources/i386/SLDT.htm
