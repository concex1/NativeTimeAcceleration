# Native Time Acceleration

Plugin SFSE para **Starfield 1.16.244** que acelera el tiempo in-game de las acciones **Dormir** y **Esperar** alterando el GameSetting `iSecondsToSleepPerUpdate`, con reparto exacto de la duración solicitada en cualquier planeta.

> **Estado**: completo y validado. Runtime objetivo: Starfield 1.16.244.0, SFSE 0.2.21, commonlibsf del 2026-09.
> **Documento maestro**: Actualizado a fecha **2026-10-02**.
> **Autor**: concex1.
> 
> 

---

## Qué hace

El menú de descanso de Starfield ejecuta ticks de tiempo in-game con un intervalo fijo (`iSecondsToSleepPerUpdate`, por defecto 30 minutos). Este mod altera ese intervalo para que dormir y esperar avancen el tiempo a la velocidad que elijas.

Tiene **dos modos** de operación:

| Modo | Descripción |
| --- | --- |
| **Classic** | Tick fijo configurable al arrancar. Simple, estable, sin dependencias de eventos. Hotkeys en vivo.

 |
| **GearShift** | Motor con detección de sesión por eventos del juego. Calibra el ratio temporal local/UT del planeta actual. Reparte la duración solicitada en ticks exactos **sin perder tiempo sobrante**.

 |

En resumen: **el tiempo que pides es el tiempo que avanzas**, sin importar la duración ni el tick configurado, y sin importar el ratio temporal local/UT del planeta donde estés.

---

## Características

* **Tick fijo configurable** (`HoursPerTick=1..4`).


* **Cambio de marcha al residuo**: si la duración no es múltiplo exacto del tick, el último tramo se consume a la velocidad del remanente en lugar de descartarse.


* **Calibración Dinámica del ratio local/UT**: el mod mide el ratio empíricamente en el primer tick de cada sesión, lo cachea durante 60 s, y detecta cambios de planeta automáticamente. Funciona en planetas donde 1 hora local equivale a 33 minutos UT (ratio < 1), a 1 hora UT (ratio = 1), o a 100 horas UT (ratio = 100), sin configuración adicional.


* **Invalidación de caché por cambio de celda (v4.1.6)**: si el jugador cambia de planeta, viaja dentro del mismo planeta, entra o sale de un interior, o carga una partida, la caché de ratio se invalida ante cualquier `TESCellFullyLoadedEvent` (ID 838273). Esto elimina cualquier riesgo de sobre-avance cuando la caché del planeta anterior podría inducir un sacrificio incorrecto en el planeta nuevo.


* **Soporte completo para Dormir y Esperar**.


* **Duración menor al tick**: si pides 1h con tick=4h, la sesión avanza 1h (antes se quedaba en 0h).


* **Manejo de interrupciones**: cancelar un descanso a mitad funciona correctamente; el mod queda listo para la siguiente sesión.


* **Hotkeys en modo Classic**: `Shift+T` (toggle) y `Shift+1..4` (cambio de velocidad en vivo).


* **GearShift opera exclusivamente por eventos del motor**: no hay hilos auxiliares sondeando el calendario, no hay rutas alternativas ni fallback. El registro de los ocho sinks es un requisito estructural.


* **Log limpio (v4.1.6)**: 2-3 líneas por sesión de descanso si el probe está activo; cero líneas fuera de sesión. Utiliza un único `exchange(false)` atómico para garantizar una sola línea de log por transición lógica de celda. Se retiró la instrumentación `[Trace]` en producción.


* **Diagnóstico en arranque**: verificación de IDs y caracterización de los ocho singletons de eventos. Solo lectura, no muta el motor.


* **Reporte empírico opcional por sesión**: contadores de eventos de tiempo (Horas, Ticks, Cruces de medianoche), ratio medido y comparación bidimensional UT/local con el eje UT sin redondear para mayor precisión en la lectura del log.



---

## Requisitos

* **Starfield** versión **1.16.244.0**.


* **SFSE (Starfield Script Extender)** 0.2.21 o superior.


* Address Library for SFSE Plugins


* Windows 10/11, 64-bit.

---

## Instalación

1. Instala **SFSE** si no lo tienes.
2. Copia `NativeTimeAcceleration.dll` y `NativeTimeAcceleration.ini` a:
```
<Starfield>/Data/SFSE/Plugins/

```


3. Arranca el juego normalmente (a través de `sfse_loader.exe`).
4. Edita el INI según tus preferencias (ver abajo).
5. El log de arranque está en:
```
%USERPROFILE%/Documents/My Games/Starfield/SFSE/Logs/NativeTimeAcceleration.log

```



---

## Configuración

Todas las opciones van en `NativeTimeAcceleration.ini`.

> **Nota**: en V4 los booleanos se expresan con enteros (`1`/`0`), no con `true`/`false`. Cualquier valor distinto de `0` se interpreta como `true`.
> 
> 

### Modo de operación

```ini
bEnableGearShiftEngine=1

```

* `0` — **Classic**. Tick fijo. Estable, sin dependencias de eventos. Hotkeys activos. No registra sinks de eventos.


* `1` — **GearShift**. **Recomendado**. Reparto exacto de duración con calibración dinámica del ratio local/UT. Registra automáticamente los ocho sinks de eventos del motor.



Si los ocho singletons no estuvieran disponibles, el mod reporta un error crítico en el log y GearShift no opera. No se degrada silenciosamente a otra ruta.

### Velocidad base

```ini
HoursPerTick=3

```

Valores válidos: **1, 2, 3, 4**. Cualquier valor fuera de `[1..4]` se reconvierte automáticamente a `3` con una advertencia en el log. El dominio se eligió por estabilidad matemática.

### Probe de verificación

```ini
bEnableVerificationProbe=0

```

Activa el conteo de `HourPassed` / `HoursPassed` / `DaysPassed` por sesión de descanso. Emite 2-3 líneas por sesión de descanso y cero líneas fuera de sesión. **Dejar en `0` en producción** para mantener el log mínimo.

### Caché interna

`bUseCachedGlobal` está **bloqueado de fábrica en false** por seguridad. La escritura a la caché interna del motor (`g_iSecondsToSleepLow`) no es prioritaria porque los tests demuestran que escribir en el GMST es suficiente para afectar al tick real.

### Hotkeys (solo modo Classic)

```ini
[Hotkeys]
ModifierKey=16   ; VK_SHIFT
MainKey=84       ; VK_T

```

Solo se procesan si `bEnableGearShiftEngine=0`. Los códigos son VK de Windows.

---

## INI de ejemplo (producción)

```ini
[Settings]
bEnableGearShiftEngine=1
HoursPerTick=3

bEnableVerificationProbe=0

[Hotkeys]
ModifierKey=16
MainKey=84

```

---

## Cómo funciona (resumen técnico)

### Modo Classic

Fija el GMST al cargar. No registra eventos.

### Modo GearShift

El motor reporta las duraciones solicitadas en horas UT, pero el GMST se interpreta en segundos locales. El mod calibra el ratio empíricamente:

1. **Tick de sacrificio**: inyecta un tick de tamaño conocido (1h local o `H` si hay caché reciente).


2. **Medición**: calcula `ratio = deltaUT / deltaLocal` tras el primer `HourPassed`, compensando el epsilon `-1s`.


3. **Cálculo y Caché**: guarda el ratio durante 60 s reales, reconstruye `totalLocal = round(requestUT / ratio)` y reparte los ticks.


4. **Cambio de Marcha**: Cuando hay un residuo (`R > 0`), tras `N` ticks completos, un monitor asíncrono cambia el GMST a `R` para el último tick.



**Invalidación por cambio de celda (v4.1.6)**: el octavo sink (`TESCellFullyLoadedEvent`) invalida la caché de ratio ante cualquier transición de celda de manera ciega para evitar sobre-avances. Su coste de pérdida de SNR es despreciable frente al beneficio de prevención de errores al cambiar de ecosistema planetario.

### Sin hooks ni parches

El mod **no ejecuta código del motor** ni parchea funciones. Toda la interacción es a través de lectura/escritura del GameSetting y suscripción a eventos (API pública).

---

## Diagnóstico en arranque

En `kPostDataLoad`, antes de registrar los sinks, se ejecuta un conjunto de **probes de solo lectura** (`RunIdProbe`, `RunCacheProbe`, `RunSourceDump`, etc.) que confirman que cada ID resuelve al RVA esperado según el CSV 1.16.244 y que las vtables son válidas.

---

## Compatibilidad

* **Con mods que alteren `iSecondsToSleepPerUpdate**`: El último en escribir gana. GearShift reescribe el GMST al inicio de cada sesión.


* **Con mods de time scale global** (`SetGameTimeScale`): Sin conflicto.



---

## Desinstalación

1. Borra `NativeTimeAcceleration.dll` y `NativeTimeAcceleration.ini` de `<Starfield>/Data/SFSE/Plugins/`.
2. El juego vuelve a su comportamiento vanilla. No se escribe nada permanente en tu partida guardada.



---

## Detalles técnicos (para modders)

* **Runtime**: Starfield 1.16.244.0, SFSE 0.2.21, commonlibsf (2026-09).


* **Lenguaje**: C++23, xmake.


* **Resolución de IDs en el CSV**: El CSV tiene **dos columnas**: (1) ID, (2) RVA. La que se usa con `REL::ID` es **siempre la columna 1**.


* **Identificación de `TESCellFullyLoadedEvent**`: El catálogo RTTI de commonlibsf (2026-09) asocia erróneamente el ID 849714 al `_TypeDescriptor` del tipo. La instancia real del singleton se localizó con Ghidra en RVA `0x5977A90`, correspondiente al ID **838273**.


* **Eje UT sin redondear**: El `VerificationProbe` (v4.1.6) recibe y reporta la duración solicitada en UT conservada como `float` (sin el `lround` prematuro), preservando toda su parte fraccionaria para diagnosticar planetas con ratio < 1.


* **`NOMINMAX` global**: Definido en `xmake.lua` y `PCH.h` para evitar colisiones con `<Windows.h>`.



---

## Créditos

* **Autor**: concex1.


* **Reverse engineering**: Análisis RTTI con Ghidra sobre `Starfield.exe`.


* [Ghidra](https://github.com/NationalSecurityAgency/ghidra)



* **Referencias**:


* [SFSE](https://github.com/ianpatt/sfse)

* [CommonlibSF](https://github.com/libxse/commonlibsf)

* [Address Library for SFSE Plugins](https://www.nexusmods.com/games/starfield/mods)




El desarrollo de este mod contó con asistencia de modelos de lenguaje (Gemini Pro, DeepSeek) utilizados como copilotos de programación. La autoría del mod corresponde íntegramente a concex1.

---

## Licencia

Este proyecto está bajo la licencia GPL-3.0 o posterior, con excepción para modificaciones y excepción para vinculación (con el código fuente correspondiente).

En concreto, el código modificado es Starfield (y sus variantes) y las bibliotecas de modificación incluyen SFSE (GitHub). Dado que este mod está vinculado a CommonLibSF, hereda y se distribuye bajo los mismos términos de licencia que CommonLibSF.