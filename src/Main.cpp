// ============================================================================
// Archivo: Main.cpp
// Función: Entrada principal de ejecución dinámica en inyección SFSE.
// ============================================================================
#include "PCH.h"
#include "Config.h"
#include "GearShiftEngine.h"
#include "SleepWaitEventSink.h"
#include "VerificationProbe.h"
#include "EventSources.h"

static std::atomic<bool> g_isTurboActive(true);
static NTA::SleepWaitEventSink g_sleepSink;

// ============================================================================
// DIAGNÓSTICO: Verificación de IDs + dumps de singletons
// ============================================================================

namespace
{
    struct IdProbe
    {
        const char*    name;
        std::uint32_t  id;
        std::uintptr_t expectedRva;
    };

    void RunIdProbe()
    {
        const auto imageBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
        logger::info("[IdProbe] ImageBase runtime = 0x{:X}", imageBase);

        const IdProbe probes[] = {
            // Infraestructura de registro de sinks
            { "BSTEventSource::RegisterSink",   123821, 0x22C8E80 },
            { "BSTEventSource::UnregisterSink", 123822, 0x22C9010 },

            // Ocho singletons BSTEventSource confirmados con
            // TypeOfSingleton.py / FindSingletonsByName.py /
            // FindBSTEventSourceSingleton.py (RTTI walk sobre el binario).
            // Las RVAs son las esperadas según el CSV 1.16.244. Los IDs son
            // los de la COLUMNA 1 del CSV (el ID real).
            { "BSTEventSource<TESSleepStartEvent>", 838515, 0x59783C8 },
            { "BSTEventSource<TESSleepStopEvent>",  838519, 0x59783F0 },
            { "BSTEventSource<TESWaitStartEvent>",  838565, 0x59785A8 },
            { "BSTEventSource<TESWaitStopEvent>",   838569, 0x59785D0 },
            { "BSTEventSource<DaysPassed::Event>",  839020, 0x5979DD0 },
            { "BSTEventSource<HourPassed::Event>",  839024, 0x5979DF8 },
            { "BSTEventSource<HoursPassed::Event>", 839229, 0x597A7F0 },

            // Octavo singleton (v4.1.6): cambio de celda completada.
            // RVA localizada con FindBSTEventSourceSingleton.py:
            //   TypeDescriptor 0x145A71570
            //     → COL 0x145038E18
            //     → Vtable 0x144B98770
            //     → Singleton 0x145977A90 (RVA 0x5977A90)
            // El ID correspondiente en el CSV 1.16.244 es 838273.
            //
            // NOTA: el ID 849714 que commonlibsf etiqueta como
            // "BSTEventSource_TESCellFullyLoadedEvent_" apunta al
            // TypeDescriptor del tipo (RVA 0x5A71570), NO a la instancia
            // del singleton. RegisterSink sobre ese puntero cuelga el
            // hilo de carga de partida.
            { "BSTEventSource<TESCellFullyLoadedEvent>", 838273, 0x5977A90 },

            // ID 881128 según el rename dump, etiquetado como
            // g_iSecondsToSleepLow.
            { "probe_id_881128 (rename dump: g_iSecondsToSleepLow)",
                                                    881128, 0x5D940C0 },
        };

        for (const auto& p : probes) {
            try {
                REL::Relocation<std::uintptr_t> reloc{ REL::ID(p.id) };
                const auto addr = reloc.address();
                const auto rva  = addr - imageBase;

                if (p.expectedRva == 0) {
                    logger::info("[IdProbe] {:42s} id={:7d} rva=0x{:X}",
                        p.name, p.id, rva);
                } else {
                    logger::info(
                        "[IdProbe] {:42s} id={:7d} rva=0x{:X} expected=0x{:X} [{}]",
                        p.name, p.id, rva, p.expectedRva,
                        (rva == p.expectedRva) ? "MATCH" : "MISMATCH");
                }
            } catch (...) {
                logger::error("[IdProbe] {:42s} id={:7d} EXCEPCIÓN", p.name, p.id);
            }
        }
    }

    // ------------------------------------------------------------------------
    // CacheProbe: lee g_iSecondsToSleepLow en modo SOLO LECTURA.
    // ------------------------------------------------------------------------
    void RunCacheProbe()
    {
        logger::info("[CacheProbe] === Verificación de caché (solo lectura) ===");

        const auto imageBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));

        std::uintptr_t cacheAddr = 0;
        try {
            REL::Relocation<std::uintptr_t> reloc{ REL::ID(881128) };
            cacheAddr = reloc.address();
        } catch (...) {
            logger::error("[CacheProbe] REL::ID(881128) lanzó excepción.");
            return;
        }

        const auto cacheRva = cacheAddr - imageBase;
        logger::info("[CacheProbe] g_iSecondsToSleepLow @ 0x{:X} (rva 0x{:X})",
            cacheAddr, cacheRva);

        const auto moduleSize = 0x20000000ull;
        if (cacheRva >= moduleSize) {
            logger::error("[CacheProbe] RVA fuera de rango. Abortando lectura.");
            return;
        }

        const auto cacheValue = *reinterpret_cast<const std::int32_t*>(cacheAddr);
        logger::info("[CacheProbe] Valor actual en caché (int32): {}", cacheValue);

        std::int32_t gmstValue = -1;
        if (auto* gmst = RE::GameSettingCollection::GetSingleton()) {
            if (auto* setting = gmst->GetSetting("iSecondsToSleepPerUpdate")) {
                gmstValue = setting->GetValue<std::int32_t>(0, false);
            }
        }
        logger::info("[CacheProbe] Valor actual en GMST (int32):  {}", gmstValue);

        if (cacheValue == gmstValue) {
            logger::info("[CacheProbe] COINCIDEN → motor sincroniza caché y GMST.");
        } else {
            logger::warn("[CacheProbe] DIFIEREN → caché independiente del GMST. "
                         "UseCachedGlobal está bloqueado en false por seguridad en V4.");
        }

        const auto cacheU64 = *reinterpret_cast<const std::uint64_t*>(cacheAddr);
        const auto cacheF32 = *reinterpret_cast<const float*>(cacheAddr);
        logger::info("[CacheProbe] Mismo offset interpretado como: u64=0x{:X} f32={:.4f}",
            cacheU64, cacheF32);
    }

    // ------------------------------------------------------------------------
    // SourceDump genérico: caracteriza un BSTEventSource<T> dado su ID.
    // ------------------------------------------------------------------------
    void DumpSingleton(const char* a_label, std::uint32_t a_id)
    {
        logger::info("[{}Dump] === Caracterización de {} (id {}) ===",
            a_label, a_label, a_id);

        std::uintptr_t addr = 0;
        try {
            REL::Relocation<std::uintptr_t> reloc{ REL::ID(a_id) };
            addr = reloc.address();
        } catch (...) {
            logger::error("[{}Dump] REL::ID({}) lanzó excepción.", a_label, a_id);
            return;
        }

        const auto imageBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
        logger::info("[{}Dump] Dirección: 0x{:X} (rva 0x{:X})",
            a_label, addr, addr - imageBase);

        const auto* qwords = reinterpret_cast<const std::uint64_t*>(addr);
        for (int i = 0; i < 8; ++i) {
            logger::info("[{}Dump]   obj +{:02X}: 0x{:016X}",
                a_label, i * 8, qwords[i]);
        }

        const auto vtblAddr = qwords[0];
        const bool vtblOk = (vtblAddr >= imageBase) &&
                            (vtblAddr <  imageBase + 0x100000000ull);
        logger::info("[{}Dump] vtable: 0x{:X} ({})",
            a_label, vtblAddr, vtblOk ? "en ejecutable" : "FUERA del ejecutable");

        if (!vtblOk) {
            logger::error("[{}Dump] vtable fuera del ejecutable. "
                          "El ID {} probablemente no apunta a un BSTEventSource.",
                a_label, a_id);
            return;
        }

        logger::info("[{}Dump] === Fin del dump ===", a_label);
    }

    void RunSourceDump()    { DumpSingleton("Source",    838515); }
    void RunSleepStopDump() { DumpSingleton("SleepStop", 838519); }
    void RunWaitDump()
    {
        DumpSingleton("WaitStart", 838565);
        DumpSingleton("WaitStop",  838569);
    }

    void RunTimeDump()
    {
        DumpSingleton("HourPassed",  839024);
        DumpSingleton("HoursPassed", 839229);
        DumpSingleton("DaysPassed",  839020);
    }

    // Octavo dump: caracteriza el singleton de cambio de celda. Se ejecuta
    // junto a los anteriores y confirma visualmente que la dirección
    // resuelta es un BSTEventSource válido (vtable dentro del ejecutable,
    // [count, cap] coherentes con los otros siete).
    void RunCellFullyLoadedDump()
    {
        DumpSingleton("CellFullyLoaded", 838273);
    }
}

/**
 * @brief Bucle monitor tradicional de la V1. Operativo exclusivamente cuando
 * bEnableGearShiftEngine = 0.
 */
static void HotkeyMonitorLoop()
{
    bool wasTogglePressed = false;
    bool wasSpeedKeyPressed = false;

    while (true) {
        HWND foreground      = GetForegroundWindow();
        HWND starfieldWindow = FindWindowA("Starfield", nullptr);

        if (foreground && foreground == starfieldWindow) {
            const bool modifierSatisfied = (ModConfig::ModifierKey == 0) || (GetAsyncKeyState(ModConfig::ModifierKey) & 0x8000);
            const bool mainKeySatisfied = (GetAsyncKeyState(ModConfig::MainKey) & 0x8000) != 0;

            if (modifierSatisfied && mainKeySatisfied) {
                if (!wasTogglePressed) {
                    wasTogglePressed = true;
                    g_isTurboActive = !g_isTurboActive;

                    int32_t activeVal = g_isTurboActive ? (ModConfig::HoursPerTick * 3600) : ModConfig::vanillaSeconds;
                    NTA::GearShift::ApplyTick(activeVal);
                    logger::info("[Classic] Interrupción manual: Transición de ciclo hacia estado {}.", g_isTurboActive ? "TURBO" : "VANILLA");
                }
            } else {
                wasTogglePressed = false;
            }

            if (modifierSatisfied && !mainKeySatisfied) {
                const bool k1 = (GetAsyncKeyState(0x31) & 0x8000) || (GetAsyncKeyState(0x61) & 0x8000);
                const bool k2 = (GetAsyncKeyState(0x32) & 0x8000) || (GetAsyncKeyState(0x62) & 0x8000);
                const bool k3 = (GetAsyncKeyState(0x33) & 0x8000) || (GetAsyncKeyState(0x63) & 0x8000);
                const bool k4 = (GetAsyncKeyState(0x34) & 0x8000) || (GetAsyncKeyState(0x64) & 0x8000);

                if (k1 || k2 || k3 || k4) {
                    if (!wasSpeedKeyPressed) {
                        wasSpeedKeyPressed = true;
                        if (k1) {
                            g_isTurboActive = false;
                            NTA::GearShift::ApplyTick(ModConfig::vanillaSeconds);
                            logger::info("[Classic] Desescalada manual: Operación Vanilla forzada.");
                        } else {
                            int newHours = k2 ? 2 : k3 ? 3 : 4;
                            ModConfig::HoursPerTick = newHours;
                            NTA::GearShift::SetNominalHours(newHours);
                            g_isTurboActive = true;
                            NTA::GearShift::ApplyTick(newHours * 3600);
                            logger::info("[Classic] Modificación estática en vivo consolidada: Tick modificado a la cadencia de {}h.", newHours);
                        }
                    }
                } else {
                    wasSpeedKeyPressed = false;
                }
            } else if (!modifierSatisfied) {
                wasSpeedKeyPressed = false;
            }
        } else {
            wasTogglePressed = false;
            wasSpeedKeyPressed = false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

static void InitializeLogging()
{
    char profilePath[MAX_PATH]{};
    ExpandEnvironmentStringsA("%USERPROFILE%", profilePath, MAX_PATH);
    std::filesystem::path logPath = std::filesystem::path(profilePath) / "Documents" / "My Games" / "Starfield" / "SFSE" / "Logs" / "NativeTimeAcceleration.log";

    auto file_sink = std::make_shared<logger::sinks::basic_file_sink_mt>(logPath.string(), true);
    auto log = std::make_shared<logger::logger>("global", file_sink);
    log->set_level(logger::level::info);
    log->flush_on(logger::level::info);
    logger::set_default_logger(log);

    logger::info("=========================================================");
    logger::info(" NativeTimeAcceleration (Arquitectura Híbrida v4.1.6)");
    logger::info(" Autor: concex1");
    logger::info(" Boot Sequence Initiated");
    logger::info("=========================================================");
}

static void MessageCallback(SFSE::MessagingInterface::Message* a_msg)
{
    if (a_msg->type != SFSE::MessagingInterface::kPostDataLoad) return;

    logger::info("[SFSE] Evento kPostDataLoad recibido con éxito. Inicializando matrices de simulación...");

    // --- Probes de diagnóstico (solo lectura, no mutan estado del motor) ---
    RunIdProbe();
    RunCacheProbe();

    // Dumps de los ocho singletons activos.
    RunSourceDump();
    RunSleepStopDump();
    RunWaitDump();
    RunTimeDump();
    RunCellFullyLoadedDump();

    // --- Inicialización del subsistema de verificación ---
    NTA::VerificationProbe::Initialize(ModConfig::bEnableVerificationProbe.load());
    NTA::GearShift::SetUseCachedGlobal(ModConfig::bUseCachedGlobal.load());

    NTA::GearShift::Mode engineMode = ModConfig::bEnableGearShiftEngine.load()
                                      ? NTA::GearShift::Mode::GearShift
                                      : NTA::GearShift::Mode::Classic;

    NTA::GearShift::Initialize(engineMode, ModConfig::HoursPerTick);

    if (engineMode == NTA::GearShift::Mode::GearShift) {
        if (NTA::RegisterSleepWaitSink(&g_sleepSink)) {
            logger::info("[SFSE] Transductores RTTI adjuntados: Árbol de eventos operando en línea.");
        } else {
            logger::error("[SFSE] Colapso Crítico: Aborto de la cadena de enrutamiento RTTI (Singleton ausente).");
        }
    } else {
        std::thread(HotkeyMonitorLoop).detach();
        logger::info("[SFSE] Alternativa de Arranque: Rama Clásica iniciada y Monitor de WinAPI anclado.");
    }
}

SFSE_PLUGIN_PRELOAD(const SFSE::PreLoadInterface* a_sfse)
{
    SFSE::Init(a_sfse);
    return true;
}

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* a_sfse)
{
    InitializeLogging();

    ModConfig::LoadConfig();

    SFSE::Init(a_sfse);

    auto* messaging = SFSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(MessageCallback)) {
        logger::error("[SFSE] Error Terminal: Incapacidad para registrar el callback dentro del bus maestro SFSE.");
        return false;
    }

    logger::info("[SFSE] Contrato de escucha establecido con SFSE.");
    return true;
}