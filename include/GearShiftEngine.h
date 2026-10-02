// ============================================================================
// Archivo: GearShiftEngine.h
// Función: API Pública del subsistema de cálculo de tiempo y manipulación
// atómica del entorno virtual global (GameSetting).
// ============================================================================
#pragma once
#include "PCH.h"

namespace NTA::GearShift
{
    // Enumera las ramificaciones lógicas activas en el bucle principal.
    enum class Mode { Classic, GearShift };

    void Initialize(Mode a_mode, int32_t a_nominalHours);
    bool ApplyTick(int32_t a_tickSeconds);
    int32_t ApplyEpsilon(int32_t a_baseSeconds);
    
    void SetUseCachedGlobal(bool a_value);
    void SetNominalHours(int32_t a_hours);
    int32_t GetNominalHours();

    /**
     * @brief Programa un monitor de memoria pasivo que inyectará el residuo
     * temporal exactamente cuando el motor concluya sus iteraciones numéricas
     * completas.
     *
     * Todas las magnitudes de entrada están en el eje LOCAL, salvo el ratio,
     * que convierte el avance local proyectado al eje UT que reporta el
     * calendario. El monitor sondea Calendar::GetDaysPassedExact() (UT) cada
     * 50 ms y solo está activo durante la ventana de cambio de marcha de una
     * sesión.
     *
     * INVARIANTE DEL MOTOR (verificada empíricamente en las sesiones de
     * validación de v4.0.0 y v4.1.0):
     *
     *   El bucle de descanso de Bethesda lee iSecondsToSleepPerUpdate UNA
     *   SOLA VEZ al inicio de cada iteración interna, y no lo vuelve a leer
     *   hasta que esa iteración finaliza. El umbral a (N - 0.5) explota
     *   precisamente esta invariante: adelanta el downshift a la mitad del
     *   ÚLTIMO tick nominal, garantizando que el motor, cuando termine ese
     *   tick, vuelva a leer el GMST ya modificado y consuma el residuo.
     *
     *   Si una versión futura del juego releyera el GMST mid-tick (por
     *   ejemplo, en cada subdivisión interna del bucle), el downshift se
     *   aplicaría con el motor aún procesando el tick actual y provocaría
     *   doble conteo: parte del tick nominal se cobraría al GMST antiguo y
     *   parte al nuevo. Este escenario NO se ha observado en 1.16.244.0.
     *
     * @param a_timeStart         Día absoluto del inicio de la ventana de
     *                            cambio de marcha (misma escala que
     *                            Calendar::GetDaysPassedExact()).
     * @param a_totalHoursLocal   Duración local restante a repartir tras el
     *                            tick inicial ya inyectado por el sink.
     * @param a_nominalHoursLocal Tamaño del tick nominal, en horas locales.
     * @param a_ratio             Factor UT-horas / local-horas vigente en el
     *                            planeta actual, medido por la calibración
     *                            dinámica del sink.
     */
    void ScheduleGearChange(float a_timeStart,
                            int32_t a_totalHoursLocal,
                            int32_t a_nominalHoursLocal,
                            float a_ratio);
    
    /**
     * @brief Aniquila de forma segura cualquier subproceso de modificación latente
     * en caso de que el usuario cancele el diálogo de espera gráficamente.
     */
    void CancelGearChange();
}