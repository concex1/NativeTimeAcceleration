// ============================================================================
// Archivo: WaitEventsShim.h
// Función: Estructura emulada (Shim) para compensar la ausencia de tipos formales
// en la rama actual del repositorio commonlibsf. 
// ============================================================================
#pragma once
#include <cstdint>

namespace RE
{
    // Las estructuras de espera mantienen una paridad estricta en el alineamiento
    // de memoria con los eventos de sueño nativos.

    struct TESWaitStartEvent
    {
        float timeStart;   // Offset relativo 0x00: Días transcurridos absolutos de inicio
        float timeEnd;     // Offset relativo 0x04: Proyección temporal del fin
    };

    struct TESWaitStopEvent
    {
        bool interrupted;  // Offset relativo 0x00: Bandera de cancelación asíncrona del usuario
    };
}