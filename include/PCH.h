// ============================================================================
// Archivo: PCH.h
// Función: Cabecera Precompilada (Precompiled Header). Agrupa todas las
// dependencias masivas del ecosistema C++ y del Starfield Script Extender para
// mitigar los elevados tiempos de traducción de plantillas.
// ============================================================================
#pragma once

// ----------------------------------------------------------------------------
// NOMINMAX: debe estar definida ANTES de la primera inclusión que arrastre
// <Windows.h>, ya sea directa o transitivamente (por ejemplo, a través de
// <RE/Starfield.h>). Sin esta guarda, Windows.h define las macros min/max que
// colisionan con std::min/std::max y producen errores C2589 en cualquier uso
// no parentizado.
//
// La macro también se inyecta vía `add_defines("NOMINMAX")` en xmake.lua para
// cubrir rutas de compilación que no procesen este PCH (IDE, análisis estático,
// includes forzados con /FI). La guarda #ifndef evita warnings C4005 de
// redefinición cuando ambas vías convergen en la misma unidad de traducción.
// ----------------------------------------------------------------------------
#ifndef NOMINMAX
#define NOMINMAX
#endif

// Bibliotecas Base del Interfaz del Motor y Abstracción de Memoria
#include <SFSE/SFSE.h>
#include <RE/Starfield.h>
#include <RE/G/GameSettingCollection.h>
#include <RE/B/BSTEvent.h>
#include <RE/C/Calendar.h>

// Subsistema de Registro de Eventos asíncrono y en búfer de memoria
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

// Llamadas directas al Sistema Operativo subyacente para interrupción de hardware
#include <Windows.h>

// Librería Estándar de C++ moderna (C++23)
#include <thread>
#include <atomic>
#include <chrono>
#include <string>
#include <string_view>
#include <filesystem>
#include <mutex>
#include <cmath>
#include <cstdint>
#include <optional>

using namespace std::literals;
namespace logger = spdlog;