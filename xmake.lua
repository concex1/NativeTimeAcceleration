-- ============================================================================
-- Archivo: xmake.lua
-- Función: Orquestación de la compilación cruzada y gestión de dependencias
-- ============================================================================

-- Reglas fundamentales de compilación y optimización para el entorno MSVC
add_rules("mode.debug", "mode.releasedbg", "mode.release")
add_rules("plugin.vsxmake.autoupdate")

-- Inclusión del subproyecto commonlibsf que expone las interfaces del motor CE2
includes("lib/commonlibsf")

-- Definición de metadatos del proyecto y transición formal al estándar C++23
set_project("NativeTimeAcceleration")
set_version("4.1.6")
set_languages("c++23")

-- Estructuración del objetivo de compilación principal (SFSE Target)
target("NativeTimeAcceleration")
    -- Invocación de la macro especializada que empaqueta las bibliotecas base
    add_rules("commonlibsf.plugin", {
        name = "NativeTimeAcceleration",
        author = "concex1",
        description = "Arquitectura Híbrida: Motor asíncrono de Cambio de Marcha y resolución geométrica de residuos temporales."
    })

    -- NOMINMAX: evita que <Windows.h> defina las macros min/max, que colisionan
    -- con std::min/std::max. Sin este define, cualquier invocación a std::max
    -- en el código se expande por el preprocesador antes de que el compilador
    -- resuelva el namespace y produce un error C2589.
    --
    -- Se aplica SOLO al target NativeTimeAcceleration: las librerías externas
    -- (commonlibsf, commonlib-shared) conservan su configuración por defecto
    -- para no alterar su contrato de compilación.
    add_defines("NOMINMAX")

    -- Consolidación de archivos fuente y mapeo estricto a la topología clásica
    add_files(
        "src/Main.cpp",
        "src/GearShiftEngine.cpp",
        "src/SleepWaitEventSink.cpp",
        "src/VerificationProbe.cpp"
    )
    
    add_headerfiles(
        "include/PCH.h",
        "include/Config.h",
        "include/EventSources.h",
        "include/GearShiftEngine.h",
        "include/SleepWaitEventSink.h",
        "include/VerificationProbe.h",
        "include/WaitEventsShim.h"
    )
    
    -- Designación del directorio de cabeceras para resolución nativa del compilador
    add_includedirs("include")
    
    -- Designación de cabecera precompilada para reducción drástica de tiempos de construcción
    set_pcxxheader("include/PCH.h")