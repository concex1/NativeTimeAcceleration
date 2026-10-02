// ============================================================================
// Archivo: SleepWaitEventSink.cpp
// Función: Instanciación de los procesos delegados por la interfaz de eventos.
// Implementa la Calibración Dinámica del ratio local/UT mediante un tick de
// sacrificio inicial y una caché con TTL para amortizar el ruido de float32
// en planetas con ratio cercano a 1:1.
//
// RESUMEN DEL MODELO:
//
//   Los eventos TESSleep/TESWaitStartEvent exponen timeStart/timeEnd en la
//   misma escala que Calendar::GetDaysPassedExact() (días UT del motor).
//   El GMST iSecondsToSleepPerUpdate se interpreta, sin embargo, en segundos
//   LOCALES. El factor de conversión ratio = UT_horas / local_horas es una
//   propiedad del planeta actual y no está expuesto por la API pública.
//
//   En lugar de extraer el ratio del binario, se MIDE empíricamente:
//     1. Se inyecta un tick de sacrificio de tamaño conocido.
//     2. Se espera al primer HourPassed.
//     3. Se mide el delta real del calendario en UT y se divide por las
//        horas locales efectivamente consumidas (descontando el epsilon).
//     4. Se cachea el ratio durante kCacheTTL segundos reales.
//
//   La invalidación por cambio de celda (TESCellFullyLoadedEvent) se
//   reincorporó en v4.1.6 tras localizar el singleton real con Ghidra
//   (ID 838273). El ID 849714 que commonlibsf tenía etiquetado apuntaba al
//   TypeDescriptor del tipo, no a la instancia, y RegisterSink colgaba la
//   carga de partida. Ver EventSources.h.
//
//   En v4.1.6 se retiró la instrumentación [Trace] introducida en v4.1.1
//   para diagnosticar Bug B. El diagnóstico final reveló que Bug B era un
//   síntoma secundario de Bug A (redondeo prematuro de m_totalDurationHoursUT),
//   ya corregido, por lo que las trazas habían perdido su valor diagnóstico
//   y solo engordaban el log.
//
//   En v4.1.6 (revisión final) se sustituyó el patrón load()+store(false)
//   del handler de TESCellFullyLoadedEvent por un único exchange(false)
//   atómico. El motor despacha TESCellFullyLoadedEvent varias veces por
//   transición (worldspace exterior + celda interior + adyacentes), y con
//   hilos concurrentes el patrón load+store podía emitir múltiples líneas
//   de log idénticas para una sola transición lógica. exchange(false) es
//   atómico: solo un hilo observa la transición true→false y por tanto
//   solo uno emite el log. El efecto funcional (invalidar la caché) es
//   idéntico; el cambio es puramente cosmético sobre el log.
//
//   En v4.1.6 (revisión final, ampliada) se eliminó el lround prematuro en
//   los call sites de VerificationProbe::OnSleepStart. El campo
//   m_totalDurationHoursUT se pasa directamente como float, preservando su
//   parte fraccionaria en el reporte [Probe][START]. Antes, un request de
//   7h locales en un planeta con ratio 0.54 aparecía como requestedUT=4h
//   (lround de 3.7910), aunque el [Probe][STOP] posterior corregía la cifra
//   vía UpdateLocalDemands. El cambio es puramente cosmético sobre el log.
// ============================================================================
#include "SleepWaitEventSink.h"
#include "EventSources.h"
#include "GearShiftEngine.h"
#include "VerificationProbe.h"

namespace NTA
{
    namespace
    {
        // --- Caché de ratio UT/local ---------------------------------------
        std::atomic<float> g_cachedRatio{ 1.0f };
        std::atomic<bool>  g_cachedRatioValid{ false };

        std::chrono::steady_clock::time_point g_lastSessionEndTime{};

        constexpr std::chrono::seconds kCacheTTL{ 60 };

        constexpr float kRatioAnomalyThreshold{ 0.001f };
        constexpr float kRatioLowThreshold{ 0.5f };
        constexpr float kPlanetChangeDeviation{ 0.30f };
        constexpr float kSacrificeSelectionMargin{ 0.01f };

        bool IsCacheUsable()
        {
            if (!g_cachedRatioValid.load()) return false;
            const auto now = std::chrono::steady_clock::now();
            if (now - g_lastSessionEndTime > kCacheTTL) return false;
            const float r = g_cachedRatio.load();
            return std::isfinite(r) && r > 0.0f;
        }
    }

    int32_t SleepWaitEventSink::SelectSacrificeHours(float a_requestUTHours) const
    {
        if (!IsCacheUsable()) {
            return 1;
        }
        const float cachedRatio = g_cachedRatio.load();
        if (!std::isfinite(cachedRatio) || cachedRatio <= 0.0f) {
            return 1;
        }

        const float requestLocalEst = a_requestUTHours / cachedRatio;
        const float nominalHours    = static_cast<float>(GearShift::GetNominalHours());

        if (requestLocalEst >= nominalHours - kSacrificeSelectionMargin) {
            return GearShift::GetNominalHours();
        }
        return 1;
    }

    void SleepWaitEventSink::CompleteCalibration(float a_nowDaysUT)
    {
        m_isCalibrating = false;

        const float deltaUTHours = (a_nowDaysUT - m_calibrationStartDays) * 24.0f;
        const int32_t appliedSeconds = GearShift::ApplyEpsilon(m_sacrificeLocalHours * 3600);
        const float appliedLocalHours = static_cast<float>(appliedSeconds) / 3600.0f;

        if (appliedLocalHours <= 0.0f) {
            logger::error("[Calibration] appliedLocalHours <= 0. Abortando calibración.");
            return;
        }

        float ratio = deltaUTHours / appliedLocalHours;

        if (!std::isfinite(ratio) || ratio <= kRatioAnomalyThreshold) {
            logger::warn("[Calibration] Ratio anómalo ({:.6f}). Fallback a 1.0.", ratio);
            ratio = 1.0f;
        } else if (ratio < kRatioLowThreshold) {
            logger::warn("[Calibration] Ratio sub-1 inesperado ({:.6f}). "
                         "Posible degradación de la calibración o comportamiento "
                         "anómalo del motor.", ratio);
        }

        if (g_cachedRatioValid.load()) {
            const float previousRatio = g_cachedRatio.load();
            if (std::isfinite(previousRatio) && previousRatio > 0.0f) {
                const float deviation = std::abs(ratio - previousRatio) / previousRatio;
                if (deviation > kPlanetChangeDeviation) {
                    logger::info("[Calibration] Cambio de planeta detectado. "
                                 "Ratio anterior {:.4f}, nuevo {:.4f} (desviación {:.1f}%).",
                                 previousRatio, ratio, deviation * 100.0f);
                }
            }
        }

        g_cachedRatio.store(ratio);
        g_cachedRatioValid.store(true);

        int32_t totalHoursLocal = static_cast<int32_t>(std::lround(m_totalDurationHoursUT / ratio));
        if (totalHoursLocal < 1) totalHoursLocal = 1;

        logger::info("[Calibration] Ratio deducido: {:.4f} (UT/Local). "
                     "UT solicitado: {:.4f}h. Total Local solicitado: {}h. Sacrificio: {}h.",
                     ratio, m_totalDurationHoursUT, totalHoursLocal, m_sacrificeLocalHours);

        VerificationProbe::UpdateLocalDemands(totalHoursLocal, ratio);

        const int32_t remainingLocal = totalHoursLocal - m_sacrificeLocalHours;
        const int32_t nominalHours   = GearShift::GetNominalHours();

        if (remainingLocal < 0) {
            logger::error("[Calibration] OVER-ADVANCE: sacrificio {}h > request local {}h. "
                          "Caché de ratio posiblemente obsoleta. La sesión sobre-avanzará.",
                          m_sacrificeLocalHours, totalHoursLocal);
            return;
        }

        if (remainingLocal == 0) {
            logger::info("[Calibration] Sesión completada íntegramente por el tick de sacrificio.");
            return;
        }

        if (remainingLocal < nominalHours) {
            logger::info("[Sink] Residuo menor al nominal detectado ({}h). Inyectando directamente.",
                         remainingLocal);
            GearShift::ApplyTick(remainingLocal * 3600);
            return;
        }

        GearShift::ApplyTick(nominalHours * 3600);
        const int32_t remainder = remainingLocal % nominalHours;
        if (remainder > 0) {
            GearShift::ScheduleGearChange(a_nowDaysUT, remainingLocal, nominalHours, ratio);
        }
    }

    RE::BSEventNotifyControl SleepWaitEventSink::ProcessEvent(const RE::TESSleepStartEvent& a_event, RE::BSTEventSource<RE::TESSleepStartEvent>*)
    {
        const float durationDays = a_event.timeEnd - a_event.timeStart;
        m_totalDurationHoursUT = durationDays * 24.0f;

        lastStartDays = a_event.timeStart;
        lastEndDays   = a_event.timeEnd;
        sessionActive = true;

        if (m_totalDurationHoursUT > 0.0f) {
            m_isCalibrating = true;
            m_calibrationStartDays = a_event.timeStart;
            m_sacrificeLocalHours = SelectSacrificeHours(m_totalDurationHoursUT);

            const bool cacheUsed = IsCacheUsable();
            logger::info("[Calibration] [SleepStart] UT Solicitado: {:.4f}h. "
                         "Tick de Sacrificio: {}h local. (caché={}, ratioCached={:.4f})",
                         m_totalDurationHoursUT, m_sacrificeLocalHours,
                         cacheUsed ? "sí" : "no",
                         g_cachedRatio.load());
            GearShift::ApplyTick(m_sacrificeLocalHours * 3600);
        } else {
            m_isCalibrating = false;
        }

        auto* cal = RE::Calendar::GetSingleton();
        VerificationProbe::OnSleepStart(cal ? cal->GetDaysPassedExact() : 0.0f,
                                        m_totalDurationHoursUT);
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl SleepWaitEventSink::ProcessEvent(const RE::TESSleepStopEvent& a_event, RE::BSTEventSource<RE::TESSleepStopEvent>*)
    {
        GearShift::CancelGearChange();
        sessionActive = false;
        m_isCalibrating = false;
        g_lastSessionEndTime = std::chrono::steady_clock::now();

        auto* cal = RE::Calendar::GetSingleton();
        const float endDaysUT = cal ? cal->GetDaysPassedExact() : 0.0f;

        VerificationProbe::OnSleepStop(endDaysUT, a_event.interrupted);
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl SleepWaitEventSink::ProcessEvent(const RE::TESWaitStartEvent& a_event, RE::BSTEventSource<RE::TESWaitStartEvent>*)
    {
        const float durationDays = a_event.timeEnd - a_event.timeStart;
        m_totalDurationHoursUT = durationDays * 24.0f;

        lastStartDays = a_event.timeStart;
        lastEndDays   = a_event.timeEnd;
        sessionActive = true;

        if (m_totalDurationHoursUT > 0.0f) {
            m_isCalibrating = true;
            m_calibrationStartDays = a_event.timeStart;
            m_sacrificeLocalHours = SelectSacrificeHours(m_totalDurationHoursUT);

            const bool cacheUsed = IsCacheUsable();
            logger::info("[Calibration] [WaitStart] UT Solicitado: {:.4f}h. "
                         "Tick de Sacrificio: {}h local. (caché={}, ratioCached={:.4f})",
                         m_totalDurationHoursUT, m_sacrificeLocalHours,
                         cacheUsed ? "sí" : "no",
                         g_cachedRatio.load());
            GearShift::ApplyTick(m_sacrificeLocalHours * 3600);
        } else {
            m_isCalibrating = false;
        }

        auto* cal = RE::Calendar::GetSingleton();
        VerificationProbe::OnSleepStart(cal ? cal->GetDaysPassedExact() : 0.0f,
                                        m_totalDurationHoursUT);
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl SleepWaitEventSink::ProcessEvent(const RE::TESWaitStopEvent& a_event, RE::BSTEventSource<RE::TESWaitStopEvent>*)
    {
        GearShift::CancelGearChange();
        sessionActive = false;
        m_isCalibrating = false;
        g_lastSessionEndTime = std::chrono::steady_clock::now();

        auto* cal = RE::Calendar::GetSingleton();
        const float endDaysUT = cal ? cal->GetDaysPassedExact() : 0.0f;

        VerificationProbe::OnSleepStop(endDaysUT, a_event.interrupted);
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl SleepWaitEventSink::ProcessEvent(const RE::HourPassed::Event&, RE::BSTEventSource<RE::HourPassed::Event>*)
    {
        auto* cal = RE::Calendar::GetSingleton();
        const float nowDaysUT = cal ? cal->GetDaysPassedExact() : m_calibrationStartDays;

        if (m_isCalibrating && sessionActive) {
            CompleteCalibration(nowDaysUT);
        }

        VerificationProbe::OnHourPassed();
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl SleepWaitEventSink::ProcessEvent(const RE::HoursPassed::Event&, RE::BSTEventSource<RE::HoursPassed::Event>*)
    {
        VerificationProbe::OnHoursPassed();
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl SleepWaitEventSink::ProcessEvent(const RE::DaysPassed::Event& a_event, RE::BSTEventSource<RE::DaysPassed::Event>*)
    {
        VerificationProbe::OnDaysPassed(a_event.days);
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl SleepWaitEventSink::ProcessEvent(const RE::TESCellFullyLoadedEvent&, RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*)
    {
        // Invalidación ciega de la caché de ratio ante cualquier cambio de
        // celda. Cubre cambio de planeta, viaje intra-planeta, entrada/salida
        // de interiores y carga de partida. El coste (pérdida del beneficio
        // SNR del sacrificio variable hasta la próxima calibración) es
        // despreciable frente al riesgo de sobre-avance por caché obsoleta:
        // si la caché proviene de un planeta con ratio mucho menor, el
        // sacrificio elegido puede exceder el request UT y hacer que el
        // motor salga del bucle sin consumir ningún tick.
        //
        // exchange(false) es una única operación atómica: solo un hilo puede
        // observar la transición true→false, y por tanto solo uno emite el
        // log. El motor despacha TESCellFullyLoadedEvent varias veces por
        // transición lógica (worldspace exterior + celda interior + celdas
        // adyacentes) y desde hilos concurrentes; con el patrón previo
        // load()+store(false), varios hilos podían ver true antes de que
        // ninguno llegase al store, produciendo líneas de log idénticas
        // para una sola transición. El efecto funcional es idéntico; el
        // cambio es puramente cosmético sobre el log.
        if (g_cachedRatioValid.exchange(false)) {
            logger::info("[Cache] Cambio de celda detectado. Invalidando caché de ratio "
                         "(ratio previo {:.4f}).", g_cachedRatio.load());
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    bool RegisterSleepWaitSink(SleepWaitEventSink* a_sink)
    {
        bool anyOk = false;
        if (auto* src = EventSources::TessSleepStart()) { src->RegisterSink(a_sink); anyOk = true; }
        if (auto* src = EventSources::TessSleepStop())  { src->RegisterSink(a_sink); anyOk = true; }
        if (auto* src = EventSources::TessWaitStart())  { src->RegisterSink(a_sink); anyOk = true; }
        if (auto* src = EventSources::TessWaitStop())   { src->RegisterSink(a_sink); anyOk = true; }
        if (auto* src = EventSources::HourPassed())     { src->RegisterSink(a_sink); anyOk = true; }
        if (auto* src = EventSources::HoursPassed())    { src->RegisterSink(a_sink); anyOk = true; }
        if (auto* src = EventSources::DaysPassed())     { src->RegisterSink(a_sink); anyOk = true; }

        // Octavo sink: invalidación de caché por cambio de celda (v4.1.6).
        if (auto* src = EventSources::CellFullyLoaded()) {
            src->RegisterSink(a_sink);
            anyOk = true;
        }

        return anyOk;
    }

    bool UnregisterSleepWaitSink(SleepWaitEventSink* a_sink)
    {
        if (auto* src = EventSources::TessSleepStart()) src->UnregisterSink(a_sink);
        if (auto* src = EventSources::TessSleepStop())  src->UnregisterSink(a_sink);
        if (auto* src = EventSources::TessWaitStart())  src->UnregisterSink(a_sink);
        if (auto* src = EventSources::TessWaitStop())   src->UnregisterSink(a_sink);
        if (auto* src = EventSources::HourPassed())     src->UnregisterSink(a_sink);
        if (auto* src = EventSources::HoursPassed())    src->UnregisterSink(a_sink);
        if (auto* src = EventSources::DaysPassed())     src->UnregisterSink(a_sink);
        if (auto* src = EventSources::CellFullyLoaded()) src->UnregisterSink(a_sink);
        return true;
    }
}