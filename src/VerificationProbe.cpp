// ============================================================================
// Archivo: VerificationProbe.cpp
// Función: Instrumentación empírica en caliente. Suprime la inundación de logs
// filtrando estrictamente la lectura a periodos de sesión confirmada.
//
// Reporta el ratio planetario medido por la calibración del sink y compara el
// avance real del calendario contra el solicitado en el eje LOCAL, aplicando
// una tolerancia proporcional al tamaño del request para absorber el ruido
// acumulado de float32 sin enmascarar desviaciones reales.
//
// A partir de v4.1.6 el eje UT se conserva como float sin redondear en todo
// el ciclo del probe. El motor reporta duraciones UT con parte fraccionaria
// en planetas con ratio < 1, y el lround prematuro en OnSleepStart destruía
// esa información en el reporte [Probe][START]. El redondeo a int32_t se
// aplica únicamente donde semánticamente corresponde (cuando el valor va a
// usarse como contador de horas locales enteras).
// ============================================================================
#include "VerificationProbe.h"
#include "EventSources.h"
#include <algorithm>
#include <cmath>

namespace NTA::VerificationProbe
{
    namespace
    {
        bool g_enabled = false;

        // Contadores de sesión para diagnóstico.
        //
        // sessionActive se activa en OnSleepStart y se desactiva en
        // OnSleepStop. Mientras está en false, los eventos HourPassed /
        // HoursPassed / DaysPassed se ignoran sin loguear (evita inundar
        // el archivo durante el juego normal, donde el reloj in-game
        // avanza a timeScale y dispara HourPassed constantemente).
        //
        // Semántica real de los tres contadores (validada en v3.2.13):
        //
        //   hourPassedCount  = número de iteraciones del bucle interno del
        //                      motor de descanso. Corresponde a los "ticks"
        //                      que el motor consume con el GMST configurado
        //                      (1 por tick completo o fraccionario).
        //
        //   hoursPassedCount = número de horas in-game transcurridas en el
        //                      eje LOCAL. Coincide aproximadamente con
        //                      deltaLocal medido entre Start y Stop.
        //
        //   daysPassedCount  = número de cruces de medianoche del calendario
        //                      local durante la sesión.
        //
        // Los tres son contadores independientes y complementarios: los dos
        // primeros permiten validar la aritmética del motor (HourPassed
        // coincide con el número de ticks calculados; HoursPassed coincide
        // con la duración local solicitada). El tercero es informativo.
        //
        // requestedHoursUT se conserva como float sin redondear (v4.1.6):
        // refleja la duración UT tal como la reporta el evento, con toda su
        // parte fraccionaria. requestedLocalHours, en cambio, es un int32_t
        // porque la calibración lo deriva con lround tras la división por
        // el ratio, y representa horas locales enteras.
        struct SessionStats
        {
            bool     sessionActive = false;
            float    startDays = 0.0f;
            float    requestedHoursUT = 0.0f;
            int32_t  requestedLocalHours = 0;
            float    ratio = 1.0f;
            int      hourPassedCount = 0;
            int      hoursPassedCount = 0;
            int      daysPassedCount = 0;
            std::chrono::steady_clock::time_point startTime;
        } g_session;
    }

    void Initialize(bool a_enabled)
    {
        g_enabled = a_enabled;
        if (a_enabled) logger::info("[Probe] Subsistema de verificación de integridad matemática ACTIVADO.");
    }

    void OnSleepStart(float a_daysNow, float a_durationHoursUT)
    {
        if (!g_enabled) return;

        g_session.sessionActive       = true;
        g_session.startDays           = a_daysNow;
        g_session.requestedHoursUT    = a_durationHoursUT;
        g_session.requestedLocalHours = 0; // Se rellena en UpdateLocalDemands
        g_session.ratio               = 1.0f;
        g_session.hourPassedCount     = 0;
        g_session.hoursPassedCount    = 0;
        g_session.daysPassedCount     = 0;
        g_session.startTime           = std::chrono::steady_clock::now();

        logger::info(
            "[Probe][START] daysNow={:.6f} requestedUT={:.4f}h",
            a_daysNow, a_durationHoursUT);
    }

    void UpdateLocalDemands(int32_t a_localHours, float a_ratio)
    {
        if (!g_enabled) return;
        g_session.requestedLocalHours = a_localHours;
        g_session.ratio = a_ratio;
    }

    void OnSleepStop(float a_daysNow, bool a_interrupted)
    {
        if (!g_enabled) return;

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - g_session.startTime).count();

        // El motor avanza el calendario en Tiempo Universal (UT).
        const float deltaRealHoursUT = (a_daysNow - g_session.startDays) * 24.0f;

        // Conversión a Tiempo Local con protección contra división por cero.
        const float safeRatio = (std::isfinite(g_session.ratio) && g_session.ratio > 0.001f)
                                ? g_session.ratio
                                : 1.0f;
        const float deltaRealLocal = deltaRealHoursUT / safeRatio;

        // Si la sesión se interrumpió antes de completar la calibración, el
        // valor local solicitado aún no está disponible; se usa UT como
        // referencia degradada para no perder la traza del evento. El valor
        // UT se toma como float (v4.1.6) para preservar su parte fraccionaria
        // en el reporte del fallback.
        const float expectedLocal = (g_session.requestedLocalHours > 0)
                                    ? static_cast<float>(g_session.requestedLocalHours)
                                    : g_session.requestedHoursUT;

        // Tolerancia proporcional al tamaño del request. El ruido de float32
        // del calendario escala con el delta medido, por lo que una tolerancia
        // fija de 0.05h penalizaría injustamente requests largos en planetas
        // de ratio grande, y sería demasiado laxa para requests cortos en 1:1.
        // Se combina un suelo absoluto de 3 minutos con un margen relativo
        // del 1% del request local.
        const float relativeMargin = 0.01f * expectedLocal;
        const float tolerance = std::max(0.05f, relativeMargin);

        logger::info(
            "[Probe][STOP] interrupted={} daysNow={:.6f} deltaUT={:.4f}h deltaLocal={:.4f}h "
            "requestedLocal={:.4f}h ratio={:.4f} tolerance={:.4f}h | "
            "counts(Hour={} Hours={} Days={}) | wall={}ms",
            a_interrupted, a_daysNow, deltaRealHoursUT, deltaRealLocal,
            expectedLocal, safeRatio, tolerance,
            g_session.hourPassedCount,
            g_session.hoursPassedCount,
            g_session.daysPassedCount,
            elapsedMs);

        if (a_interrupted) {
            logger::info("[Probe] Sesión interrumpida por el jugador. "
                         "Delta local {:.4f}h < solicitadas {:.4f}h locales (esperado).",
                deltaRealLocal, expectedLocal);
        } else if (std::abs(deltaRealLocal - expectedLocal) > tolerance) {
            logger::warn(
                "[Probe] DIVERGENCIA: solicitadas {:.4f}h locales vs reales {:.4f}h locales "
                "(delta {:.4f}h, tolerancia {:.4f}h).",
                expectedLocal, deltaRealLocal,
                deltaRealLocal - expectedLocal, tolerance);
        } else {
            logger::info("[Probe] Coincidencia geométrica: {:.4f}h locales ≈ {:.4f}h locales "
                         "(dentro de tolerancia {:.4f}h).",
                expectedLocal, deltaRealLocal, tolerance);
        }

        g_session.sessionActive = false;
    }

    // ------------------------------------------------------------------------
    // Handlers de los eventos de tiempo.
    //
    // Solo se acumulan contadores cuando g_session.sessionActive == true.
    // Fuera de sesión se ignoran silenciosamente: el juego normal emite
    // HourPassed / HoursPassed continuamente (timeScale=20 → varias veces
    // por segundo real) y loguear cada uno inunda el archivo.
    //
    // Los contadores se reportan agregados en el log de OnSleepStop.
    // ------------------------------------------------------------------------
    void OnHourPassed()
    {
        if (!g_enabled)               return;
        if (!g_session.sessionActive) return;

        ++g_session.hourPassedCount;
    }

    void OnHoursPassed()
    {
        if (!g_enabled)               return;
        if (!g_session.sessionActive) return;

        ++g_session.hoursPassedCount;
    }

    void OnDaysPassed(std::int32_t /*a_days*/)
    {
        if (!g_enabled)               return;
        if (!g_session.sessionActive) return;

        ++g_session.daysPassedCount;
    }

    bool RegisterAuxiliarySinks() { return true; }
}