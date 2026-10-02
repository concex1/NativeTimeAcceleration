// ============================================================================
// Archivo: SleepWaitEventSink.h
// Función: Multiplexor de sumideros que hereda de múltiples interfaces virtuales
// puras para interceptar los ocho bloques de eventos del motor.
//
// El sink encapsula la Calibración Dinámica del ratio local/UT: en el primer
// HourPassed de cada sesión mide empíricamente el ratio a partir del avance
// real del calendario, y usa esa medición para descomponer la duración
// solicitada (que el motor reporta en UT) en ticks locales que el GMST pueda
// consumir sin pérdida.
//
// A partir de v4.1.1 el campo m_totalDurationHoursUT se conserva como float
// sin redondear: los planetas con ratio < 1 pueden solicitar < 1h UT por una
// hora local completa, y el redondeo prematuro destruía esa información
// (Bug A).
//
// A partir de v4.1.6 el sink intercepta TESCellFullyLoadedEvent para invalidar
// la caché de ratio en cada carga de celda. La resolución del singleton se
// hace a través de EventSources::CellFullyLoaded() (ID 838273), no del
// ID 849714 que commonlibsf tiene mal mapeado. Ver EventSources.h.
//
// En v4.1.6 se retiró la instrumentación [Trace] introducida en v4.1.1 para
// diagnosticar Bug B. El diagnóstico final reveló que Bug B era un síntoma
// secundario de Bug A (redondeo prematuro de m_totalDurationHoursUT), ya
// corregido, por lo que las trazas habían perdido su valor diagnóstico y
// solo engordaban el log. Los campos m_sessionStartReal, m_sessionStartDaysUT
// y m_hourPassedIndex se eliminaron en la misma iteración.
// ============================================================================
#pragma once
#include "WaitEventsShim.h"
#include <RE/B/BSTEvent.h>
#include <RE/E/Events.h>

namespace NTA
{
    class SleepWaitEventSink final :
        public RE::BSTEventSink<RE::TESSleepStartEvent>,
        public RE::BSTEventSink<RE::TESSleepStopEvent>,
        public RE::BSTEventSink<RE::TESWaitStartEvent>,
        public RE::BSTEventSink<RE::TESWaitStopEvent>,
        public RE::BSTEventSink<RE::HourPassed::Event>,
        public RE::BSTEventSink<RE::HoursPassed::Event>,
        public RE::BSTEventSink<RE::DaysPassed::Event>,
        public RE::BSTEventSink<RE::TESCellFullyLoadedEvent>
    {
    public:
        // Contratos de anulación virtual para el despachador BST
        RE::BSEventNotifyControl ProcessEvent(const RE::TESSleepStartEvent&, RE::BSTEventSource<RE::TESSleepStartEvent>*) override;
        RE::BSEventNotifyControl ProcessEvent(const RE::TESSleepStopEvent&, RE::BSTEventSource<RE::TESSleepStopEvent>*) override;
        RE::BSEventNotifyControl ProcessEvent(const RE::TESWaitStartEvent&, RE::BSTEventSource<RE::TESWaitStartEvent>*) override;
        RE::BSEventNotifyControl ProcessEvent(const RE::TESWaitStopEvent&, RE::BSTEventSource<RE::TESWaitStopEvent>*) override;
        
        RE::BSEventNotifyControl ProcessEvent(const RE::HourPassed::Event&, RE::BSTEventSource<RE::HourPassed::Event>*) override;
        RE::BSEventNotifyControl ProcessEvent(const RE::HoursPassed::Event&, RE::BSTEventSource<RE::HoursPassed::Event>*) override;
        RE::BSEventNotifyControl ProcessEvent(const RE::DaysPassed::Event&, RE::BSTEventSource<RE::DaysPassed::Event>*) override;

        // Intercepta cada carga de celda completada. Su única acción es
        // invalidar la caché de ratio vigente. Esto cubre: cambio de planeta,
        // viaje intra-planeta, entrada/salida de interiores y carga de
        // partida. Es deliberadamente ciego: el coste de invalidar la caché
        // (pérdida del SNR del sacrificio variable hasta la próxima
        // calibración) es despreciable frente al riesgo de sobre-avance por
        // caché obsoleta.
        RE::BSEventNotifyControl ProcessEvent(const RE::TESCellFullyLoadedEvent&, RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override;

        // Rastreadores de metadatos de sesión. Se mantienen públicos porque el
        // Probe los consulta desde su propio módulo de traducción; su semántica
        // es de solo lectura externa.
        float lastStartDays{ 0.0f };
        float lastEndDays{ 0.0f };
        bool  sessionActive{ false };

    private:
        // --- Estado de la Calibración Dinámica ---
        bool    m_isCalibrating{ false };
        float   m_calibrationStartDays{ 0.0f };
        int32_t m_sacrificeLocalHours{ 1 };

        // Duración solicitada tal como la reporta el evento, en horas UT. Se
        // conserva sin redondear: los planetas con ratio < 1 pueden solicitar
        // fracciones de hora UT para una hora local completa, y el redondeo
        // destruye la información necesaria para reconstruir el número exacto
        // de horas locales (Bug A, corregido en v4.1.1).
        float   m_totalDurationHoursUT{ 0.0f };

        // Elige el tamaño del tick de sacrificio para la sesión en curso.
        [[nodiscard]] int32_t SelectSacrificeHours(float a_requestUTHours) const;

        // Ejecuta la calibración tras el primer HourPassed de la sesión.
        void CompleteCalibration(float a_nowDaysUT);
    };

    // Registra el sink en las ocho fuentes de eventos reales.
    bool RegisterSleepWaitSink(SleepWaitEventSink* a_sink);
    bool UnregisterSleepWaitSink(SleepWaitEventSink* a_sink);
}