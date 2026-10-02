// ============================================================================
// Archivo: GearShiftEngine.cpp
// Función: Corazón matemático del mod. Gestiona la alteración microscópica
// (Epsilon) y el lanzamiento de subprocesos ligeros para cambio de marcha.
//
// La detección de sesión de descanso corre exclusivamente a cargo de los
// siete sinks BSTEventSource<> registrados desde SleepWaitEventSink. No
// existe ruta alternativa (polling u otra): si los eventos no están
// disponibles, GearShift no opera.
// ============================================================================
#include "GearShiftEngine.h"

namespace NTA::GearShift
{
    namespace
    {
        constexpr int32_t kVanillaSeconds = 3600;

        bool    g_useCachedGlobal = false;
        int32_t g_nominalHours    = 3;

        // Semáforos y rastreadores de ID monótonos para evitar condiciones de carrera (Race Conditions)
        std::atomic<uint64_t> g_nextGearSessionId{ 1 };
        std::atomic<uint64_t> g_activeGearSessionId{ 0 };

        constexpr auto kGearMonitorPoll = std::chrono::milliseconds(50);

        [[nodiscard]] int32_t* CachedGlobalPtr() noexcept
        {
            static REL::Relocation<int32_t*> ptr{ REL::ID(881128) };
            return ptr.get();
        }
    }

    void SetUseCachedGlobal(bool v) { g_useCachedGlobal = v; }
    void SetNominalHours(int32_t v) { g_nominalHours = v; }
    int32_t GetNominalHours()       { return g_nominalHours; }

    /**
     * @brief Aplica el ajuste Micro-Epsilon matemático (-1s) para que la
     * comparación acumulativa dentro del motor C++ no exceda el tiempo objetivo.
     */
    int32_t ApplyEpsilon(int32_t a_baseSeconds)
    {
        return (a_baseSeconds > 1) ? (a_baseSeconds - 1) : a_baseSeconds;
    }

    /**
     * @brief Interfaz de mutación directa. Reconfigura el GMST con seguridad
     * de acceso en tiempo de ejecución.
     */
    bool ApplyTick(int32_t a_tickSeconds)
    {
        const int32_t finalTick = ApplyEpsilon(a_tickSeconds);

        bool gmstOk = false;
        if (auto* gmst = RE::GameSettingCollection::GetSingleton()) {
            gmstOk = gmst->SetSetting("iSecondsToSleepPerUpdate", finalTick);
            if (!gmstOk) {
                logger::error("[GearShift] El despachador GameSettingCollection rechazó la escritura del GMST.");
            }
        }

        bool cacheOk = false;
        if (g_useCachedGlobal) {
            if (auto* c = CachedGlobalPtr()) {
                *c = finalTick;
                cacheOk = true;
            }
        }

        logger::info("[GearShift] Aplicación de Tick confirmada: base={}s mitigado={}s GMST_Status={}",
            a_tickSeconds, finalTick, gmstOk ? "Exito" : "Fallo");

        return gmstOk;
    }

    void Initialize(Mode a_mode, int32_t a_nominalHours)
    {
        SetNominalHours(a_nominalHours);
        if (a_mode == Mode::Classic) {
            const int32_t rawTick = a_nominalHours * kVanillaSeconds;
            ApplyTick(rawTick);
            logger::info("[GearShift] Arranque Completo. Operando en Modalidad Clásica Pura.");
        } else {
            logger::info("[GearShift] Arranque Completo. Motor Asíncrono Listo. Salto Nominal = {}h", a_nominalHours);
        }
    }

    /**
     * @brief El algoritmo central de recuperación de residuos. Si una solicitud
     * no es divisible de forma entera, este hilo espera pasivamente hasta que
     * se agoten los ticks completos para cambiar el tamaño del paso.
     *
     * INVARIANTE DEL MOTOR: el umbral a (N - 0.5) asume que el bucle interno
     * de descanso lee iSecondsToSleepPerUpdate UNA SOLA VEZ al inicio de cada
     * iteración, y no lo vuelve a leer hasta terminarla. Bajo esa hipótesis,
     * un downshift inyectado a mitad del último tick nominal se recogerá
     * limpiamente en la siguiente iteración, sin doble conteo. La invariante
     * está verificada empíricamente en Starfield 1.16.244.0.
     *
     * El ratio recibido convierte el delta local proyectado al eje UT del
     * calendario. Los valores inyectados al GMST se mantienen siempre en
     * segundos locales (es lo que el motor espera), pero el umbral del
     * monitor se calcula en UT-días para comparar directamente con
     * Calendar::GetDaysPassedExact().
     */
    void ScheduleGearChange(float a_timeStart,
                            int32_t a_totalHoursLocal,
                            int32_t a_nominalHoursLocal,
                            float a_ratio)
    {
        if (a_totalHoursLocal <= 0 || a_nominalHoursLocal <= 0) return;
        if (!std::isfinite(a_ratio) || a_ratio <= 0.0f) return;

        const int32_t N = a_totalHoursLocal / a_nominalHoursLocal; // División entera (Truncamiento local)
        const int32_t R = a_totalHoursLocal % a_nominalHoursLocal; // Cálculo del residuo horario local

        if (N <= 0 || R <= 0) return; // Nada que compensar

        // Reclamar la sesión activa desvinculando cualquier hilo fantasma anterior
        const uint64_t sessionId = g_nextGearSessionId.fetch_add(1);
        g_activeGearSessionId.store(sessionId);

        // Umbral de activación: punto medio del ÚLTIMO salto nominal proyectado
        // a Tiempo Universal. Al restar 0.5f a N, el monitor interviene mientras
        // el motor está ocupado procesando el último salto largo, de modo que
        // cuando ese salto termine, el GMST ya estará reconfigurado y el bucle
        // siguiente consumirá el residuo sin abortar por falta de tiempo.
        const float thresholdDays = a_timeStart +
                                    (((N - 0.5f) * (a_nominalHoursLocal * 3600.0f) * a_ratio) / 86400.0f);

        // El GameSetting requiere el valor residual en formato LOCAL, no UT.
        const int32_t remainderSeconds = R * kVanillaSeconds;

        logger::info("[GearShift] Trazando Recuperación: Tras {} saltos ({}h locales / ratio {:.2f}), "
                     "inyectaremos un residuo exacto de {}s.",
                     N, a_nominalHoursLocal, a_ratio, remainderSeconds);

        std::thread([sessionId, thresholdDays, remainderSeconds]() {
            auto* cal = RE::Calendar::GetSingleton();
            if (!cal) return;

            // Bucle ligero de bajo consumo (Sondeo cada 50 milisegundos)
            while (g_activeGearSessionId.load(std::memory_order_relaxed) == sessionId) {
                const float now = cal->GetDaysPassedExact(); // Devuelve Tiempo Universal (UT)
                if (now >= thresholdDays) {
                    uint64_t expected = sessionId;
                    // Intercambio atómico (CAS): Solo un hilo es autorizado a mutar el GMST
                    if (g_activeGearSessionId.compare_exchange_strong(expected, 0, std::memory_order_relaxed)) {
                        logger::info("[GearShift] Umbral Temporal Intersecado en UT. Ejecutando Downshift Asíncrono.");
                        ApplyTick(remainderSeconds);
                    }
                    return; // Retorno de éxito, destrucción del hilo
                }
                std::this_thread::sleep_for(kGearMonitorPoll);
            }
        }).detach();
    }

    void CancelGearChange()
    {
        const uint64_t previous = g_activeGearSessionId.exchange(0);
        if (previous != 0) {
            logger::info("[GearShift] Operación purgada dinámicamente debido a interrupción explícita del usuario.");
        }
    }
}