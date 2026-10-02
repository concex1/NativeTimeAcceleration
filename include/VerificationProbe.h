// ============================================================================
// Archivo: VerificationProbe.h
// Función: Contadores empíricos de iteración para validar las matemáticas
// del motor contra el progreso temporal real provocado por el Native Time
// Acceleration.
//
// El probe opera en dos ejes simultáneos: UT (el que reporta el calendario del
// motor) y local (el que percibe el jugador y solicita desde la UI). El ratio
// de conversión se recibe desde SleepWaitEventSink tras la calibración inicial
// de cada sesión, y se usa para emitir reportes legibles en ambos ejes.
//
// A partir de v4.1.6 el eje UT se mantiene como float sin redondear: el motor
// reporta duraciones UT con parte fraccionaria en planetas con ratio < 1 (por
// ejemplo, 0.5391h UT para 1h local), y el redondeo prematuro a int32_t en la
// firma de OnSleepStart destruía esa información en el reporte [Probe][START].
// El campo se conserva como float hasta el reporte final.
// ============================================================================
#pragma once
#include <cstdint>

namespace NTA::VerificationProbe
{
    void Initialize(bool a_enabled);

    // Se invoca desde los handlers *StartEvent (Sleep y Wait) con el tiempo
    // UT reportado por el calendario en el instante de arranque y con la
    // duración solicitada tal como la reporta el evento (en horas UT). El
    // valor se recibe como float sin redondear: los planetas con ratio < 1
    // pueden solicitar fracciones de hora UT para una hora local completa,
    // y el redondeo prematuro destruía esa información en el reporte.
    // requestedLocal y ratio se dejan en su valor neutro hasta que la
    // calibración se completa.
    void OnSleepStart(float a_daysNow, float a_durationHoursUT);

    // Se invoca UNA SOLA VEZ por sesión, inmediatamente después de que
    // SleepWaitEventSink::CompleteCalibration haya medido el ratio planetario
    // y calculado las horas locales reales solicitadas. Sustituye las
    // métricas neutras de OnSleepStart por los valores derivados, de modo
    // que el [Probe][STOP] posterior reporte cifras comparables en el eje
    // local (que es el que el usuario reconoce).
    void UpdateLocalDemands(int32_t a_localHours, float a_ratio);

    void OnSleepStop(float a_daysNow, bool a_interrupted);
    void OnHourPassed();
    void OnHoursPassed();
    void OnDaysPassed(std::int32_t a_days);
    bool RegisterAuxiliarySinks();
}