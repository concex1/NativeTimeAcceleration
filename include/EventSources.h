// ============================================================================
// Archivo: EventSources.h
// Función: Mapeo de punteros a tablas virtuales (Vtables) para la obtención
// estática de los objetos despachadores de eventos de Bethesda.
// ============================================================================
#pragma once
#include "PCH.h"
#include "WaitEventsShim.h"
#include <RE/B/BSTEvent.h>
#include <RE/E/Events.h>

namespace NTA::EventSources
{
    // Los identificadores numéricos referencian el mapeo de la segunda columna
    // del Address Library, garantizando la resolución a RVAs precisas del bloque de datos.

    inline RE::BSTEventSource<RE::TESSleepStartEvent>* TessSleepStart()
    {
        static REL::Relocation<RE::BSTEventSource<RE::TESSleepStartEvent>> singleton{ REL::ID(838515) };
        return reinterpret_cast<RE::BSTEventSource<RE::TESSleepStartEvent>*>(singleton.address());
    }

    inline RE::BSTEventSource<RE::TESSleepStopEvent>* TessSleepStop()
    {
        static REL::Relocation<RE::BSTEventSource<RE::TESSleepStopEvent>> singleton{ REL::ID(838519) };
        return reinterpret_cast<RE::BSTEventSource<RE::TESSleepStopEvent>*>(singleton.address());
    }

    inline RE::BSTEventSource<RE::TESWaitStartEvent>* TessWaitStart()
    {
        static REL::Relocation<RE::BSTEventSource<RE::TESWaitStartEvent>> singleton{ REL::ID(838565) };
        return reinterpret_cast<RE::BSTEventSource<RE::TESWaitStartEvent>*>(singleton.address());
    }

    inline RE::BSTEventSource<RE::TESWaitStopEvent>* TessWaitStop()
    {
        static REL::Relocation<RE::BSTEventSource<RE::TESWaitStopEvent>> singleton{ REL::ID(838569) };
        return reinterpret_cast<RE::BSTEventSource<RE::TESWaitStopEvent>*>(singleton.address());
    }

    // Auxiliares de medición de rendimiento y diagnóstico de calendario

    inline RE::BSTEventSource<RE::HourPassed::Event>* HourPassed()
    {
        static REL::Relocation<RE::BSTEventSource<RE::HourPassed::Event>> singleton{ REL::ID(839024) };
        return reinterpret_cast<RE::BSTEventSource<RE::HourPassed::Event>*>(singleton.address());
    }

    inline RE::BSTEventSource<RE::HoursPassed::Event>* HoursPassed()
    {
        static REL::Relocation<RE::BSTEventSource<RE::HoursPassed::Event>> singleton{ REL::ID(839229) };
        return reinterpret_cast<RE::BSTEventSource<RE::HoursPassed::Event>*>(singleton.address());
    }

    inline RE::BSTEventSource<RE::DaysPassed::Event>* DaysPassed()
    {
        static REL::Relocation<RE::BSTEventSource<RE::DaysPassed::Event>> singleton{ REL::ID(839020) };
        return reinterpret_cast<RE::BSTEventSource<RE::DaysPassed::Event>*>(singleton.address());
    }

    // Octavo evento: cambio de celda completada. Se usa para invalidar la caché
    // de ratio ante cualquier carga de celda (cambio de planeta, viaje intra-
    // planeta, entrada/salida de interiores, carga de partida).
    //
    // RESOLUCIÓN DEL ID (v4.1.6):
    //   El catálogo RTTI de commonlibsf (2026-09) asocia el ID 849714 a
    //   "BSTEventSource_TESCellFullyLoadedEvent_", pero ese ID apunta al
    //   TypeDescriptor del tipo (RVA 0x5A71570), no a la instancia singleton.
    //   RegisterSink sobre ese puntero cuelga el hilo de carga de partida
    //   porque trata los bytes del _TypeDescriptor como si fueran la
    //   estructura interna del BSTEventSource (vtable + [count, cap] + buffer).
    //
    //   La instancia real se localizó con Ghidra mediante el script
    //   FindBSTEventSourceSingleton.py, que caminó:
    //     TypeDescriptor ".?AV?$BSTEventSource@UTESCellFullyLoadedEvent@@@@"
    //       → CompleteObjectLocator 0x145038E18
    //       → Vtable 0x144B98770
    //       → Singleton 0x145977A90  (RVA 0x5977A90)
    //
    //   La RVA 0x5977A90 cruzada con el CSV 1.16.244 devuelve el ID
    //   838273, que es el que se usa aquí.
    inline RE::BSTEventSource<RE::TESCellFullyLoadedEvent>* CellFullyLoaded()
    {
        static REL::Relocation<RE::BSTEventSource<RE::TESCellFullyLoadedEvent>> singleton{ REL::ID(838273) };
        return reinterpret_cast<RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*>(singleton.address());
    }
}