# NativeTimeAcceleration — HANDOFF v4.1.6

> Documento maestro del estado del proyecto a fecha **2026-10-02**.
> Última release estable: **v4.1.6** (Arquitectura Híbrida: Classic + GearShift + Calibración Dinámica del ratio local/UT + invalidación de caché por cambio de celda + Probe completo con eje UT sin redondear + Probes de diagnóstico).
> Runtime objetivo: **Starfield 1.16.244.0**, SFSE 0.2.21, commonlibsf del 2026-09.
> Autor: **concex1**.

---

## 1. Qué es este mod

`NativeTimeAcceleration.dll` es un plugin SFSE que acelera el tiempo in-game del menú de **Dormir** y **Esperar** alterando el GameSetting `iSecondsToSleepPerUpdate` (tipo `int`, en segundos in-game por tick de UI).

Tiene dos modos de operación, seleccionables desde el INI:

| Modo        | Descripción | Estado |
|-------------|-------------|:------:|
| `Classic`   | Fija el GMST a `HoursPerTick * 3600 - 1` al cargar. Hotkeys. Cero dependencias de eventos. | ✅ Estable |
| `GearShift` | Motor con eventos + tick fijo + cambio de marcha al residuo + calibración dinámica + invalidación por cambio de celda. | ✅ Completo y validado |

En modo GearShift, la duración solicitada **D** (expresada por el motor en horas UT) se reparte en `N = floor(D_local / H)` ticks nominales de **H** horas locales más, si `R = D_local mod H > 0`, un último tick de **R** horas aplicado mediante un monitor asíncrono. Resultado: **cero pérdida de tiempo**, sin importar la duración ni el tamaño del tick.

**v4.1.x introduce la Calibración Dinámica**: el motor no conoce el ratio UT/local del planeta actual (es una propiedad no expuesta por la API), así que el mod lo **mide empíricamente** en el primer tick de cada sesión inyectando un "tick de sacrificio" de tamaño conocido. Esto permite operar correctamente en planetas donde 1 hora local equivale a 33 minutos UT (ratio < 1), 1 hora UT (ratio = 1) o 100 horas UT (ratio = 100).

**v4.1.6 introduce la invalidación de caché por cambio de celda**: el octavo sink (`TESCellFullyLoadedEvent`, ID 838273) invalida la caché de ratio ante cualquier transición de celda, eliminando el riesgo de sobre-avance cuando el jugador cambia de planeta entre sesiones de descanso. La revisión final de v4.1.6 incluye dos pulidos adicionales: `exchange(false)` en el handler del octavo sink (una sola línea de log por transición lógica) y la eliminación del `lround` prematuro en el eje UT del `VerificationProbe`.

---

## 2. Estado real de la aplicación

### 2.1. Qué funciona

- **Modo Classic.** Funcional desde v3.1.1, consolidado en v4.0.0. Cero dependencias de eventos. Aplica el tick configurado (`HoursPerTick`) más el micro-epsilon (`-1s`) al GMST. Hotkeys `Shift+T` (toggle) y `Shift+1..4` (cambio de velocidad) operativas. El monitor WinAPI solo se lanza si `bEnableGearShiftEngine = 0`.
- **Modo GearShift con eventos (Dormir).** Sink registrado en `BSTEventSource<TESSleepStartEvent>` (838515) y `BSTEventSource<TESSleepStopEvent>` (838519).
- **Modo GearShift con eventos (Esperar).** Sink registrado en `BSTEventSource<TESWaitStartEvent>` (838565) y `BSTEventSource<TESWaitStopEvent>` (838569).
- **GearShift opera exclusivamente por la ruta de eventos.** No existe ruta alternativa ni fallback. El registro de los ocho sinks es un requisito estructural del modo, no una opción de configuración.
- **Calibración Dinámica del ratio local/UT (v4.1.0+).** El mod mide el ratio empíricamente en el primer `HourPassed` de cada sesión:
  1. Al recibir `*StartEvent`, calcula `m_totalDurationHoursUT = (timeEnd - timeStart) * 24` **sin redondear** (float).
  2. Elige el tamaño del tick de sacrificio:
     - Si hay caché de ratio reciente (< 60 s) y `requestLocalEst = requestUT / cachedRatio >= H - margin`, sacrificio = `H`.
     - Si no, sacrificio = 1h (conservador).
  3. Inyecta `ApplyTick(sacrificio * 3600)`.
  4. En el primer `HourPassed`:
     - Mide `deltaUTHours = (now - start) * 24`.
     - Calcula `ratio = deltaUTHours / (sacrificio * 3600 - 1) / 3600` (compensando el epsilon `-1s` aplicado al GMST).
     - Sanitiza: si `ratio <= 0.001` → fallback a 1.0 con warning; si `ratio < 0.5` → warning de ratio sub-1.
     - Detecta cambio de planeta si la desviación respecto al ratio cacheado supera el 30%.
     - Actualiza la caché global (`g_cachedRatio`, `g_cachedRatioValid`).
     - Calcula `totalLocal = round(m_totalDurationHoursUT / ratio)`.
     - Notifica al probe vía `UpdateLocalDemands`.
     - Aplica `remainingLocal = totalLocal - sacrificio` con la aritmética habitual (D<H → ApplyTick directo; resto > 0 → ScheduleGearChange).
- **Caché de ratio con TTL (v4.1.0+).** El ratio medido se conserva durante 60 s reales tras el cierre de cada sesión. En sesiones subsiguientes del mismo planeta, esto permite usar sacrificio = `H` (mejor SNR: ~4× menos ruido de `float32` que con sacrificio = 1h). La caché se invalida por:
  - TTL expirado.
  - Detección de cambio de planeta (desviación > 30%).
  - Fallo de la sesión antes de la calibración.
  - **Cambio de celda (v4.1.6)**: invalidación ciega ante cualquier `TESCellFullyLoadedEvent`.
- **Invalidación de caché por cambio de celda (v4.1.6).** El octavo sink (`BSTEventSource<TESCellFullyLoadedEvent>`, ID 838273) invalida la caché de ratio ante cualquier transición de celda: cambio de planeta, viaje intra-planeta, entrada/salida de interiores y carga de partida. La invalidación es deliberadamente ciega. Su coste (pérdida del beneficio SNR del sacrificio variable hasta la próxima calibración) es despreciable frente al riesgo de sobre-avance por caché obsoleta: si la caché proviene de un planeta con ratio mucho menor, el sacrificio elegido puede exceder el request UT y hacer que el motor salga del bucle sin consumir ningún tick. La resolución del singleton requirió sortear un bug de commonlibsf (ID 849714 apunta al TypeDescriptor, no a la instancia). Ver §6.25.
  El handler usa `exchange(false)` para garantizar **una sola línea de log por transición lógica**, a pesar de que el motor despacha el evento varias veces por cambio de celda desde hilos concurrentes. Ver §6.27.
- **Consistencia de la selección de sacrificio (v4.1.2).** La comparación `requestLocalEst >= H` tolera un margen de `kSacrificeSelectionMargin = 0.01h` para absorber el ruido de `float32` en la división. Sin el margen, un mismo request de H horas locales alternaba entre sacrificio=1h y sacrificio=H según el último ratio cacheado, introduciendo inconsistencia en el log sin ganancia funcional.
- **Cambio de marcha al residuo.** Cuando `remainingLocal mod H = R > 0`, tras `N = floor(remainingLocal / H)` ticks completos de H horas, un monitor asíncrono cambia el GMST a R para que el motor consuma el resto en un tick final en lugar de descartarlo. El umbral del monitor se calcula en la escala UT del calendario: `thresholdDays = timeStart + ((N - 0.5) * H * 3600 * ratio) / 86400`.
- **Caso D < H.** Cuando `remainingLocal < H`, se aplica `ApplyTick(remainingLocal * 3600)` directamente. Sin esto, el motor no consumía el tick nominal y la sesión no avanzaba (bug detectado en v3.2.8, corregido en v3.2.9).
- **Sink de eventos de tiempo.** Se registran también `BSTEventSource<HourPassed::Event>` (839024), `BSTEventSource<HoursPassed::Event>` (839229) y `BSTEventSource<DaysPassed::Event>` (839020). Alimentan a `VerificationProbe`, que cuenta los tres tipos de eventos por sesión de descanso. Ver §6.16 para la semántica exacta de cada contador.
- **Interrupción validada.** El campo `interrupted` de `TESSleepStopEvent` / `TESWaitStopEvent` se lee correctamente desde el shim. Cuando el jugador interrumpe una sesión, el campo llega a `true`; en sesiones completas llega a `false`. El monitor del cambio de marcha se cancela limpiamente en ambos casos vía `GearShift::CancelGearChange()`.
- **Probes de diagnóstico (v4.0.0+).** `RunIdProbe`, `RunCacheProbe`, `DumpSingleton`, `RunSourceDump`, `RunSleepStopDump`, `RunWaitDump`, `RunTimeDump`, `RunCellFullyLoadedDump` se ejecutan en `kPostDataLoad` **antes** del registro de sinks. Sirven como sanity check en cada arranque: confirman que cada ID resuelve al RVA esperado según el CSV 1.16.244 y que las direcciones resueltas contienen objetos `BSTEventSource` válidos con vtable dentro del ejecutable. Solo leen, no mutan estado del motor.
- **Reporte completo de VerificationProbe (v4.0.0+, ampliado en v4.1.0 y v4.1.6).** `OnSleepStart` emite `[Probe][START]` con `daysNow` y `requestedUT`. `OnSleepStop` emite `[Probe][STOP]` con `interrupted`, `daysNow`, `deltaUT`, `deltaLocal`, `requestedLocal`, `ratio`, `tolerance`, `counts(Hour=…, Hours=…, Days=…)` y `wall` en ms. Añade aviso específico si la sesión fue interrumpida y aviso de `DIVERGENCIA` si `|deltaLocal - requestedLocal| > tolerance`.
  **Eje UT sin redondear (v4.1.6)**: `OnSleepStart` recibe la duración UT como `float` sin redondear, y el `[Probe][START]` la emite con cuatro decimales. Anteriormente un request de 7h locales en un planeta con ratio 0.54 aparecía como `requestedUT=4h` (lround de 3.7910), aunque el `[Probe][STOP]` posterior corregía la cifra vía `UpdateLocalDemands`. El cambio es puramente cosmético sobre el log. Ver §6.28.
  **Tolerancia proporcional (v4.1.0)**: `tolerance = max(0.05h, 0.01 * requestedLocal)`. Absorbe el ruido de `float32` acumulado proporcionalmente al tamaño del request, sin enmascarar desviaciones reales.
- **Reporte de configuración completa (v4.0.0+).** La línea `[Config] Análisis de pre-vuelo completado` del arranque enumera **todas** las opciones efectivas: `bEnableGearShiftEngine`, `HoursPerTick`, `bEnableVerificationProbe`, `bUseCachedGlobal` (marcado como bloqueado de fábrica) y las hotkeys.
- **Log limpio (v4.1.6).** Con `bEnableVerificationProbe=0`, el mod emite únicamente las líneas de arranque (autoría, configuración, probes, dumps de singletons, confirmación de registro) y las líneas de calibración por sesión de descanso. No hay instrumentación `[Trace]` activa en producción (ver §6.26). Cada transición de celda produce **exactamente una línea** de invalidación (ver §6.27).

### 2.2. Qué NO funciona / limitaciones conocidas

- **Requests "redondos" de gran magnitud en el motor vanilla.** Con `HoursPerTick=4` y `HoursPerTick` nominal, ciertos requests redondos (por ejemplo 100h local en planetas de ratio < 1) hacen que el motor salga del bucle antes de procesar ningún tick. Síntoma: `wall ~1000ms, HourPassed totales = 0, deltaLocal = 0`. No es un bug del mod; el mod hace lo correcto (aplica el sacrificio, espera el `HourPassed`, no llega, no calibra). Workaround del usuario: usar valores no exactamente redondos (99h o 101h). Pendiente de caracterizar con más tests para identificar el conjunto exacto de valores afectados.
- **Escritura de la caché interna `g_iSecondsToSleepLow`.** `bUseCachedGlobal` está **bloqueado de fábrica en false** en `Config.h`. La CacheProbe demuestra que el ID 881128 contiene un puntero (`0x7FF6...`), no un `int32_t`. El ID real sigue pendiente. No es prioritario: los tests demuestran que `SetSetting` sobre el GMST **sí** afecta al tick real del motor.
- **Rango de `HoursPerTick` restringido a [1-4].** Sanitización en `Config.h::LoadConfig`. Valores fuera del dominio se reconvierten a 3 con log de advertencia. El dominio se eligió por estabilidad matemática (ver §6.2).
- **Sin ruta de contingencia si los eventos no están disponibles.** GearShift depende estructuralmente de los ocho singletons. Si `RegisterSleepWaitSink` falla, el modo no opera. Es una decisión de diseño consciente (§6.19), no una limitación accidental.
- **Ratio local/UT no extraído del binario.** El cluster de IDs 848336–848344 identificado por el usuario corresponde a RTTI TypeDescriptors y vtables del subsistema `TimeMultiplierManager`, no a la instancia del singleton. La ruta de extracción directa se abandonó en favor de la calibración empírica (§6.20). El ID 848339 en concreto apunta a un `_TypeDescriptor` con nombre `.?AVTimeMultiplierManager@@`, confirmado por dump en runtime.
- **Precisión en planetas con ratio ≈ 1.** En planetas 1:1, el ruido de `float32` en `Calendar::GetDaysPassedExact()` limita la precisión de la calibración a ±0.5% cuando el sacrificio es 1h, y ±0.15% cuando el sacrificio es 4h (caché caliente). En planetas con ratio > 1 el ruido relativo es despreciable. Impacto: la métrica `deltaLocal` puede desviarse hasta ~0.2% del valor solicitado en 1:1 con caché fría. No degrada la experiencia de juego.

### 2.3. Correcciones de diseño respecto a V3

La premisa original del documento `Los dos IDs confirmados.txt` decía que GearShift debía **repartir la duración solicitada en N ticks proporcionales** (ej. 6h con H=4 → 2 ticks de 3h). Esto era **incorrecto**:

- Rompe el modelo "cambio de marcha" (ticks de tamaño variable desde el principio).
- No es el comportamiento del motor original.
- Fue corregido en v3.2.2: **tick FIJO = H×3600** para todos los ticks intermedios, con el cambio de marcha actuando solo en el último.

`ComputeOptimalTickSeconds` fue eliminado del API de `GearShiftEngine` en esa corrección.

### 2.4. Shim de tipos Wait (`WaitEventsShim.h`)

A fecha 2026-09, commonlibsf **no define `RE::TESWaitStartEvent` ni `RE::TESWaitStopEvent`**, aunque el binario del juego sí los tiene. Sus equivalentes de Sleep sí están expuestos.

El proyecto declara un shim local en `WaitEventsShim.h` que replica el layout mínimo necesario (mismos campos que Sleep: `timeStart`, `timeEnd`, `interrupted`). Este header debe eliminarse cuando commonlibsf añada los tipos reales, y sustituirse por el include correspondiente.

**Validación completa**: los logs confirman que el shim tiene el layout correcto. Las duraciones se leen con precisión (1h, 6h, 7h, 11h, 17h, etc.) y el campo `interrupted` refleja correctamente el estado de la sesión (`true` al interrumpir, `false` al completar).

### 2.5. NOMINMAX global (v4.1.2)

A partir de v4.1.2, `NOMINMAX` se define en **dos puntos** para cubrir todas las rutas de compilación:

- `xmake.lua` → `add_defines("NOMINMAX")` en el target `NativeTimeAcceleration` (solo en este target; las librerías externas conservan su configuración).
- `include/PCH.h` → `#ifndef NOMINMAX / #define NOMINMAX / #endif` como primera línea efectiva tras `#pragma once`.

Motivo: `<Windows.h>`, arrastrado transitivamente por `<RE/Starfield.h>`, define macros `min` y `max` que colisionan con `std::min`/`std::max`. Sin `NOMINMAX`, cualquier uso no parentizado produce `error C2589`. La doble definición (guardada por `#ifndef`) evita `warning C4005` de redefinición y cubre compilaciones que no procesen el `xmake.lua`.

---

## 3. Arquitectura del código (build actual)

### 3.1. En build

| Archivo | Rol |
|---|---|
| `src/Main.cpp` | Entry points SFSE, logging con autoría, hotkeys (Classic), probes de diagnóstico (`RunIdProbe`, `RunCacheProbe`, `RunSourceDump`, `RunSleepStopDump`, `RunWaitDump`, `RunTimeDump`, `RunCellFullyLoadedDump`, `DumpSingleton`). |
| `src/GearShiftEngine.cpp` | Aplica el GMST y contiene el monitor asíncrono del cambio de marcha (`ScheduleGearChange` / `CancelGearChange`). Sin ruta de polling (eliminada en v4.0.0, §6.19). |
| `src/SleepWaitEventSink.cpp` | Sink unificado Sleep+Wait+Time+Cell. Registra en las ocho fuentes confirmadas. Implementa la Calibración Dinámica (tick de sacrificio + caché con TTL + detección de cambio de planeta + invalidación por cambio de celda con `exchange(false)`). Dispatch a `VerificationProbe` con eje UT sin redondear. |
| `src/VerificationProbe.cpp` | Cuenta HourPassed/HoursPassed/DaysPassed por sesión. Reporte completo en Start/Stop con tolerancia proporcional. Eje UT conservado como `float` sin redondear. Filtra el ruido fuera de sesión. No-op si `bEnableVerificationProbe=false`. |
| `include/Config.h` | Estructura de estado estático con `std::atomic`. Lectura del INI vía `GetPrivateProfileIntA`. Sanitización de `HoursPerTick` en [1-4] y `vanillaSeconds` fijo en 3600. `bUseCachedGlobal` bloqueado en false. Reporte de pre-vuelo con las opciones efectivas. |
| `include/EventSources.h` | Mapeo de IDs a los singletons `BSTEventSource<>`. Documentación extensa de la metodología RTTI y de las trampas del CSV. Incluye la resolución del octavo singleton (ID 838273) sorteando el bug de commonlibsf. |
| `include/GearShiftEngine.h` | API pública del motor (Initialize, ApplyTick, ApplyEpsilon, Set/GetNominalHours, ScheduleGearChange, CancelGearChange). Documenta la invariante del motor. |
| `include/PCH.h` | Cabecera precompilada. Define `NOMINMAX` antes de cualquier inclusión que arrastre `<Windows.h>`. Agrupa SFSE, commonlibsf, spdlog, Windows, STL. |
| `include/SleepWaitEventSink.h` | Declaración del sink unificado. Miembros privados de la máquina de calibración (`m_isCalibrating`, `m_calibrationStartDays`, `m_totalDurationHoursUT`, `m_sacrificeLocalHours`) y miembros públicos (`lastStartDays`, `lastEndDays`, `sessionActive`). Métodos privados `SelectSacrificeHours` y `CompleteCalibration`. |
| `include/VerificationProbe.h` | API del probe (Initialize, OnSleepStart, UpdateLocalDemands, OnSleepStop, OnHourPassed, OnHoursPassed, OnDaysPassed, RegisterAuxiliarySinks). La firma de `OnSleepStart` recibe la duración UT como `float` sin redondear (v4.1.6). |
| `include/WaitEventsShim.h` | Shim local de `TESWaitStartEvent` / `TESWaitStopEvent`. |
| `NativeTimeAcceleration.ini` | Configuración de usuario. |
| `xmake.lua` | Orquestación de compilación. Define `NOMINMAX` solo en el target principal. `set_version("4.1.6")`. `set_languages("c++23")`. |

### 3.2. Fuera del build

Ninguno. Todos los `.cpp` están en el build.

### 3.3. Estado interno del sink (miembros de la clase)


```

lastStartDays            float     -- daysUT de inicio (público)
lastEndDays              float     -- daysUT proyectado (público)
sessionActive            bool      -- bandera activa de sesión (público)
m_isCalibrating          bool      -- true entre *StartEvent y el primer HourPassed (privado)
m_calibrationStartDays   float     -- daysUT en el momento del arranque (privado)
m_sacrificeLocalHours    int32     -- sacrificio elegido para esta sesión (1 o H) (privado)
m_totalDurationHoursUT   float     -- duración UT del evento, sin redondear (privado)

```

**Nota (v4.1.6)**: los campos `m_sessionStartReal`, `m_sessionStartDaysUT` y `m_hourPassedIndex` se eliminaron junto con la instrumentación `[Trace]` que los consumía. Ver §6.26.

### 3.4. Estado global del sink (namespace anónimo)


```

g_cachedRatio            atomic  -- ratio UT/local medido en la última sesión
g_cachedRatioValid       atomic   -- validez de la caché
g_lastSessionEndTime     time_point     -- marca del último *StopEvent

kCacheTTL                        = 60 s
kRatioAnomalyThreshold           = 0.001
kRatioLowThreshold               = 0.5
kPlanetChangeDeviation           = 0.30
kSacrificeSelectionMargin        = 0.01h

```

### 3.5. Estado interno del probe (namespace anónimo)


```

g_session.sessionActive          bool      -- true entre OnSleepStart y OnSleepStop
g_session.startDays              float     -- daysUT al inicio de la sesión
g_session.requestedHoursUT       float     -- duración UT sin redondear (v4.1.6)
g_session.requestedLocalHours    int32     -- duración local calculada por la calibración
g_session.ratio                  float     -- ratio medido por la calibración
g_session.hourPassedCount        int       -- ticks del bucle interno del motor
g_session.hoursPassedCount       int       -- horas in-game en eje local
g_session.daysPassedCount        int       -- cruces de medianoche
g_session.startTime              time_point -- marca para el cálculo de wall

```

---

## 4. IDs confirmados y pendientes

### 4.1. Confirmados (verificados con `TypeOfSingleton.py`, `FindSingletonsByName.py` y `FindBSTEventSourceSingleton.py` sobre el binario)

| Símbolo                                          | RVA         | ID       | RTTI                                                   |
|--------------------------------------------------|------------:|---------:|--------------------------------------------------------|
| `BSTEventSource<TESSleepStartEvent>`             | `0x59783C8` | `838515` | `.?AV?$BSTEventSource@UTESSleepStartEvent@@@@`         |
| `BSTEventSource<TESSleepStopEvent>`              | `0x59783F0` | `838519` | `.?AV?$BSTEventSource@UTESSleepStopEvent@@@@`          |
| `BSTEventSource<TESWaitStartEvent>`              | `0x59785A8` | `838565` | `.?AV?$BSTEventSource@UTESWaitStartEvent@@@@`          |
| `BSTEventSource<TESWaitStopEvent>`               | `0x59785D0` | `838569` | `.?AV?$BSTEventSource@UTESWaitStopEvent@@@@`           |
| `BSTEventSource<DaysPassed::Event>`              | `0x5979DD0` | `839020` | `.?AV?$BSTEventSource@UEvent@DaysPassed@@@@`           |
| `BSTEventSource<HourPassed::Event>`              | `0x5979DF8` | `839024` | `.?AV?$BSTEventSource@UEvent@HourPassed@@@@`           |
| `BSTEventSource<HoursPassed::Event>`             | `0x597A7F0` | `839229` | `.?AV?$BSTEventSource@UEvent@HoursPassed@@@@`          |
| `BSTEventSource<TESCellFullyLoadedEvent>`        | `0x5977A90` | `838273` | `.?AV?$BSTEventSource@UTESCellFullyLoadedEvent@@@@`    |

**Patrón Start/Stop y contigüidad**: cada par Start/Stop está a `+0x28` bytes. Cada objeto `BSTEventSource<>` ocupa `0x28` bytes y está indexado en el CSV con 4 entradas consecutivas (start + 3 campos).

**Nota sobre el octavo singleton (v4.1.6)**: la RVA 0x5977A90 se localizó con `FindBSTEventSourceSingleton.py` caminando la cadena RTTI: `TypeDescriptor 0x145A71570` → `CompleteObjectLocator 0x145038E18` → `Vtable 0x144B98770` → `Singleton 0x145977A90`.

### 4.2. Falsos positivos descartados

| RVA            | ID       | RTTI real                  | Motivo de descarte  |
|----------------|---------:|----------------------------|---------------------|
| `0x5978418`    | `838523` | `TESSpellCastEvent`        | No relacionado      |
| `0x5978440`    | `838527` | `TESSpellCastFailureEvent` | No relacionado      |
| `0x5977D60`    | ?        | `TESEscortWaitStartEvent`  | Escolta, no jugador |
| `0x5977D88`    | ?        | `TESEscortWaitStopEvent`   | Escolta, no jugador |
| `0x5A71570`    | `849714` | `_TypeDescriptor` de `BSTEventSource<TESCellFullyLoadedEvent>` | **Apuntado por commonlibsf como si fuera el singleton**. Ver §4.5 y §6.25. |

La región `0x59783xx`–`0x597A7xx` mezcla eventos de familias distintas. **No sirve la búsqueda por proximidad de direcciones**; hay que usar RTTI.

### 4.3. Falsos positivos adicionales: cluster TimeMultiplierManager

El cluster `848336`–`848344` fue explorado como candidato para extraer el ratio local/UT directamente. El dump en runtime demuestra que **el ID 848339 apunta a un `_TypeDescriptor`**, no a la instancia del singleton `TimeMultiplierManager`:


```

[TimeMultiplier] TimeMultiplierManager @ 0x7FF790D480B0
[TimeMultiplier]   +00: 0x00007FF78FD324C8   ← vtable de type_info
[TimeMultiplier]   +08: 0x0000000000000000
[TimeMultiplier]   +10: 0x656D695456413F2E   ← ".?AVTi" (ASCII)
[TimeMultiplier]   +18: 0x696C7069746C754D   ← "Multipli"
[TimeMultiplier]   +20: 0x6567616E614D7265   ← "erManage"
[TimeMultiplier]   +28: 0x0000000000404072   ← "r@@" (sufijo de cierre)

```

La cadena ASCII forma `.?AVTimeMultiplierManager@@`, el nombre RTTI demangado. La ruta de extracción se abandonó en favor de la calibración empírica (§6.20).

### 4.4. Pendientes

| Símbolo                             | RVA | ID    |
|-------------------------------------|----:|------:|
| `g_iSecondsToSleepLow` (caché real) | ?   | ?     |

No es prioritario: los tests demuestran que `SetSetting` sobre el GMST ya afecta al tick real.

### 4.5. Etiquetas incorrectas del rename dump y del catálogo RTTI de commonlibsf

**Rename dump (región de funciones):**
- ID `107199` "TESSleepStartEvent::GetEventSource" → RVA `0x1C21F50`, tiene stack frame completo, **no es un getter**.
- ID `107200` "TESSleepStopEvent::GetEventSource" → análogo.
- ID `107211` "TESWaitStartEvent::GetEventSource" → análogo.
- ID `881128` "g_iSecondsToSleepLow" → RVA `0x5D940C0`, contiene un puntero, no un `int32_t`.

**Catálogo RTTI de commonlibsf (región de singletons):**
- ID `849714` "`BSTEventSource_TESCellFullyLoadedEvent_`" → RVA `0x5A71570`. **Apunta al `_TypeDescriptor` del tipo, no a la instancia del singleton**. `RegisterSink` sobre ese puntero cuelga el hilo de carga de partida porque trata los bytes del `_TypeDescriptor` como si fueran la estructura interna del `BSTEventSource` (vtable + `[count, cap]` + buffer). La instancia real está en RVA `0x5977A90` (ID `838273`). Ver §6.25.

**Regla de oro (ampliada en v4.1.6)**: nunca confiar en una etiqueta del rename dump **ni en una entrada del catálogo RTTI de commonlibsf** sin verificarla contra el CSV de la versión objetivo (`offsets-1-16-244-0.txt` de Address Library for SFSE Plugins) más un walk RTTI sobre el binario real. La confusión TypeDescriptor↔instancia es un patrón recurrente que ya se había visto en el cluster `TimeMultiplierManager` (§4.3) y que se repite con `TESCellFullyLoadedEvent`.

**Regla adicional (lección v3.2.11)**: el CSV tiene **dos columnas**: (1) ID, (2) RVA. La que se usa con `REL::ID` es **siempre la columna 1**.

---

## 5. Metodología Ghidra (validada)

El procedimiento que funcionó para localizar los ocho singletons:
1. Abrir `Starfield.exe` en Ghidra.
2. Ejecutar `TypeOfSingleton.py` con las direcciones conocidas → confirma los nombres RTTI.
3. Ejecutar `FindSingletonsByName.py` con uno o varios substrings → encuentra candidatos por nombre.
4. Cruzar los RVAs con el CSV → obtener los IDs.
5. Actualizar `EventSources.h` con los IDs.
6. Recompilar, probar, verificar con dumps post-register.

Requisitos técnicos:
- Ghidra 11.x con `PyGhidra` instalado (`pip install pyghidra`).
- Lanzar Ghidra con `pyghidraw.exe` (en `%APPDATA%\Python\Python313\Scripts\`), **no** con el acceso directo estándar (que no engancha Python al proceso).
- Los scripts van en `%USERPROFILE%\ghidra_scripts\`.

Scripts auxiliares:
- `TypeOfSingleton.py` — recibe RVAs y devuelve el nombre RTTI.
- `FindSingletonsByName.py` — recibe uno o varios substrings y devuelve todos los candidatos (`SINGLETON?`) con su RVA y `[count, cap]`.
- `FindEventSources.py` — recorre todos los strings RTTI que contienen `BSTEventSource` y devuelve vtables y singletons para cada uno.
- `FindBSTEventSourceSingleton.py` (**nuevo en v4.1.6**) — recorre explícitamente los candidatos por xref a la vtable, partiendo del `TypeDescriptor` del tipo objetivo. Devuelve la instancia del singleton en `.data` y clasifica los falsos positivos (por ejemplo, código en `.text` que contiene el patrón por casualidad). Imprescindible para sortear los casos donde `FindSingletonsByName.py` devuelve tanto la instancia como el `_TypeDescriptor` del mismo tipo.

---

## 6. Historial de descubrimientos

### 6.1. Rename dump desactualizado
El archivo `starfield.rename.txt` tiene al menos una etiqueta mala documentada: decía `33961 = MemoryManager::GetSingleton`, pero el CSV 1.16.244 dice `35721 = MemoryManager::GetSingleton`.

### 6.2. Fórmula del reparto exacto era errónea
Corregido en v3.2.2: el motor debe usar tick fijo = H×3600, no reparto proporcional. Ver §2.3.

### 6.3. Poller de respaldo: introducción y eliminación posterior
El poller asíncrono se introdujo en v3.1.5 como capa de robustez cuando la ruta de eventos aún no estaba validada. Se eliminó por completo en v4.0.0. Ver §6.19.

### 6.4. Verificación RTTI sobre el binario real
Los IDs de singleton no se pueden deducir por proximidad de direcciones: la región `0x59783xx` mezcla eventos de distintas familias. Hay que caminar RTTI (`TypeOfSingleton.py`, `FindSingletonsByName.py`).

### 6.5. Validación empírica del ciclo Dormir (v3.2.6)
| Test                   | Duración | Ticks  | Real avanzado | Resto |
|------------------------|---------:|-------:|--------------:|------:|
| Dormir 6h con H=4      | 6h       | 1 × 4h | 3.9961h       | 2h    |
| Dormir 8h con H=4      | 8h       | 2 × 4h | 7.9980h       | 0h    |

Desviaciones <21s están dentro del ruido de `float32` del calendario.

### 6.6. Confirmación del patrón Start/Stop
Los pares Start/Stop de Sleep y Wait están ambos a `+0x28` bytes.

### 6.7. Ausencia de TESWaitStartEvent en commonlibsf
El bundle de headers de commonlibsf (2026-09) no incluye `RE::TESWaitStartEvent` ni `RE::TESWaitStopEvent`. Se resuelve con el shim local `WaitEventsShim.h`. Ver §2.4.

### 6.8. Cambio de marcha al residuo (v3.2.8)
Implementado como monitor asíncrono que sondea `Calendar::GetDaysPassedExact()` cada 50 ms. Mecanismo:
1. En el handler de `*StartEvent`, tras aplicar el tick nominal, si `D mod H = R > 0` se llama a `ScheduleGearChange`.
2. Se publica un ID de sesión monótono en `g_activeGearSessionId`.
3. El monitor calcula el umbral y sondea cada 50 ms.
4. Al alcanzar el umbral, CAS de `sessionId` a `0` y `ApplyTick(R*3600)`.
5. En `*StopEvent` se llama a `CancelGearChange`.

**Hipótesis validada en v3.2.9**: el motor relee el GMST en cada iteración del bucle interno.

### 6.9. Validación empírica del ciclo Esperar (v3.2.7b)
Idéntico al ciclo Dormir. Confirma la validez de los IDs 838565/838569.

### 6.10. Validación completa del cambio de marcha (v3.2.9)
Cero pérdida de tiempo en todas las combinaciones. Desviaciones máximas <20s, atribuibles a la granularidad de `float32` del calendario.

### 6.11. Bug D < H (v3.2.9)
Detectado en v3.2.8: con `HoursPerTick=4`, al dormir o esperar 1h, 2h o 3h, el tick nominal (4h) se aplicaba al GMST pero el motor nunca lo consumía porque `D < H` → 0 iteraciones.
Corregido en v3.2.9: cuando `D < H`, se aplica `D` directamente como tick.

### 6.12. Margen de seguridad en el umbral del monitor (v3.2.10)
Se introdujo un margen de 60s in-game restado al umbral del monitor como defensa pasiva contra el ruido de `float32`.

### 6.13. Sink de eventos de tiempo (v3.2.11)
Los tres eventos de tiempo (`HourPassed`, `HoursPassed`, `DaysPassed`) se localizaron con `FindSingletonsByName.py` en modo multi-substring.

### 6.14. Fix del probe: contadores y filtrado de logs (v3.2.12)
**Síntoma**: `[Probe][HOUR]` y `[Probe][HOURS]` aparecían miles de veces por minuto, incluso fuera de sesiones.
**Fix**: campo `sessionActive` en `SessionStats`. Los tres handlers salen silenciosamente si no hay sesión.

### 6.15. Validación completa de v3.2.13
17 combinaciones validadas. Cero pérdida de tiempo. Deltas <20s.

### 6.16. Semántica real de HourPassed / HoursPassed / DaysPassed (v3.2.13)
| Evento               | Qué cuenta                                                      |
|----------------------|-----------------------------------------------------------------|
| `HourPassed::Event`  | **Iteraciones del bucle interno del motor** (ticks consumidos). |
| `HoursPassed::Event` | **Horas in-game transcurridas** (delta_real).                   |
| `DaysPassed::Event`  | **Cruces de medianoche del calendario**.                        |

#### 6.16.1. `HoursPassed` rezagado en interrupciones abruptas (v4.0.0)
En sesiones interrumpidas por el jugador, `HoursPassed` puede quedar rezagado respecto a `deltaReal`. No es un bug del mod; es una característica del motor vanilla.

### 6.17. Limpieza de código muerto (v3.2.13)
Eliminados `RunSinkProbe`, `DummySink`, `ExtractSingletonFromPrologue`, `PollInternalState`, `LowListSleeping`, `LowListTimeout`, `CachedSecondsToSleep`.

### 6.18. Restauración de probes y reporte completo en v4.0.0
Reincorporación de `RunIdProbe`, `RunCacheProbe`, dumps de singletons. Reporte completo de `VerificationProbe`. Autoría visible en log.

### 6.19. Consolidación de la ruta de eventos única en v4.0.0
Eliminación de `bEnableEventSinks` y `bPollingFallback`. GearShift tiene una única ruta de operación: los sinks. Si los eventos no están disponibles, GearShift no opera y el log lo reporta como error crítico.
**Principio de diseño**: las opciones que solo pueden degradar el modo no pertenecen al INI.

### 6.20. Calibración Dinámica del ratio local/UT (v4.1.0)
**Contexto**. El motor reporta las duraciones solicitadas en **horas UT** (vía `timeEnd - timeStart`), pero el GMST `iSecondsToSleepPerUpdate` se interpreta en **segundos locales**. El factor de conversión `ratio = UT_horas / local_horas` es una propiedad del planeta y **no está expuesto por la API pública del juego**.

**Consecuencia**: cuando el ratio difiere de 1.0, el mod interpretaba mal la duración. Ejemplos:
- Planeta 1:100 (Venus-like): pedir "1h local" llega como 100h UT. El mod lo trataba como 100h locales, ponía GMST=4h, y el motor veía que el próximo tick (400h UT) excedía las 100h UT solicitadas → 0 ticks ejecutados, `deltaReal = 0`.
- Planeta 1:0.54 (1 hora local = 33 min UT): pedir "1h local" llega como 0.54h UT. `lround(0.54) = 1`, y luego `round(1 / 0.54) = 2h` → sobre-avance de 1h local.

**Estrategia adoptada**. En lugar de extraer el ratio del binario, se mide empíricamente:
1. Inyectar un tick de sacrificio de tamaño conocido (1h local).
2. Esperar al primer `HourPassed`, que marca el fin de ese tick.
3. Medir el delta real del calendario en UT y dividir por las horas locales efectivamente consumidas (descontando el epsilon `-1s`).
4. Cachear el ratio durante 60 s reales.

**Rutas de extracción alternativas descartadas**:
- **`TimeMultiplierManager`** (cluster 848336–848344): el ID 848339 apunta a un RTTI TypeDescriptor, no a la instancia del singleton. La ruta directa se abandonó. Ver §4.3.
- **Lectura del GMST tras la UI**: la UI muestra "1 HORA LOCAL (33 MINUTOS UT)" en el menú, pero no hay API pública para leer esos valores.

### 6.21. Bug A: redondeo prematuro de la duración UT (corregido en v4.1.1)
**Descripción**: el campo `m_totalDurationHoursUT` se calculaba como `int32_t` con `lround(durationDays * 24.0f)`. En planetas con ratio < 1, una duración de 0.5391h UT se redondeaba a 1, destruyendo la información de que la duración real era menor a 1h UT.
**Efecto**: la reconstrucción posterior `totalLocal = round(1 / ratio)` producía `totalLocal = 2h` en lugar del correcto `1h`. Sobre-avance de 1h local en cada request.
**Fix (v4.1.1)**: cambiar el tipo a `float` y mover el `lround` al punto final del cálculo, después de la división por el ratio.

### 6.22. Bug B: race condition en la aplicación del residuo (diagnóstico)
**Descripción inicial (v4.1.0)**: en el planeta con ratio < 1, sesiones idénticas (request 2h local, sacrificio 1h, residuo 3h) producían resultados alternos: unas completaban con `deltaUT = 2.1680h` (éxito), y otras salían del bucle con `deltaUT = 0.5391h` (fallo, el residuo no se consumía). El motor procesaba 2 `HourPassed` en el caso correcto y 1 en el caso de fallo.
**Diagnóstico final**: Bug B era un **síntoma secundario de Bug A**. Con el redondeo prematuro, ciertas combinaciones calculaban `totalLocal` inflado o desajustado respecto al sacrificio real, y el timing de la aplicación del residuo caía ocasionalmente en una ventana donde el motor salía del bucle antes de consumirlo .
**Validación post-fix (v4.1.1)**: 40 sesiones en 3 planetas, incluyendo 9 sesiones con `sacrificio=1h` y `totalLocal=4h` en el planeta ratio<1 (mismo escenario que antes fallaba ~50%). **0/40 fallos**. El motor siempre procesó los `HourPassed` esperados y consumió el residuo íntegramente.

### 6.23. Planetas con ratio UT/local < 1 existen (confirmado v4.1.1)
**Contraejemplo empírico**: el usuario reportó un planeta con la relación **1 hora local = 33 minutos UT** (ratio ≈ 0.55). La UI del menú de espera lo confirma: `1 HORA LOCAL ( 33 MINUTOS UT )`. El log de calibración mide un ratio estable de ~0.5392–0.5451 a lo largo de múltiples sesiones.
**Consecuencias de diseño**:
- **No se aplica el snappear a 1.0.** El rango de ratios es `(0, ∞)`, no `[1.0, ∞)`.
- **El umbral `kRatioLowThreshold = 0.5`** sigue emitiendo un warning cuando el ratio cae por debajo, pero no es un error.
- **El caso D < H se manifiesta de forma distinta**: en planetas con ratio < 1, un request de 1h local puede llegar como 0.54h UT, y `remainingLocal < H` se detecta correctamente tras la calibración.

### 6.24. Fix de consistencia en la selección de sacrificio (v4.1.2)
**Causa**: `float32` en la división `requestUT / ratioCacheado`. La imprecisión de ±0.01h en `requestLocalEst` es suficiente para cruzar el umbral cuando el request está exactamente en H.
**Fix (v4.1.2)**: nuevo margen de tolerancia `kSacrificeSelectionMargin = 0.01f` en la comparación.

### 6.25. Invalidación de caché por cambio de celda (v4.1.6)
**Contexto**: con la caché de ratio vigente durante 60 s, existía un riesgo de sobre-avance al cambiar de planeta entre sesiones dentro de esa ventana.
**Resolución del ID (sortear bug de commonlibsf)**: el catálogo RTTI de commonlibsf (2026-09) asocia el ID **849714** a `BSTEventSource_TESCellFullyLoadedEvent_`, pero ese ID apunta al **`_TypeDescriptor` del tipo** (RVA `0x5A71570`), no a la instancia singleton. La instancia real se localizó con Ghidra (RVA `0x5977A90`), correspondiente al ID **838273**.
**Coste asumido**: pérdida del beneficio SNR del sacrificio variable hasta la próxima calibración. Coste despreciable frente al riesgo de sobre-avance.

### 6.26. Retirada de la instrumentación `[Trace]` (v4.1.6)
**Contexto**: la instrumentación `[Trace]` introducida para diagnosticar Bug B perdió su valor diagnóstico al corregirse Bug A.
**Retirada (v4.1.6)**: se eliminaron las emisiones `[Trace]` en `SleepWaitEventSink::ProcessEvent` y los campos privados que las alimentaban (`m_sessionStartReal`, `m_sessionStartDaysUT`, `m_hourPassedIndex`). Sin cambios funcionales, el efecto es puramente cosmético sobre el log de producción.

### 6.27. `exchange(false)` en el handler del octavo sink (v4.1.6, revisión final)
**Contexto**: la primera versión usaba `load() + store(false)`, lo que provocaba triples invalidaciones concurrentes de log en la misma transición de celda.
**Fix**: sustituir el patrón por un único `exchange(false)`. Solo un hilo puede observar el valor previo `true`; los demás observan `false` y salen sin emitir log. El comportamiento funcional quedó idéntico; solo la firma del log cambió a una línea única.

### 6.28. Eliminación del `lround` prematuro en el eje UT del probe (v4.1.6, revisión final)
**Contexto**: el `[Probe][START]` reportaba `requestedUT` previamente truncado a `int32_t` mediante `lround`, lo cual era engañoso en planetas con ratio < 1.
**Fix**: la duración se conserva y transmite como `float` sin redondear, preservando toda su parte fraccionaria para el reporte.

---

## 7. Estado del INI (v4.1.6)

El INI de V4 usa **enteros (1/0)** para los booleanos, no cadenas `true/false`. Esto es coherente con `ModConfig::LoadConfig()`, que usa `GetPrivateProfileIntA`.

```ini
[Settings]
; 0 = Modo Clásico; 1 = Motor de Cambio de Marcha (GearShift)
bEnableGearShiftEngine=1

; Rango válido: 1..4. Valores fuera del dominio → reconversión a 3.
HoursPerTick=3

; Probe de verificación empírica. 2-3 líneas por sesión de descanso,
; cero fuera de sesión. Útil para validación; irrelevante para juego normal.
bEnableVerificationProbe=0

; NOTA: no existe clave para los sinks de eventos. En GearShift se registran
;       siempre los ocho BSTEventSource<> como requisito estructural del modo.
;       En Classic no se registra ninguno. Ver §6.19 y §6.25.
;
; NOTA: no existe clave de polling fallback. El subsistema fue eliminado en
;       v4.0.0. La única vía de detección de sesión son los ocho sinks. Ver §6.19.
;
; NOTA: bUseCachedGlobal no está expuesto en el INI. Config.h lo bloquea
;       de fábrica en false por seguridad (ver §2.2).

[Hotkeys]
; Solo se procesan si bEnableGearShiftEngine=0.
ModifierKey=16
MainKey=84

```

---

## 8. Autoría y créditos



### 8.1. Autor humano



* **Autor**: concex1.


* **Reverse engineering**: análisis RTTI con Ghidra sobre `Starfield.exe`.
- [Ghidra](https://github.com/NationalSecurityAgency/ghidra)

* **Referencias**: 
- [SFSE](https://github.com/ianpatt/sfse)
- [CommonlibSF](https://github.com/libxse/commonlibsf)
- [Address Library for SFSE Plugins](https://www.nexusmods.com/games/starfield/mods/3256)


### 8.2. Asistencia de IA en el desarrollo



El desarrollo de este mod contó con asistencia de modelos de lenguaje utilizados como copilotos de programación. La autoría del mod corresponde íntegramente a concex1, documentando la participación de IA (Gemini Pro, DeepSeek) como parte de la trazabilidad del proyecto.

---

## 9. Estado de validación



### 9.1. a 9.5. Resumen de iteraciones de v4



Las validaciones confirman la completa operatividad del sistema en todas sus configuraciones. Las modificaciones finales (`exchange`, retirada de `[Trace]`, eje UT sin redondear) no introdujeron regresiones en las sesiones de validación. El octavo sink opera correctamente y la invalidación de caché funciona con firma limpia de una línea por transición.

### 9.6. Pendientes para la próxima iteración (v4.2.0)



1. Evaluar la retirada del shim `WaitEventsShim.h` cuando commonlibsf añada los tipos formales `RE::TESWaitStartEvent` y `RE::TESWaitStopEvent`.



### 9.7. Conclusión



El núcleo del motor mantiene cero pérdida de tiempo en todas las combinaciones probadas, incluidos los casos D<H, cambio de marcha, interrupciones, y calibración en planetas con ratio < 1, = 1 y > 1. La superficie de configuración del INI sigue siendo la misma que en v4.0.0.