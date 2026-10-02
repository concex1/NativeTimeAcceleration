// ============================================================================
// Archivo: Config.h
// Función: Estructura de estado estático y lectura del archivo de configuración.
// Aplica el paradigma estricto de validación de enteros (1/0) y ejecuta
// la sanitización protectora del calendario in-game.
// ============================================================================
#pragma once
#include "PCH.h"

struct ModConfig {
    // Definiciones Atómicas garantizan coherencia de datos entre el hilo principal
    // del motor de juego y los subprocesos de monitorización asíncrona.
    //
    // NOTA DE DISEÑO: no existen opciones de ruta de operación. GearShift
    // usa la ruta de eventos de forma exclusiva y obligatoria; Classic no
    // usa eventos en absoluto. No hay fallback ni ruta alternativa. Si el
    // registro de sinks falla en GearShift, se reporta como error crítico
    // y el modo no opera; no se degrada silenciosamente a otra ruta.
    static inline std::atomic<bool> bEnableGearShiftEngine{true};
    static inline std::atomic<bool> bEnableVerificationProbe{false};
    static inline std::atomic<bool> bUseCachedGlobal{false}; 

    // Variables de inicialización algorítmica y control de interfaz
    static inline int32_t HoursPerTick{3};
    static inline int ModifierKey{16};
    static inline int MainKey{84};
    static inline int32_t vanillaSeconds{3600}; // El valor intrínseco de 1 hora en CE2

    /**
     * @brief Interpreta el archivo .ini y satura las variables locales aplicando 
     * protocolos defensivos para rechazar configuraciones perjudiciales.
     */
    static void LoadConfig() {
        char buffer[MAX_PATH];
        GetModuleFileNameA(GetModuleHandleA("NativeTimeAcceleration.dll"), buffer, MAX_PATH);
        std::string iniPath(buffer);
        size_t pos = iniPath.find_last_of('.');
        if (pos != std::string::npos) {
            iniPath = iniPath.substr(0, pos) + ".ini";
        } else {
            iniPath += ".ini";
        }

        // Extracción de datos utilizando GetPrivateProfileIntA para imponer el formato entero.
        // Se compara con 0 para resolver la conversión booleana atómica.
        bEnableGearShiftEngine = GetPrivateProfileIntA("Settings", "bEnableGearShiftEngine", 1, iniPath.c_str()) != 0;
        HoursPerTick = GetPrivateProfileIntA("Settings", "HoursPerTick", 3, iniPath.c_str());

        bEnableVerificationProbe = GetPrivateProfileIntA("Settings", "bEnableVerificationProbe", 0, iniPath.c_str()) != 0;
        
        bUseCachedGlobal = false; // Bloqueado de fábrica para eludir escritura ilícita en punteros

        ModifierKey = GetPrivateProfileIntA("Hotkeys", "ModifierKey", 16, iniPath.c_str());
        MainKey = GetPrivateProfileIntA("Hotkeys", "MainKey", 84, iniPath.c_str());

        // --- SANITIZACIÓN ALGORÍTMICA DE LA ESTABILIDAD ---
        // Violaciones a esta regla de tolerancia colapsan el comparador de punto flotante.
        if (HoursPerTick < 1 || HoursPerTick > 4) {
            logger::warn("[Config] VIOLACIÓN DE UMBRAL ESTÁTICO. El valor de HoursPerTick ({}) excede el dominio de tolerancia [1-4]. Restableciendo forzosamente a 3.", HoursPerTick);
            HoursPerTick = 3;
        }

        // Reporte de pre-vuelo: se enumeran TODAS las opciones cargadas (incluidas
        // las que están en false o bloqueadas de fábrica) para dejar constancia
        // inequívoca de la configuración efectiva en el arranque.
        //
        // EventSinks y PollingFallback no aparecen porque no son configurables:
        // en GearShift se registran siempre los siete sinks y no existe ruta
        // alternativa; en Classic no se registra ninguno y no hay hilo auxiliar.
        logger::info("[Config] Análisis de pre-vuelo completado. "
                     "Modo GearShift: {}, HoursPerTick: {}h, "
                     "VerificationProbe: {}, "
                     "UseCachedGlobal: {} (bloqueado de fábrica), "
                     "Teclas: [Modificador: {} | Principal: {}]", 
                     bEnableGearShiftEngine.load() ? "Activado" : "Desactivado", 
                     HoursPerTick, 
                     bEnableVerificationProbe.load() ? "Activado" : "Desactivado",
                     bUseCachedGlobal.load() ? "Activado" : "Desactivado",
                     ModifierKey, MainKey);
    }
};